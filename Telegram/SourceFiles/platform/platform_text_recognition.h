/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace TextRecognition {

struct RectWithText {
	QString text;
	QRect rect;
	std::vector<QRect> glyphs;
};

struct Result {
	std::vector<RectWithText> items;
	bool success = false;

	inline operator bool() const {
		return success;
	}
};

[[nodiscard]] bool IsAvailable();
[[nodiscard]] Result RecognizeText(const QImage &image);

} // namespace TextRecognition
} // namespace Platform

// Platform dependent implementations.

#include "platform/win/text_recognition_win.h"