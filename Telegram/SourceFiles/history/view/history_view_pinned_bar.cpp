/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_bar.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_poll.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_service_message.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "core/loogrigram_lang.h"
#include "core/ui_integration.h"
#include "base/event_filter.h"
#include "base/weak_ptr.h"
#include "apiwrap.h"
#include "ui/chat/chat_style.h"
#include "ui/dynamic_image.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace HistoryView {
namespace {

[[nodiscard]] Ui::MessageBarContent ContentWithoutPreview(
		not_null<HistoryItem*> item,
		Fn<void()> repaint) {
	return Ui::MessageBarContent{
		.text = item->inReplyText(),
		.context = Core::TextContext({
			.session = &item->history()->session(),
			.repaint = std::move(repaint),
		}),
	};
}

class PreviewImage final : public Ui::DynamicImage {
public:
	PreviewImage(QImage source, bool spoiler);

	std::shared_ptr<Ui::DynamicImage> clone() override;
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _source;
	QPixmap _pixmap;
	QImage _frame;
	QImage _spoilerCache;
	std::unique_ptr<Ui::SpoilerAnimation> _spoiler;
	bool _spoilered = false;

};

PreviewImage::PreviewImage(QImage source, bool spoiler)
: _source(std::move(source))
, _pixmap(QPixmap::fromImage(_source, Qt::ColorOnly))
, _spoilered(spoiler) {
}

std::shared_ptr<Ui::DynamicImage> PreviewImage::clone() {
	return std::make_shared<PreviewImage>(_source, _spoilered);
}

void PreviewImage::subscribeToUpdates(Fn<void()> callback) {
	if (!callback || !_spoilered) {
		_spoiler = nullptr;
	} else {
		_spoiler = std::make_unique<Ui::SpoilerAnimation>(
			std::move(callback));
	}
}

QImage PreviewImage::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = QSize(size, size) * ratio;
	const auto to = QRect(0, 0, size, size);
	if (_frame.size() != full) {
		_frame = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);
	auto p = QPainter(&_frame);
	p.drawPixmap(to, _pixmap);
	if (_spoiler) {
		FillPreviewSpoiler(
			p,
			to,
			_pixmap,
			Ui::DefaultImageSpoiler().frame(_spoiler->index(
				crl::now(),
				On(PowerSaving::kChatSpoiler))),
			_spoilerCache);
	}
	p.end();
	return _frame;
}

[[nodiscard]] Ui::MessageBarContent ContentWithPreview(
		not_null<HistoryItem*> item,
		Image *preview,
		bool spoiler,
		Fn<void()> repaint) {
	auto result = ContentWithoutPreview(item, repaint);
	result.preview = std::make_shared<PreviewImage>(
		(preview
			? Images::Round(preview->original(), ImageRoundRadius::Small)
			: QImage()),
		preview && spoiler);
	return result;
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItem(
		not_null<HistoryItem*> item,
		Fn<void()> repaint) {
	return item->history()->session().changes().messageFlagsValue(
		item,
		Data::MessageUpdate::Flag::Edited
	) | rpl::map([=]() -> rpl::producer<Ui::MessageBarContent> {
		const auto media = item->media();
		if (!media || !media->hasReplyPreview()) {
			return rpl::single(ContentWithoutPreview(item, repaint));
		}
		constexpr auto kFullLoaded = 2;
		const auto loadedLevel = [=] {
			constexpr auto kSomeLoaded = 1;
			constexpr auto kNotLoaded = 0;
			const auto preview = media->replyPreview();
			return media->replyPreviewLoaded()
				? kFullLoaded
				: preview
				? kSomeLoaded
				: kNotLoaded;
		};
		return rpl::single(
			loadedLevel()
		) | rpl::then(
			item->history()->session().downloaderTaskFinished(
			) | rpl::map(loadedLevel)
		) | rpl::distinct_until_changed(
		) | rpl::take_while([=](int loadLevel) {
			return loadLevel < kFullLoaded;
		}) | rpl::then(
			rpl::single(kFullLoaded)
		) | rpl::map([=] {
			return ContentWithPreview(
				item,
				media->replyPreview(),
				media->hasSpoilerForPreview(),
				repaint);
		});
	}) | rpl::flatten_latest();
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		Fn<void()> repaint,
		bool alreadyLoaded = false) {
	if (!id) {
		return rpl::single(Ui::MessageBarContent());
	} else if (const auto item = session->data().message(id)) {
		return ContentByItem(item, repaint);
	} else if (alreadyLoaded) {
		return rpl::single(Ui::MessageBarContent()); // Deleted message?..
	}
	auto load = rpl::make_producer<Ui::MessageBarContent>([=](auto consumer) {
		consumer.put_next(Ui::MessageBarContent{
			.text = { tr::lng_contacts_loading(tr::now) },
		});
		const auto peer = session->data().peer(id.peer);
		const auto callback = [=] { consumer.put_done(); };
		session->api().requestMessageData(peer, id.msg, callback);
		return rpl::lifetime();
	});
	return std::move(
		load
	) | rpl::then(rpl::deferred([=] {
		return ContentByItemId(session, id, repaint, true);
	}));
}

} // namespace

rpl::producer<Ui::MessageBarContent> MessageBarContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		Fn<void()> repaint) {
	return ContentByItemId(session, id, std::move(repaint));
}

PinnedPill::PinnedPill(
	not_null<QWidget*> parent,
	not_null<const Ui::ChatStyle*> st,
	Fn<bool(QEvent*)> scrollViewportEvent,
	Fn<void()> hide,
	Fn<void()> showAll)
: AbstractButton(parent)
, _st(st)
, _scrollViewportEvent(std::move(scrollViewportEvent))
, _hide(std::move(hide))
, _showAll(std::move(showAll)) {
	setAccessibleName(LoogriGram::Lang::PinnedMessages());
	base::install_event_filter(this, parent, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			updatePosition();
		}
		return base::EventFilterResult::Continue;
	});
	_st->paletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
	QWidget::hide();
	updatePosition();
}

PinnedPill::~PinnedPill() = default;

void PinnedPill::setCount(int count) {
	const auto text = QString::number(count);
	if (_text != text) {
		_text = text;
		_textWidth = st::msgServiceFont->width(_text);
		setAccessibleDescription(_text);
		accessibilityDescriptionChanged();
		updatePosition();
		update();
	}
	if (_count != count) {
		_count = count;
		refreshShown(anim::type::normal);
	}
}

void PinnedPill::toggle(bool shown, anim::type animated) {
	_wanted = shown;
	refreshShown(animated);
}

// The count starts at zero until the pinned list is counted, and a pill
// reading 0 would be a lie either way, so it waits for one.
void PinnedPill::refreshShown(anim::type animated) {
	const auto shown = _wanted && (_count > 0);
	if (_shown != shown) {
		_shown = shown;
		setAttribute(Qt::WA_TransparentForMouseEvents, !shown);
		if (animated == anim::type::normal) {
			show();
			raise();
			_shownAnimation.start(
				[=] { animationCallback(); },
				shown ? 0. : 1.,
				shown ? 1. : 0.,
				st::historyToDownDuration);
			return;
		}
	}
	if (animated == anim::type::instant) {
		finishAnimating();
	}
}

void PinnedPill::finishAnimating() {
	_shownAnimation.stop();
	animationCallback();
}

void PinnedPill::animationCallback() {
	const auto hidden = !_shown && !_shownAnimation.animating();
	if (hidden != isHidden()) {
		setVisible(!hidden);
		if (!hidden) {
			raise();
		}
	}
	update();
}

void PinnedPill::updatePosition() {
	const auto &padding = st::historyPinnedPillPadding;
	const auto &icon = st::historyPinnedPillIcon;
	resize(
		(padding.left()
			+ icon.width()
			+ st::historyPinnedPillSkip
			+ _textWidth
			+ padding.right()),
		padding.top() + st::msgServiceFont->height + padding.bottom());

	// Level with the floating date, at the corner buttons' distance from
	// the right edge.
	moveToRight(
		st::historyToDownPosition.x(),
		st::msgServiceMargin.top(),
		parentWidget()->width());
}

void PinnedPill::paintEvent(QPaintEvent *e) {
	const auto opacity = _shownAnimation.value(_shown ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	auto p = Painter(this);
	p.setOpacity(opacity);
	ServiceMessagePainter::PaintBubble(p, _st, rect());

	const auto &padding = st::historyPinnedPillPadding;
	const auto &icon = st::historyPinnedPillIcon;
	const auto &fg = _st->msgServiceFg();
	icon.paint(
		p,
		QPoint(padding.left(), (height() - icon.height()) / 2),
		width(),
		fg->c);
	p.setPen(fg);
	p.setFont(st::msgServiceFont);
	p.drawText(
		padding.left() + icon.width() + st::historyPinnedPillSkip,
		padding.top() + st::msgServiceFont->ascent,
		_text);
}

void PinnedPill::wheelEvent(QWheelEvent *e) {
	if (_scrollViewportEvent) {
		_scrollViewportEvent(e);
	}
}

void PinnedPill::contextMenuEvent(QContextMenuEvent *e) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(
		LoogriGram::Lang::HidePinnedMessages(),
		_hide,
		&st::menuIconCancel);
	_menu->addAction(
		LoogriGram::Lang::AllPinnedMessages(),
		_showAll,
		&st::menuIconPin);
	_menu->popup(e->globalPos());
}

} // namespace HistoryView
