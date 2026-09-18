/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;

// LoogriGram: this file was dialogs_video_userpic, and VideoUserpic played a
// Premium user's profile video in the chat list and beside messages. Profile
// videos play in lists for nobody now; what remains paints the still photo.
void PaintUserpic(
	Painter &p,
	not_null<Entry*> entry,
	PeerData *peer,
	PeerUserpicView &view,
	const Ui::PaintContext &context);

} // namespace Dialogs::Ui
