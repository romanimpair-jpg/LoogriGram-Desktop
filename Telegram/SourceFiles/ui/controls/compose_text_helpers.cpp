/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/compose_text_helpers.h"

#include "core/mime_type.h"
#include "data/data_premium_limits.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text.h"
#include "ui/widgets/fields/input_field.h"

namespace Ui {
namespace {

constexpr auto kSendAsFilePasteMultiplier = 8;

[[nodiscard]] int SendAsFilePasteThreshold(not_null<Main::Session*> session) {
	return kSendAsFilePasteMultiplier
		* Data::PremiumLimits(session).messageLengthCurrent();
}

} // namespace

PreparedList PrepareTextAsFile(const QString &text) {
	auto content = text.toUtf8();
	auto result = PreparedList();
	auto file = PreparedFile(QString());
	file.content = content;
	file.displayName = u"message.txt"_q;
	file.size = content.size();
	file.information = std::make_unique<PreparedFileInformation>();
	file.information->filemime = u"text/plain"_q;
	result.files.push_back(std::move(file));
	return result;
}

LargeTextPasteResult CheckLargeTextPaste(
		not_null<Main::Session*> session,
		not_null<Ui::InputField*> field,
		not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return {};
	}
	const auto pasteText = Core::ReadMimeText(data);
	if (pasteText.isEmpty()) {
		return {};
	}
	const auto cursor = field->textCursor();
	const auto currentText = field->getLastText();
	const auto selStart = cursor.selectionStart();
	const auto selEnd = cursor.selectionEnd();
	const auto resultingSize = currentText.size()
		- (selEnd - selStart)
		+ pasteText.size();
	if (resultingSize < SendAsFilePasteThreshold(session)) {
		return {};
	}
	return {
		.exceeds = true,
		.resultingText = currentText.mid(0, selStart)
			+ pasteText
			+ currentText.mid(selEnd),
	};
}

} // namespace Ui
