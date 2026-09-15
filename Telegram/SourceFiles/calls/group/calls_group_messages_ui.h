/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/round_rect.h"

struct TextWithTags;

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Data {
struct ReactionId;
} // namespace Data

namespace Ui {
class ElasticScroll;
class EmojiButton;
class InputField;
class SendButton;
class PopupMenu;
class RpWidget;
} // namespace Ui

namespace Calls::Group {

struct Message;
struct MessageIdUpdate;
struct MessageDeleteRequest;

enum class MessagesMode {
	GroupCall,
	VideoStream,
};

class MessagesUi final {
public:
	MessagesUi(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		MessagesMode mode,
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<MessageIdUpdate> idUpdates,
		rpl::producer<bool> canManageValue,
		rpl::producer<bool> shown,
		Fn<bool(QPoint)> inputReserved = nullptr);
	~MessagesUi();

	void move(int left, int bottom, int width, int availableHeight);
	void raise();

	[[nodiscard]] rpl::producer<> hiddenShowRequested() const;
	[[nodiscard]] rpl::producer<MessageDeleteRequest> deleteRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct MessageView;

	void setupBadges();
	void setupList(
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown);
	void showList(const std::vector<Message> &list);
	void handleIdUpdates(rpl::producer<MessageIdUpdate> idUpdates);
	void toggleMessage(MessageView &entry, bool shown);
	void setContentFailed(MessageView &entry);
	void setContent(MessageView &entry);
	void updateMessageSize(MessageView &entry);
	bool updateMessageHeight(MessageView &entry);
	void animateMessageSent(MessageView &entry);
	void repaintMessage(MsgId id);
	void highlightMessage(MsgId id);
	void startHighlight(MsgId id);
	void recountHeights(std::vector<MessageView>::iterator i, int top);
	void appendMessage(const Message &data);
	void checkReactionContent(
		MessageView &entry,
		const TextWithEntities &text);
	void startReactionAnimation(MessageView &entry);
	void updateReactionPosition(MessageView &entry);
	void removeReaction(not_null<Ui::RpWidget*> widget);
	void setupMessagesWidget();

	void applyGeometry();
	void updateGeometries();

	void updateTopFade();
	void updateBottomFade();

	void receiveSomeMouseEvents();
	void receiveAllMouseEvents();
	void handleClick(const MessageView &entry, QPoint point);
	void showContextMenu(const MessageView &entry, QPoint globalPoint);

	[[nodiscard]] TextWithEntities nameText(not_null<PeerData*> peer);

	const not_null<QWidget*> _parent;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const MessagesMode _mode;
	const Fn<bool(QPoint)> _inputReserved;
	std::unique_ptr<Ui::ElasticScroll> _scroll;
	Ui::Animations::Simple _scrollToAnimation;
	Ui::RpWidget *_messages = nullptr;
	QImage _canvas;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::variable<bool> _canManage;
	rpl::event_stream<> _hiddenShowRequested;
	rpl::event_stream<MessageDeleteRequest> _deleteRequests;
	std::optional<std::vector<Message>> _hidden;
	std::vector<MessageView> _views;
	style::complex_color _messageBg;
	Ui::RoundRect _messageBgRect;
	MsgId _delayedHighlightId = 0;
	MsgId _highlightId = 0;
	Ui::Animations::Simple _highlightAnimation;

	QPoint _reactionBasePosition;
	rpl::lifetime _effectsLifetime;

	Ui::Text::String _liveBadge;
	Ui::Text::String _adminBadge;

	//Ui::Animations::Simple _topFadeAnimation;
	//Ui::Animations::Simple _bottomFadeAnimation;
	int _fadeHeight = 0;
	bool _topFadeShown = false;
	bool _bottomFadeShown = false;
	bool _streamMode = false;

	int _left = 0;
	int _bottom = 0;
	int _width = 0;
	int _availableHeight = 0;

	MsgId _revealedSpoilerId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
