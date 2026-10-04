/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_pack.h"

#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lottie/lottie_common.h"
#include "ui/emoji_config.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "ui/rect.h"
#include "main/main_session.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "chat_helpers/stickers_lottie.h"
#include "history/view/media/history_view_sticker.h"
#include "lottie/lottie_single_player.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>

namespace Stickers {
namespace {

constexpr auto kRefreshTimeout = 7200 * crl::time(1000);
constexpr auto kPremiumCachesCount = 8;

// LoogriGram: EffectType told Premium sticker effects (1) from emoji
// interactions (0) and message effects (2); only Premium sticker effects
// are left (2026-10-04). The value stays part of their cache key.
constexpr auto kPremiumEffectTag = uint8(1);

[[nodiscard]] const Lottie::ColorReplacements *ColorReplacements(int index) {
	Expects(index >= 1 && index <= 5);

	static const auto color1 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color1,
		.tag = 1,
	};
	static const auto color2 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color2,
		.tag = 2,
	};
	static const auto color3 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color3,
		.tag = 3,
	};
	static const auto color4 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color4,
		.tag = 4,
	};
	static const auto color5 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color5,
		.tag = 5,
	};
	static const auto list = std::array{
		&color1,
		&color2,
		&color3,
		&color4,
		&color5,
	};
	return list[index - 1];
}

} // namespace

EmojiPack::EmojiPack(not_null<Main::Session*> session)
: _session(session) {
	refresh();

	session->data().viewRemoved(
	) | rpl::filter([](not_null<const ViewElement*> view) {
		return view->isIsolatedEmoji() || view->isOnlyCustomEmoji();
	}) | rpl::on_next([=](not_null<const ViewElement*> item) {
		remove(item);
	}, _lifetime);

	Ui::Emoji::Updated(
	) | rpl::on_next([=] {
		refreshAll();
	}, _lifetime);
}

EmojiPack::~EmojiPack() = default;

bool EmojiPack::add(not_null<ViewElement*> view) {
	if (view->data()->textAppearing()
		|| view->Get<HistoryView::FakeBotAboutTop>()) {
		return false;
	} else if (const auto custom = view->onlyCustomEmoji()) {
		_onlyCustomItems.emplace(view);
		return true;
	} else if (const auto emoji = view->isolatedEmoji()) {
		_items[emoji].emplace(view);
		return true;
	}
	return false;
}

void EmojiPack::remove(not_null<const ViewElement*> view) {
	Expects(view->isIsolatedEmoji() || view->isOnlyCustomEmoji());

	if (view->isOnlyCustomEmoji()) {
		_onlyCustomItems.remove(view);
	} else if (const auto emoji = view->isolatedEmoji()) {
		const auto i = _items.find(emoji);
		Assert(i != end(_items));
		const auto j = i->second.find(view);
		Assert(j != end(i->second));
		i->second.erase(j);
		if (i->second.empty()) {
			_items.erase(i);
		}
	}
}

auto EmojiPack::stickerForEmoji(EmojiPtr emoji) -> Sticker {
	Expects(emoji != nullptr);

	const auto i = _map.find(emoji);
	if (i != end(_map)) {
		return { i->second.get(), nullptr };
	}
	if (!emoji->colored()) {
		return {};
	}
	const auto j = _map.find(emoji->original());
	if (j != end(_map)) {
		const auto index = emoji->variantIndex(emoji);
		return { j->second.get(), ColorReplacements(index) };
	}
	return {};
}

auto EmojiPack::stickerForEmoji(const IsolatedEmoji &emoji) -> Sticker {
	Expects(!emoji.empty());

	if (!v::is_null(emoji.items[1])) {
		return {};
	} else if (const auto regular = std::get_if<EmojiPtr>(&emoji.items[0])) {
		return stickerForEmoji(*regular);
	}
	return {};
}

std::unique_ptr<Lottie::SinglePlayer> EmojiPack::effectPlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath) {
	// Shortened copy from stickers_lottie module.
	const auto baseKey = document->bigFileBaseCacheKey();
	const auto keyShift = ((kPremiumEffectTag << 4) & 0xF0)
		| (uint8(ChatHelpers::StickerLottieSize::EmojiInteraction) & 0x0F);
	const auto key = Storage::Cache::Key{
		baseKey.high,
		baseKey.low + keyShift
	};
	const auto get = [=](int i, FnMut<void(QByteArray &&cached)> handler) {
		document->owner().cacheBigFile().get(
			{ key.high, key.low + i },
			std::move(handler));
	};
	const auto weak = base::make_weak(&document->session());
	const auto put = [=](int i, QByteArray &&cached) {
		crl::on_main(weak, [=, data = std::move(cached)]() mutable {
			weak->data().cacheBigFile().put(
				{ key.high, key.low + i },
				std::move(data));
		});
	};
	const auto size = HistoryView::Sticker::PremiumEffectSize(document);
	const auto request = Lottie::FrameRequest{
		size * style::DevicePixelRatio(),
	};
	auto &weakProvider = _sharedProviders[document];
	auto shared = [&] {
		if (const auto result = weakProvider.lock()) {
			return result;
		}
		const auto result = Lottie::SinglePlayer::SharedProvider(
			kPremiumCachesCount,
			get,
			put,
			Lottie::ReadContent(data, filepath),
			request,
			Lottie::Quality::High);
		weakProvider = result;
		return result;
	}();
	return std::make_unique<Lottie::SinglePlayer>(std::move(shared), request);
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		// LoogriGram: refreshAnimations() fetched the emoji interactions'
		// sticker set here and rescheduled this refresh once it answered.
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void EmojiPack::applySet(const MTPDmessages_stickerSet &data) {
	const auto stickers = collectStickers(data.vdocuments().v);
	auto was = base::take(_map);

	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			applyPack(data, stickers);
		});
	}

	for (const auto &[emoji, document] : _map) {
		const auto i = was.find(emoji);
		if (i == end(was)) {
			refreshItems(emoji);
		} else {
			if (i->second != document) {
				refreshItems(i->first);
			}
			was.erase(i);
		}
	}
	for (const auto &[emoji, document] : was) {
		refreshItems(emoji);
	}
	_refreshed.fire({});
}

void EmojiPack::refreshAll() {
	auto items = base::flat_set<not_null<HistoryItem*>>();
	auto count = 0;
	for (const auto &[emoji, list] : _items) {
		// refreshItems(list); // This call changes _items!
		count += int(list.size());
	}
	items.reserve(count);
	for (const auto &[emoji, list] : _items) {
		// refreshItems(list); // This call changes _items!
		for (const auto &view : list) {
			items.emplace(view->data());
		}
	}
	refreshItems(items);
	refreshItems(_onlyCustomItems);
}

void EmojiPack::refreshItems(EmojiPtr emoji) {
	const auto i = _items.find(IsolatedEmoji{ { emoji } });
	if (!emoji->colored()) {
		if (const auto count = emoji->variantsCount()) {
			for (auto i = 0; i != count; ++i) {
				refreshItems(emoji->variant(i + 1));
			}
		}
	}
	if (i == end(_items)) {
		return;
	}
	refreshItems(i->second);
}

void EmojiPack::refreshItems(
		const base::flat_set<not_null<ViewElement*>> &list) {
	auto items = base::flat_set<not_null<HistoryItem*>>();
	items.reserve(list.size());
	for (const auto &view : list) {
		items.emplace(view->data());
	}
	refreshItems(items);
}

void EmojiPack::refreshItems(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	for (const auto &item : items) {
		_session->data().requestItemViewRefresh(item);
	}
}

void EmojiPack::applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map) {
	const auto emoji = [&] {
		return Ui::Emoji::Find(qs(data.vemoticon()));
	}();
	const auto document = [&]() -> DocumentData * {
		for (const auto &id : data.vdocuments().v) {
			const auto i = map.find(id.v);
			if (i != end(map)) {
				return i->second.get();
			}
		}
		return nullptr;
	}();
	if (emoji && document) {
		_map.emplace_or_assign(emoji, document);
	}
}

base::flat_map<uint64, not_null<DocumentData*>> EmojiPack::collectStickers(
		const QVector<MTPDocument> &list) const {
	auto result = base::flat_map<uint64, not_null<DocumentData*>>();
	for (const auto &sticker : list) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			result.emplace(document->id, document);
		}
	}
	return result;
}

void EmojiPack::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _session, [=] {
		refresh();
	});
}

} // namespace Stickers
