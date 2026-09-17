/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// LoogriGram: what is left of compose_ai_button_factory.h after the AI compose
// feature was removed. None of this was ever about AI - the paste-as-file check
// and the expand-button line count merely shared a file with the AI button
// factory, and every caller of them is a plain compose or caption field. Kept
// under a name that says what they actually do.

class QMimeData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class InputField;
} // namespace Ui

namespace Ui {

struct PreparedList;

[[nodiscard]] PreparedList PrepareTextAsFile(const QString &text);

struct LargeTextPasteResult {
	bool exceeds = false;
	QString resultingText;
};

// A paste that would push the field past the message length limit several
// times over is offered as a file instead.
[[nodiscard]] LargeTextPasteResult CheckLargeTextPaste(
	not_null<Main::Session*> session,
	not_null<Ui::InputField*> field,
	not_null<const QMimeData*> data);

} // namespace Ui
