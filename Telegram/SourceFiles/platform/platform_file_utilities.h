/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

namespace Platform {
namespace File {

QString UrlToLocal(const QUrl &url);

namespace Unfused {

void UnsafeOpenUrl(const QString &url);
void UnsafeOpenEmailLink(const QString &email);
bool UnsafeShowOpenWithDropdown(const QString &filepath);
bool UnsafeShowOpenWith(const QString &filepath);
void UnsafeLaunch(const QString &filepath);

} // namespace Unfused

// All these functions may enter a nested event loop. Use with caution.
void UnsafeOpenUrl(const QString &url);
void UnsafeOpenEmailLink(const QString &email);
bool UnsafeShowOpenWithDropdown(const QString &filepath);
bool UnsafeShowOpenWith(const QString &filepath);
void UnsafeLaunch(const QString &filepath);

void PostprocessDownloaded(const QString &filepath);

} // namespace File

namespace FileDialog {

void InitLastPath();

bool Get(
	QPointer<QWidget> parent,
	QStringList &files,
	QByteArray &remoteContent,
	const QString &caption,
	const QString &filter,
	::FileDialog::internal::Type type,
	QString startFile = QString());

} // namespace FileDialog
} // namespace Platform

// Platform dependent implementations.

#include "platform/win/file_utilities_win.h"
