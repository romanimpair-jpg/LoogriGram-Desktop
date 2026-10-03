/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_file_origin.h"

#include <QtCore/QString>

#include <vector>

class History;
class PhotoData;
class DocumentData;
struct FilePrepareResult;

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct MessageToSend;
struct SendAction;
struct MusicSelectionItem {
	not_null<DocumentData*> document;
	Data::FileOrigin origin;
};

void SendExistingDocument(
	MessageToSend &&message,
	not_null<DocumentData*> document,
	std::optional<MsgId> localMessageId = std::nullopt);

void SendMusicSelection(
	MessageToSend &&message,
	std::vector<MusicSelectionItem> items);

void SendExistingPhoto(
	MessageToSend &&message,
	not_null<PhotoData*> photo,
	std::optional<MsgId> localMessageId = std::nullopt);

bool SendDice(MessageToSend &message);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MessageFlags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FilePrepareResult> &file);

} // namespace Api
