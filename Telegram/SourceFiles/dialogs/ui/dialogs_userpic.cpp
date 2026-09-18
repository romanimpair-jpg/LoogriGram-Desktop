/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_userpic.h"

#include "data/data_peer.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

namespace Dialogs::Ui {

void PaintUserpic(
		Painter &p,
		not_null<Entry*> entry,
		PeerData *peer,
		PeerUserpicView &view,
		const Ui::PaintContext &context) {
	if (peer) {
		peer->paintUserpicLeft(
			p,
			view,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else {
		entry->paintUserpic(p, view, context);
	}
}

} // namespace Dialogs::Ui
