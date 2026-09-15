/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;
class NumberInput;
class SettingsButton;

struct InviteLinkFields {
	QString link;
	QString label;
	TimeId expireDate = 0;
	int usageLimit = 0;
	bool requestApproval = false;
	bool isGroup = false;
	bool isPublic = false;
	bool globalRequestApproval = false;
	QString guardBotUsername;
	QString guardBotLink;
};

// LoogriGram: an invite link could carry a monthly price in stars. No
// money is taken through this client, so no link asks for any.

void EditInviteLinkBox(
	not_null<Ui::GenericBox*> box,
	const InviteLinkFields &data,
	Fn<void(InviteLinkFields)> done);

void CreateInviteLinkBox(
	not_null<Ui::GenericBox*> box,
	bool isGroup,
	bool isPublic,
	bool globalRequestApproval,
	const QString &guardBotUsername,
	const QString &guardBotLink,
	Fn<void(InviteLinkFields)> done);

} // namespace Ui
