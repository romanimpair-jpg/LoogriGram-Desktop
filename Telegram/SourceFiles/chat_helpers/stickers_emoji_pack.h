/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "base/timer.h"

#include <crl/crl_object_on_queue.h>

class HistoryItem;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Lottie {
class SinglePlayer;
class FrameProvider;
struct ColorReplacements;
} // namespace Lottie

namespace Ui {
namespace Text {
class String;
} // namespace Text
namespace Emoji {
class UniversalImages;
} // namespace Emoji
} // namespace Ui

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Stickers {

using IsolatedEmoji = Ui::Text::IsolatedEmoji;

class EmojiPack final {
public:
	using ViewElement = HistoryView::Element;

	struct Sticker {
		DocumentData *document = nullptr;
		const Lottie::ColorReplacements *replacements = nullptr;

		[[nodiscard]] bool empty() const {
			return (document == nullptr);
		}
		[[nodiscard]] explicit operator bool() const {
			return !empty();
		}
	};

	explicit EmojiPack(not_null<Main::Session*> session);
	~EmojiPack();

	bool add(not_null<ViewElement*> view);
	void remove(not_null<const ViewElement*> view);

	[[nodiscard]] Sticker stickerForEmoji(EmojiPtr emoji);
	[[nodiscard]] Sticker stickerForEmoji(const IsolatedEmoji &emoji);

	[[nodiscard]] rpl::producer<> refreshed() const {
		return _refreshed.events();
	}

	[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> effectPlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath);

private:
	class ImageLoader;

	void refresh();
	void refreshDelayed();
	void applySet(const MTPDmessages_stickerSet &data);
	void applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map);
	[[nodiscard]] auto collectStickers(const QVector<MTPDocument> &list) const
		-> base::flat_map<uint64, not_null<DocumentData*>>;
	void refreshAll();
	void refreshItems(EmojiPtr emoji);
	void refreshItems(const base::flat_set<not_null<ViewElement*>> &list);
	void refreshItems(const base::flat_set<not_null<HistoryItem*>> &items);

	const not_null<Main::Session*> _session;
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
	base::flat_map<
		IsolatedEmoji,
		base::flat_set<not_null<HistoryView::Element*>>> _items;
	mtpRequestId _requestId = 0;

	base::flat_set<not_null<HistoryView::Element*>> _onlyCustomItems;

	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Lottie::FrameProvider>> _sharedProviders;

	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
