/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class BoxContent;
} // namespace Ui

// LoogriGram: these lived in media/stories/media_stories_share, beside the
// story share box. Sharing a video at a timestamp is not a story feature, so
// they moved here when stories were removed.

namespace Media::View {

[[nodiscard]] QString FormatShareAtTime(TimeId seconds);

[[nodiscard]] object_ptr<Ui::BoxContent> PrepareShareAtTimeBox(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<HistoryItem*> item,
	TimeId videoTimestamp);

} // namespace Media::View
