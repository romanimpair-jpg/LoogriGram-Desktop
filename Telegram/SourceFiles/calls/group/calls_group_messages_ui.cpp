/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages_ui.h"

#include "base/unixtime.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "calls/group/calls_group_messages.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_message_reactions.h"
#include "data/data_message_reaction_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "ui/color_int_conversion.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "styles/style_calls.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info_levels.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Calls::Group {
namespace {

constexpr auto kMessageBgOpacity = 0.8;
constexpr auto kDarkOverOpacity = 0.25;
constexpr auto kColoredMessageBgOpacity = 0.65;
constexpr auto kAdminBadgeTextOpacity = 0.6;

class TransparentMessagesEventFilter final : public QObject {
public:
	explicit TransparentMessagesEventFilter(
		not_null<Ui::ElasticScroll*> scroll,
		Fn<bool(QPoint)> inputReserved,
		Fn<bool(QPoint)> handleClick);

private:
	bool eventFilter(QObject *watched, QEvent *event) override;
	bool mousePressFilter(
		QObject *watched,
		not_null<QMouseEvent*> event);
	bool wheelFilter(QObject *watched, not_null<QWheelEvent*> event);

	Fn<bool(QPoint)> _inputReserved;
	Fn<bool(QPoint)> _handleClick;

};

TransparentMessagesEventFilter::TransparentMessagesEventFilter(
	not_null<Ui::ElasticScroll*> scroll,
	Fn<bool(QPoint)> inputReserved,
	Fn<bool(QPoint)> handleClick)
: QObject(scroll)
, _inputReserved(std::move(inputReserved))
, _handleClick(std::move(handleClick)) {
}

bool TransparentMessagesEventFilter::eventFilter(
		QObject *watched,
		QEvent *event) {
	if (event->type() == QEvent::MouseButtonPress) {
		return mousePressFilter(
			watched,
			static_cast<QMouseEvent*>(event));
	} else if (event->type() == QEvent::Wheel) {
		return wheelFilter(
			watched,
			static_cast<QWheelEvent*>(event));
	}
	return false;
}

bool TransparentMessagesEventFilter::mousePressFilter(
		QObject *watched,
		not_null<QMouseEvent*> event) {
	Expects(parent()->isWidgetType());

	const auto scroll = static_cast<Ui::ElasticScroll*>(parent());
	if (watched != scroll->window()->windowHandle()) {
		return false;
	}
	const auto global = event->globalPos();
	const auto local = scroll->mapFromGlobal(global);
	if (!scroll->rect().contains(local)) {
		return false;
	}
	if (_inputReserved && _inputReserved(global)) {
		return false;
	}
	return _handleClick(local + QPoint(0, scroll->scrollTop()));
}

bool TransparentMessagesEventFilter::wheelFilter(
		QObject *watched,
		not_null<QWheelEvent*> event) {
	Expects(parent()->isWidgetType());

	const auto scroll = static_cast<Ui::ElasticScroll*>(parent());
	if (watched != scroll->window()->windowHandle()
		|| !scroll->scrollTopMax()) {
		return false;
	}
	const auto global = event->globalPosition().toPoint();
	const auto local = scroll->mapFromGlobal(global);
	if (!scroll->rect().contains(local)) {
		return false;
	}
	if (_inputReserved && _inputReserved(global)) {
		return false;
	}
	auto e = QWheelEvent(
		event->position(),
		event->globalPosition(),
		event->pixelDelta(),
		event->angleDelta(),
		event->buttons(),
		event->modifiers(),
		event->phase(),
		event->inverted(),
		event->source());
	e.setTimestamp(crl::now());
	QGuiApplication::sendEvent(scroll, &e);
	return true;
}

[[nodiscard]] int CountMessageRadius() {
	const auto minHeight = st::groupCallMessagePadding.top()
		+ st::messageTextStyle.font->height
		+ st::groupCallMessagePadding.bottom();
	return minHeight / 2;
}

void ReceiveSomeMouseEvents(
		not_null<Ui::ElasticScroll*> scroll,
		Fn<bool(QPoint)> inputReserved,
		Fn<bool(QPoint)> handleClick) {
	scroll->setAttribute(Qt::WA_TransparentForMouseEvents);
	qApp->installEventFilter(new TransparentMessagesEventFilter(
		scroll,
		std::move(inputReserved),
		std::move(handleClick)));
}

[[nodiscard]] base::unique_qptr<Ui::Menu::ItemBase> MakeMessageDateAction(
		not_null<Ui::PopupMenu*> menu,
		TimeId value) {
	const auto parent = menu->menu();

	const auto parsed = base::unixtime::parse(value);
	const auto date = parsed.date();
	const auto time = QLocale().toString(
		parsed.time(),
		QLocale::ShortFormat);
	const auto today = QDateTime::currentDateTime().date();
	const auto text = (date == today)
		? tr::lng_context_sent_today(tr::now, lt_time, time)
		: (date.addDays(1) == today)
		? tr::lng_context_sent_yesterday(tr::now, lt_time, time)
		: tr::lng_context_sent_date(
			tr::now,
			lt_date,
			langDayOfMonthFull(date),
			lt_time,
			time);
	const auto action = Ui::Menu::CreateAction(parent, text, [] {});
	action->setDisabled(true);
	return base::make_unique_q<Ui::Menu::Action>(
		menu->menu(),
		st::storiesCommentSentAt,
		action,
		nullptr,
		nullptr);
}

void ShowDeleteMessageConfirmation(
		std::shared_ptr<Ui::Show> show,
		MsgId id,
		not_null<PeerData*> from,
		bool canModerate,
		Fn<void(MessageDeleteRequest)> callback) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<bool> report;
			rpl::variable<bool> all;
			rpl::variable<bool> ban;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto confirmed = [=](Fn<void()> close) {
			callback(MessageDeleteRequest{
				.id = id,
				.deleteAllFrom = state->all.current() ? from.get() : nullptr,
				.ban = state->ban.current() ? from.get() : nullptr,
				.reportSpam = state->report.current(),
			});
			close();
		};
		Ui::ConfirmBox(box, {
			.text = tr::lng_selected_delete_sure_this(),
			.confirmed = confirmed,
			.confirmText = tr::lng_box_delete(),
			.labelStyle = &st::groupCallBoxLabel,
		});
		if (canModerate) {
			const auto check = [&](rpl::producer<QString> text) {
				const auto add = st::groupCallCheckbox.margin;
				const auto added = QMargins(0, add.top(), 0, add.bottom());
				const auto margin = st::boxRowPadding + added;

				return box->addRow(object_ptr<Ui::Checkbox>(
					box,
					std::move(text),
					false,
					st::groupCallCheckbox
				), margin)->checkedValue();
			};
			state->report = check(tr::lng_report_spam());
			state->all = check(tr::lng_delete_all_from_user(
				lt_user,
				rpl::single(from->shortName()))),
			state->ban = check(tr::lng_ban_user());
		}
	}));
}

} // namespace

struct MessagesUi::MessageView {
	MsgId id = 0;
	MsgId sendingId = 0;
	not_null<PeerData*> from;
	ClickHandlerPtr fromLink;
	Ui::Animations::Simple toggleAnimation;
	Ui::Animations::Simple sentAnimation;
	Data::ReactionId reactionId;
	std::unique_ptr<Ui::InfiniteRadialAnimation> sendingAnimation;
	std::unique_ptr<Ui::ReactionFlyAnimation> reactionAnimation;
	std::unique_ptr<Ui::RpWidget> reactionWidget;
	QPoint reactionShift;
	TextWithEntities original;
	Ui::PeerUserpicView view;
	Ui::Text::String name;
	Ui::Text::String text;
	TimeId date = 0;
	int top = 0;
	int width = 0;
	int left = 0;
	int height = 0;
	int realHeight = 0;
	int nameWidth = 0;
	int textLeft = 0;
	int textTop = 0;
	bool removed = false;
	bool sending = false;
	bool failed = false;
	bool admin = false;
	bool mine = false;
};

// LoogriGram: a paid comment was pinned in a strip above the chat, in a
// colour bought with the number of stars. Nothing is paid, so the strip
// and its background colours are gone.

MessagesUi::MessagesUi(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	MessagesMode mode,
	rpl::producer<std::vector<Message>> messages,
	rpl::producer<MessageIdUpdate> idUpdates,
	rpl::producer<bool> canManageValue,
	rpl::producer<bool> shown,
	Fn<bool(QPoint)> inputReserved)
: _parent(parent)
, _show(std::move(show))
, _mode(mode)
, _inputReserved(std::move(inputReserved))
, _canManage(std::move(canManageValue))
, _messageBg([] {
	auto result = st::groupCallBg->c;
	result.setAlphaF(kMessageBgOpacity);
	return result;
})
, _messageBgRect(CountMessageRadius(), _messageBg.color())
, _fadeHeight(st::normalFont->height * 2)
, _streamMode(_mode == MessagesMode::VideoStream) {
	setupBadges();
	setupList(std::move(messages), std::move(shown));
	handleIdUpdates(std::move(idUpdates));
}

MessagesUi::~MessagesUi() = default;

void MessagesUi::setupBadges() {
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto liveText = helper.paletteDependent(
		Ui::Text::CustomEmojiTextBadge(
			tr::lng_video_stream_live(tr::now).toUpper(),
			st::groupCallMessageBadge,
			st::groupCallMessageBadgeMargin));
	_liveBadge.setMarkedText(
		st::messageTextStyle,
		liveText,
		kMarkupTextOptions,
		helper.context());

	_adminBadge.setText(st::messageTextStyle, tr::lng_admin_badge(tr::now));
}

void MessagesUi::setupList(
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown) {
	rpl::combine(
		std::move(messages),
		std::move(shown)
	) | rpl::on_next([=](std::vector<Message> &&list, bool shown) {
		if (shown) {
			_hidden = std::nullopt;
		} else {
			_hidden = base::take(list);
		}
		showList(list);
	}, _lifetime);
}

void MessagesUi::showList(const std::vector<Message> &list) {
	auto from = begin(list);
	auto till = end(list);
	for (auto &entry : _views) {
		if (entry.removed) {
			continue;
		}
		const auto id = entry.id;
		const auto i = ranges::find(
			from,
			till,
			id,
			&Message::id);
		if (i == till) {
			toggleMessage(entry, false);
			continue;
		} else if (entry.failed != i->failed) {
			setContentFailed(entry);
			updateMessageSize(entry);
			repaintMessage(entry.id);
		} else if (entry.sending != (i->date == 0)) {
			animateMessageSent(entry);
		}
		entry.date = i->date;
		if (i == from) {
			++from;
		}
	}
	auto addedSendingToBottom = false;
	for (auto i = from; i != till; ++i) {
		const auto j = ranges::find(_views, i->id, &MessageView::id);
		if (j != end(_views)) {
			if (!j->removed) {
				continue;
			}
			if (j->failed != i->failed) {
				setContentFailed(*j);
				updateMessageSize(*j);
				repaintMessage(j->id);
			} else if (j->sending != (i->date == 0)) {
				animateMessageSent(*j);
			}
			j->date = i->date;
			toggleMessage(*j, true);
		} else {
			if (i + 1 == till && !i->date) {
				addedSendingToBottom = true;
			}
			appendMessage(*i);
		}
	}
	if (addedSendingToBottom) {
		const auto from = _scroll->scrollTop();
		const auto till = _scroll->scrollTopMax();
		if (from >= till) {
			return;
		}
		_scrollToAnimation.stop();
		_scrollToAnimation.start([=] {
			_scroll->scrollToY(_scroll->scrollTopMax()
				- _scrollToAnimation.value(0));
		}, till - from, 0, st::slideDuration, anim::easeOutCirc);
	}
}

void MessagesUi::handleIdUpdates(rpl::producer<MessageIdUpdate> idUpdates) {
	std::move(
		idUpdates
	) | rpl::on_next([=](MessageIdUpdate update) {
		const auto i = ranges::find(
			_views,
			update.localId,
			&MessageView::id);
		if (i == end(_views)) {
			return;
		}
		i->sendingId = update.localId;
		i->id = update.realId;
		if (_revealedSpoilerId == update.localId) {
			_revealedSpoilerId = update.realId;
		}
	}, _lifetime);
}

void MessagesUi::animateMessageSent(MessageView &entry) {
	const auto id = entry.id;
	entry.sending = false;
	entry.sentAnimation.start([=] {
		repaintMessage(id);
	}, 0., 1, st::slideDuration, anim::easeOutCirc);
	repaintMessage(id);
}

void MessagesUi::updateMessageSize(MessageView &entry) {
	const auto &padding = st::groupCallMessagePadding;

	const auto hasUserpic = !entry.failed;
	const auto userpicPadding = st::groupCallUserpicPadding;
	const auto userpicSize = st::groupCallUserpic;
	const auto leftSkip = hasUserpic
		? (userpicPadding.left() + userpicSize + userpicPadding.right())
		: padding.left();
	const auto widthSkip = leftSkip + padding.right();
	const auto inner = _width - widthSkip;

	const auto size = Ui::Text::CountOptimalTextSize(
		entry.text,
		std::min(st::groupCallWidth / 2, inner),
		inner);
	const auto space = st::normalFont->spacew;
	const auto nameWidth = entry.name.isEmpty() ? 0 : entry.name.maxWidth();
	const auto nameLineWidth = nameWidth
		? (nameWidth
			+ space
			+ _liveBadge.maxWidth()
			+ space
			+ _adminBadge.maxWidth())
		: 0;

	const auto nameHeight = entry.name.isEmpty()
		? 0
		: st::messageTextStyle.font->height;
	const auto textHeight = size.height();
	entry.width = widthSkip
		+ std::max(size.width(), std::min(nameLineWidth, inner));
	entry.left = _streamMode ? 0 : (_width - entry.width) / 2;
	entry.textLeft = leftSkip;
	entry.textTop = padding.top() + nameHeight;
	entry.nameWidth = std::min(
		nameWidth,
		(entry.width
			- widthSkip
			- space
			- _liveBadge.maxWidth()
			- space
			- _adminBadge.maxWidth()));
	updateReactionPosition(entry);

	const auto contentHeight = entry.textTop + textHeight + padding.bottom();
	const auto userpicHeight = hasUserpic
		? (userpicPadding.top() + userpicSize + userpicPadding.bottom())
		: 0;

	const auto skip = st::groupCallMessageSkip;
	entry.realHeight = skip + std::max(contentHeight, userpicHeight);
}

bool MessagesUi::updateMessageHeight(MessageView &entry) {
	const auto height = entry.toggleAnimation.animating()
		? anim::interpolate(
			0,
			entry.realHeight,
			entry.toggleAnimation.value(entry.removed ? 0. : 1.))
		: entry.realHeight;
	if (entry.height == height) {
		return false;
	}
	entry.height = height;
	return true;
}

void MessagesUi::setContentFailed(MessageView &entry) {
	entry.failed = true;
	entry.name = Ui::Text::String();
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		TextWithEntities().append(
			QString::fromUtf8("\xe2\x9d\x97\xef\xb8\x8f")
		).append(' ').append(
			tr::italic(u"Failed to send the message."_q)),
		kMarkupTextOptions,
		st::groupCallWidth / 8);
}

void MessagesUi::setContent(MessageView &entry) {
	const auto name = nameText(entry.from);
	entry.name = entry.admin
		? Ui::Text::String(
			st::messageTextStyle,
			name,
			kMarkupTextOptions,
			Ui::kQFixedMax)
		: Ui::Text::String();
	auto composed = entry.admin
		? entry.original
		: tr::link(name, 1).append(' ').append(entry.original);
	if (!entry.admin) {
		composed.text.replace(QChar('\n'), QChar(' '));
	}
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		composed,
		kMarkupTextOptions,
		st::groupCallWidth / 8);
	if (!entry.admin) {
		entry.text.setLink(1, entry.fromLink);
	}
	if (entry.text.hasSpoilers()) {
		const auto id = entry.id;
		const auto guard = base::make_weak(_messages);
		entry.text.setSpoilerLinkFilter([=](const ClickContext &context) {
			if (context.button != Qt::LeftButton || !guard) {
				return false;
			}
			const auto i = ranges::find(
				_views,
				_revealedSpoilerId,
				&MessageView::id);
			if (i != end(_views) && _revealedSpoilerId != id) {
				i->text.setSpoilerRevealed(false, anim::type::normal);
			}
			_revealedSpoilerId = id;
			return true;
		});
	}
}

void MessagesUi::toggleMessage(MessageView &entry, bool shown) {
	const auto id = entry.id;
	entry.removed = !shown;
	entry.toggleAnimation.start(
		[=] { repaintMessage(id); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		shown ? anim::easeOutCirc : anim::easeInCirc);
	repaintMessage(id);
}

void MessagesUi::repaintMessage(MsgId id) {
	auto i = ranges::find(_views, id, &MessageView::id);
	if (i == end(_views) && id < 0) {
		i = ranges::find(_views, id, &MessageView::sendingId);
	}
	if (i == end(_views)) {
		return;
	} else if (i->removed && !i->toggleAnimation.animating()) {
		const auto top = i->top;
		i = _views.erase(i);
		recountHeights(i, top);
		return;
	}
	if (!i->sending && !i->sentAnimation.animating()) {
		i->sendingAnimation = nullptr;
	}
	if (!i->toggleAnimation.animating() && id == _delayedHighlightId) {
		highlightMessage(base::take(_delayedHighlightId));
	}
	if (i->toggleAnimation.animating() || i->height != i->realHeight) {
		if (updateMessageHeight(*i)) {
			recountHeights(i, i->top);
			return;
		}
	}
	_messages->update(0, i->top, _messages->width(), i->height);
}

void MessagesUi::recountHeights(
		std::vector<MessageView>::iterator i,
		int top) {
	auto from = top;
	for (auto e = end(_views); i != e; ++i) {
		i->top = top;
		top += i->height;
		updateReactionPosition(*i);
	}
	if (_views.empty()) {
		_scrollToAnimation.stop();
		delete base::take(_messages);
		_scroll = nullptr;
	} else {
		updateGeometries();
		_messages->update(0, from, _messages->width(), top - from);
	}
}

void MessagesUi::appendMessage(const Message &data) {
	const auto top = _views.empty()
		? 0
		: (_views.back().top + _views.back().height);

	if (!_scroll) {
		setupMessagesWidget();
	}

	const auto id = data.id;
	const auto peer = data.peer;
	auto &entry = _views.emplace_back(MessageView{
		.id = id,
		.from = peer,
		.original = data.text,
		.date = data.date,
		.top = top,
		.sending = !data.date,
		.admin = data.admin && _streamMode,
		.mine = data.mine,
	});
	const auto repaint = [=] {
		repaintMessage(id);
	};
	entry.fromLink = std::make_shared<LambdaClickHandler>([=] {
		_show->show(
			PrepareShortInfoBox(peer, _show, &st::storiesShortInfoBox));
	});
	if (data.failed) {
		setContentFailed(entry);
	} else {
		setContent(entry);
	}
	updateMessageSize(entry);
	if (entry.sending) {
		using namespace Ui;
		const auto &st = st::defaultInfiniteRadialAnimation;
		entry.sendingAnimation = std::make_unique<InfiniteRadialAnimation>(
			repaint,
			st);
		entry.sendingAnimation->start(0);
	}
	entry.height = 0;
	toggleMessage(entry, true);
	checkReactionContent(entry, data.text);
}

// LoogriGram: the top stars donors wore a crown before their name, ranked
// by what they had paid. Deleted with the donor list.
TextWithEntities MessagesUi::nameText(not_null<PeerData*> peer) {
	return tr::bold(peer->shortName());
}

void MessagesUi::checkReactionContent(
		MessageView &entry,
		const TextWithEntities &text) {
	auto outLength = 0;
	using Type = Data::Reactions::Type;
	const auto reactions = &_show->session().data().reactions();
	const auto set = [&](Data::ReactionId id) {
		reactions->preloadAnimationsFor(id);
		entry.reactionId = std::move(id);
	};
	if (text.entities.size() == 1
		&& text.entities.front().type() == EntityType::CustomEmoji
		&& text.entities.front().offset() == 0
		&& text.entities.front().length() == text.text.size()) {
		set({ text.entities.front().data().toULongLong() });
	} else if (const auto emoji = Ui::Emoji::Find(text.text, &outLength)) {
		if (outLength < text.text.size()) {
			return;
		}
		const auto &all = reactions->list(Type::All);
		for (const auto &reaction : all) {
			if (reaction.id.custom()) {
				continue;
			}
			const auto &text = reaction.id.emoji();
			if (Ui::Emoji::Find(text) != emoji) {
				continue;
			}
			set(reaction.id);
			break;
		}
	}
}

void MessagesUi::startReactionAnimation(MessageView &entry) {
	entry.reactionWidget = std::make_unique<Ui::RpWidget>(_parent);
	const auto raw = entry.reactionWidget.get();
	raw->show();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (!_effectsLifetime) {
		rpl::combine(
			_scroll->scrollTopValue(),
			_scroll->RpWidget::positionValue()
		) | rpl::on_next([=](int yshift, QPoint point) {
			_reactionBasePosition = point - QPoint(0, yshift);
			for (auto &view : _views) {
				updateReactionPosition(view);
			}
		}, _effectsLifetime);
	}

	entry.reactionAnimation = std::make_unique<Ui::ReactionFlyAnimation>(
		&_show->session().data().reactions(),
		Ui::ReactionFlyAnimationArgs{
			.id = entry.reactionId,
			.effectOnly = true,
		},
		[=] { raw->update(); },
		st::reactionInlineImage);
	updateReactionPosition(entry);

	const auto effectSize = st::reactionInlineImage * 2;
	const auto animation = entry.reactionAnimation.get();
	raw->resize(effectSize, effectSize);
	raw->paintRequest() | rpl::on_next([=] {
		if (animation->finished()) {
			crl::on_main(raw, [=] {
				removeReaction(raw);
			});
			return;
		}
		auto p = QPainter(raw);
		const auto size = raw->width();
		const auto skip = (size - st::reactionInlineImage) / 2;
		const auto target = QRect(
			QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		animation->paintGetArea(
			p,
			QPoint(),
			target,
			st::radialFg->c,
			QRect(),
			crl::now());
	}, raw->lifetime());
}

void MessagesUi::removeReaction(not_null<Ui::RpWidget*> widget) {
	const auto i = ranges::find_if(_views, [&](const MessageView &entry) {
		return entry.reactionWidget.get() == widget;
	});
	if (i != end(_views)) {
		i->reactionId = {};
		i->reactionWidget = nullptr;
		i->reactionAnimation = nullptr;
	};
}

void MessagesUi::updateReactionPosition(MessageView &entry) {
	if (const auto widget = entry.reactionWidget.get()) {
		if (entry.failed) {
			widget->resize(0, 0);
			return;
		}
		const auto padding = st::groupCallMessagePadding;
		const auto userpicSize = st::groupCallUserpic;
		const auto userpicPadding = st::groupCallUserpicPadding;
		const auto esize = st::emojiSize;
		const auto eleft = entry.text.maxWidth() - st::emojiPadding - esize;
		const auto etop = (st::normalFont->height - esize) / 2;
		const auto effectSize = st::reactionInlineImage * 2;
		entry.reactionShift = QPoint(entry.left, entry.top)
			+ QPoint(
				userpicPadding.left() + userpicSize + userpicPadding.right(),
				padding.top())
			+ QPoint(eleft + (esize / 2), etop + (esize / 2))
			- QPoint(effectSize / 2, effectSize / 2);
		widget->move(_reactionBasePosition + entry.reactionShift);
	}
}

void MessagesUi::updateTopFade() {
	const auto topFadeShown = (_scroll->scrollTop() > 0);
	if (_topFadeShown != topFadeShown) {
		_topFadeShown = topFadeShown;
		//const auto from = topFadeShown ? 0. : 1.;
		//const auto till = topFadeShown ? 1. : 0.;
		//_topFadeAnimation.start([=] {
			_messages->update(
				0,
				_scroll->scrollTop(),
				_messages->width(),
				_fadeHeight);
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::updateBottomFade() {
	const auto max = _scroll->scrollTopMax();
	const auto bottomFadeShown = (_scroll->scrollTop() < max);
	if (_bottomFadeShown != bottomFadeShown) {
		_bottomFadeShown = bottomFadeShown;
		//const auto from = bottomFadeShown ? 0. : 1.;
		//const auto till = bottomFadeShown ? 1. : 0.;
		//_bottomFadeAnimation.start([=] {
			_messages->update(
				0,
				_scroll->scrollTop() + _scroll->height() - _fadeHeight,
				_messages->width(),
				_fadeHeight);
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::setupMessagesWidget() {
	_scroll = std::make_unique<Ui::ElasticScroll>(
		_parent,
		st::groupCallMessagesScroll);
	const auto scroll = _scroll.get();

	_messages = scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(scroll));
	_messages->move(0, 0);
	rpl::combine(
		scroll->scrollTopValue(),
		scroll->heightValue(),
		_messages->heightValue()
	) | rpl::on_next([=] {
		updateTopFade();
		updateBottomFade();
	}, scroll->lifetime());

	if (_mode == MessagesMode::GroupCall) {
		receiveSomeMouseEvents();
	} else {
		receiveAllMouseEvents();
	}

	_messages->paintRequest() | rpl::on_next([=](QRect clip) {
		const auto start = scroll->scrollTop();
		const auto end = start + scroll->height();
		const auto ratio = style::DevicePixelRatio();
		if ((_canvas.width() < scroll->width() * ratio)
			|| (_canvas.height() < scroll->height() * ratio)) {
			_canvas = QImage(
				scroll->size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_canvas.setDevicePixelRatio(ratio);
		}
		auto p = Painter(&_canvas);

		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.fillRect(QRect(QPoint(), scroll->size()), QColor(0, 0, 0, 0));

		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		const auto now = crl::now();
		const auto skip = st::groupCallMessageSkip;
		const auto padding = st::groupCallMessagePadding;
		p.translate(0, -start);
		for (auto &entry : _views) {
			if (entry.height <= skip || entry.top + entry.height <= start) {
				continue;
			} else if (entry.top >= end) {
				break;
			}
			const auto x = entry.left;
			const auto y = entry.top;
			const auto use = entry.realHeight - skip;
			const auto width = entry.width;
			p.setPen(Qt::NoPen);
			const auto scaled = (entry.height < entry.realHeight);
			if (scaled) {
				const auto used = entry.height - skip;
				const auto mx = scaled ? (x + (width / 2)) : 0;
				const auto my = scaled ? (y + (used / 2)) : 0;
				const auto scale = used / float64(use);
				p.save();
				p.translate(mx, my);
				p.scale(scale, scale);
				p.setOpacity(scale);
				p.translate(-mx, -my);
			}
			// LoogriGram: a paid comment was painted in the colour its
			// price bought. Every comment gets the plain background.
			if (!_streamMode || entry.admin) {
				_messageBgRect.paint(p, { x, y, width, use });
			}

			const auto textLeft = entry.textLeft;
			const auto hasUserpic = !entry.failed;
			if (hasUserpic) {
				const auto userpicSize = st::groupCallUserpic;
				const auto userpicPadding = st::groupCallUserpicPadding;
				const auto position = QPoint(
					x + userpicPadding.left(),
					y + userpicPadding.top());
				const auto rect = QRect(
					position,
					QSize(userpicSize, userpicSize));
				entry.from->paintUserpic(p, entry.view, {
					.position = position,
					.size = userpicSize,
					.shape = Ui::PeerUserpicShape::Circle,
				});
				if (const auto animation = entry.sendingAnimation.get()) {
					auto hq = PainterHighQualityEnabler(p);
					auto pen = st::groupCallBg->p;
					const auto shift = userpicPadding.left();
					pen.setWidthF(shift);
					p.setPen(pen);
					p.setBrush(Qt::NoBrush);
					const auto state = animation->computeState();
					const auto sent = entry.sending
						? 0.
						: entry.sentAnimation.value(1.);
					p.setOpacity(state.shown * (1. - sent));
					p.drawArc(
						rect.marginsRemoved({ shift, shift, shift, shift }),
						state.arcFrom,
						state.arcLength);
					p.setOpacity(1.);
				}
			}

			p.setPen(st::white);
			if (!entry.name.isEmpty()) {
				const auto space = st::normalFont->spacew;
				entry.name.draw(p, {
					.position = {
						x + textLeft,
						y + padding.top(),
					},
					.availableWidth = entry.nameWidth,
					.palette = &st::groupCallMessagePalette,
					.elisionLines = 1,
				});
				const auto liveLeft = x + textLeft + entry.nameWidth + space;
				_liveBadge.draw(p, {
					.position = { liveLeft, y + padding.top() },
				});

				p.setOpacity(kAdminBadgeTextOpacity);
				const auto adminLeft = x
					+ entry.width
					- padding.right()
					- _adminBadge.maxWidth();
				_adminBadge.draw(p, {
					.position = { adminLeft, y + padding.top() },
				});
				p.setOpacity(1.);
			}
			const auto textRight = padding.right();
			entry.text.draw(p, {
				.position = {
					x + textLeft,
					y + entry.textTop,
				},
				.availableWidth = entry.width - textLeft - textRight,
				.palette = &st::groupCallMessagePalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = now,
				.paused = !_messages->window()->isActiveWindow(),
			});
			if (!scaled && entry.reactionId && !entry.reactionAnimation) {
				startReactionAnimation(entry);
			}

			if (scaled) {
				p.restore();
			}
		}
		p.translate(0, start);

		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.setPen(Qt::NoPen);

		const auto topFade = (//_topFadeAnimation.value(
			_topFadeShown ? 1. : 0.);
		if (topFade) {
			auto gradientTop = QLinearGradient(0, 0, 0, _fadeHeight);
			gradientTop.setStops({
				{ 0., QColor(255, 255, 255, 0) },
				{ 1., QColor(255, 255, 255, 255) },
			});
			p.setOpacity(topFade);
			p.setBrush(gradientTop);
			p.drawRect(0, 0, scroll->width(), _fadeHeight);
			p.setOpacity(1.);
		}
		const auto bottomFade = (//_bottomFadeAnimation.value(
			_bottomFadeShown ? 1. : 0.);
		if (bottomFade) {
			const auto till = scroll->height();
			const auto from = till - _fadeHeight;
			auto gradientBottom = QLinearGradient(0, from, 0, till);
			gradientBottom.setStops({
				{ 0., QColor(255, 255, 255, 255) },
				{ 1., QColor(255, 255, 255, 0) },
			});
			p.setBrush(gradientBottom);
			p.drawRect(0, from, scroll->width(), _fadeHeight);
		}
		QPainter(_messages).drawImage(
			QRect(QPoint(0, start), scroll->size()),
			_canvas,
			QRect(QPoint(), scroll->size() * ratio));
	}, _messages->lifetime());

	scroll->show();
	applyGeometry();
}

void MessagesUi::receiveSomeMouseEvents() {
	ReceiveSomeMouseEvents(_scroll.get(), _inputReserved, [=](QPoint point) {
		for (const auto &entry : _views) {
			if (entry.failed || entry.top + entry.height <= point.y()) {
				continue;
			} else if (entry.top < point.y()
				&& entry.left < point.x()
				&& entry.left + entry.width > point.x()) {
				handleClick(entry, point);
				return true;
			}
			break;
		}
		return false;
	});
}

void MessagesUi::receiveAllMouseEvents() {
	_messages->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::MouseButtonPress) {
			return;
		}
		const auto m = static_cast<QMouseEvent*>(e.get());
		const auto point = m->pos();
		for (const auto &entry : _views) {
			if (entry.failed || entry.top + entry.height <= point.y()) {
				continue;
			} else if (entry.top < point.y()
				&& entry.left < point.x()
				&& entry.left + entry.width > point.x()) {
				if (m->button() == Qt::LeftButton) {
					handleClick(entry, point);
				} else {
					showContextMenu(entry, m->globalPos());
				}
			}
			break;
		}
	}, _messages->lifetime());
}

void MessagesUi::handleClick(const MessageView &entry, QPoint point) {
	const auto padding = st::groupCallMessagePadding;
	const auto userpicSize = st::groupCallUserpic;
	const auto userpicPadding = st::groupCallUserpicPadding;
	const auto userpic = QRect(
		entry.left + userpicPadding.left(),
		entry.top + userpicPadding.top(),
		userpicSize,
		userpicSize);
	const auto name = entry.name.isEmpty()
		? QRect()
		: QRect(
			entry.left + entry.textLeft,
			entry.top + padding.top(),
			entry.nameWidth,
			st::messageTextStyle.font->height);
	const auto link = (userpic.contains(point) || name.contains(point))
		? entry.fromLink
		: entry.text.getState(point - QPoint(
			entry.left + entry.textLeft,
			entry.top + entry.textTop
		), entry.width - entry.textLeft - padding.right()).link;
	if (link) {
		ActivateClickHandler(_messages, link, Qt::LeftButton);
	}
}

void MessagesUi::showContextMenu(
		const MessageView &entry,
		QPoint globalPoint) {
	if (_menu || !entry.date || entry.failed) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_parent,
		st::groupCallPopupMenuWithIcons);
	_menu->addAction(MakeMessageDateAction(_menu.get(), entry.date));
	const auto &original = entry.original;
	const auto canCopy = !original.empty();
	const auto canDelete = entry.mine || _canManage.current();
	if (canCopy || canDelete) {
		_menu->addSeparator(&st::mediaviewWideMenuSeparator);
	}
	if (canCopy) {
		_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
			TextUtilities::SetClipboardText(
				TextForMimeData::WithExpandedLinks(original));
		}, &st::mediaMenuIconCopy);
	}
	if (canDelete) {
		const auto id = entry.id;
		const auto from = entry.from;
		const auto canModerate = _canManage.current() && !entry.mine;
		_menu->addAction(tr::lng_context_delete_msg(tr::now), [=] {
			ShowDeleteMessageConfirmation(_show, id, from, canModerate, [=](
					MessageDeleteRequest request) {
				_deleteRequests.fire_copy(request);
			});
		}, &st::mediaMenuIconDelete);
	}
	_menu->popup(globalPoint);
}

void MessagesUi::highlightMessage(MsgId id) {
	if (!_scroll) {
		return;
	}
	const auto i = ranges::find(_views, id, &MessageView::id);
	if (i == end(_views) || i->top < 0) {
		return;
	} else if (i->toggleAnimation.animating()) {
		_delayedHighlightId = id;
		return;
	}
	_delayedHighlightId = 0;
	const auto top = std::clamp(
		i->top - ((_scroll->height() - i->realHeight) / 2),
		0,
		i->top);
	const auto to = top - i->top;
	const auto from = _scroll->scrollTop() - i->top;
	if (from == to) {
		startHighlight(id);
		return;
	}
	_scrollToAnimation.stop();
	_scrollToAnimation.start([=] {
		const auto i = ranges::find(_views, id, &MessageView::id);
		if (i == end(_views)) {
			_scrollToAnimation.stop();
			return;
		}
		_scroll->scrollToY(i->top + _scrollToAnimation.value(to));
		if (!_scrollToAnimation.animating()) {
			startHighlight(id);
		}
	}, from, to, st::slideDuration, anim::easeOutCirc);
}

void MessagesUi::startHighlight(MsgId id) {
	_highlightId = id;
	_highlightAnimation.start([=] {
		repaintMessage(id);
	}, 0., 3., 1000);
}

void MessagesUi::applyGeometry() {
	if (_scroll) {
		auto top = 0;
		for (auto &entry : _views) {
			entry.top = top;

			updateMessageSize(entry);
			updateMessageHeight(entry);

			top += entry.height;
		}
	}
	updateGeometries();
}

void MessagesUi::updateGeometries() {
	if (_scroll) {
		const auto scrollBottom = (_scroll->scrollTop() + _scroll->height());
		const auto atBottom = (scrollBottom >= _messages->height());
		const auto bottom = _bottom;

		const auto height = _views.empty()
			? 0
			: (_views.back().top + _views.back().height);
		_messages->resize(_width, height);

		const auto min = std::min(height, _availableHeight);
		_scroll->setGeometry(_left, bottom - min, _width, min);

		if (atBottom) {
			_scroll->scrollToY(std::max(height - _scroll->height(), 0));
		}
	}
}

void MessagesUi::move(int left, int bottom, int width, int availableHeight) {
	const auto min = st::groupCallWidth * 2 / 6;
	if (width < min) {
		const auto add = min - width;
		width += add;
		left -= add / 2;
	}
	if (_left != left
		|| _bottom != bottom
		|| _width != width
		|| _availableHeight != availableHeight) {
		_left = left;
		_bottom = bottom;
		_width = width;
		_availableHeight = availableHeight;
		applyGeometry();
	}
}

void MessagesUi::raise() {
	if (_scroll) {
		_scroll->raise();
	}
	for (const auto &view : _views) {
		if (const auto widget = view.reactionWidget.get()) {
			widget->raise();
		}
	}
}

rpl::producer<> MessagesUi::hiddenShowRequested() const {
	return _hiddenShowRequested.events();
}

rpl::producer<MessageDeleteRequest> MessagesUi::deleteRequests() const {
	return _deleteRequests.events();
}

rpl::lifetime &MessagesUi::lifetime() {
	return _lifetime;
}

} // namespace Calls::Group
