/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/loogrigram_update.h"

#include "base/timer.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/loogrigram_build_tag.h"
#include "logs.h"
#include "settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Core::LoogriGram {
namespace {

// Our repository, not Telegram's. The updater never contacts Telegram, and
// the request carries no account information - it is an unauthenticated GET
// of a public endpoint.
constexpr auto kLatestReleaseUrl = "https://api.github.com/repos/"
	"romanimpair-jpg/LoogriGram-Desktop/releases/latest";

// The asset CI attaches to every release. Only Windows is published, so on
// any other platform the lookup simply finds nothing and the check stops.
constexpr auto kAssetName = "LoogriGram.exe";

// Let the app finish starting before spending bandwidth on this. Nothing
// depends on the result, so being late costs nothing.
constexpr auto kStartDelay = crl::time(10000);

// A real build is a bit over 200MB. This is not a security check - we trust
// GitHub over TLS for that - it only stops an error page or a truncated
// download from being swapped in as if it were the program.
constexpr auto kMinimumSize = 32 * 1024 * 1024;

bool Started/* = false*/;

[[nodiscard]] bool TagLooksReal(const QString &tag) {
	// A local build carries "dev", which matches no published tag and would
	// otherwise make every launch download the newest release over the top of
	// whatever the developer just compiled.
	return !tag.isEmpty() && (tag != u"dev"_q);
}

struct State final : QObject {
	explicit State(QObject *parent) : QObject(parent), manager(this) {
	}

	QNetworkAccessManager manager;
	base::Timer timer;
};

[[nodiscard]] QNetworkRequest PrepareRequest(const QString &url) {
	auto request = QNetworkRequest(QUrl(url));

	// GitHub rejects requests without a User-Agent. It identifies the program,
	// nothing about the person running it.
	request.setRawHeader("User-Agent", "LoogriGram-Updater");
	request.setRawHeader("Accept", "application/vnd.github+json");

	// browser_download_url redirects to a storage host, so redirects have to
	// be followed, but never down from https to http.
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	return request;
}

void ShowRestartBox() {
	Ui::show(Ui::MakeConfirmBox({
		.text = u"A newer LoogriGram has been installed beside this one. "
			"Restart to run it?"_q,
		.confirmed = [] { Core::Restart(); },
		.confirmText = u"Restart"_q,
		.cancelText = u"Later"_q,
	}));
}

// Swaps the downloaded build in for the running one.
//
// A running executable cannot be deleted or overwritten on Windows, but it can
// be renamed: the image is mapped by file object rather than by path. So the
// running binary is moved aside to .previous and the new one takes its place.
// The running process carries on from the renamed image, and the next launch -
// which resolves the same path it was started from - gets the new build.
//
// The old binary is kept rather than deleted, both because Windows would
// refuse to remove it while it is running and because it is the way back if a
// build turns out to be broken.
[[nodiscard]] bool ApplyUpdate(const QByteArray &data) {
	if (data.size() < kMinimumSize || !data.startsWith("MZ")) {
		LOG(("Update Error: refusing %1 bytes that are not a Windows binary."
			).arg(data.size()));
		return false;
	}
	const auto current = cExeDir() + cExeName();
	const auto previous = current + u".previous"_q;
	const auto fresh = current + u".new"_q;

	QFile::remove(fresh);
	{
		auto file = QFile(fresh);
		if (!file.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: could not write %1.").arg(fresh));
			return false;
		} else if (file.write(data) != data.size() || !file.flush()) {
			LOG(("Update Error: could not write all of %1.").arg(fresh));
			file.close();
			QFile::remove(fresh);
			return false;
		}
	}
	QFile::remove(previous);
	if (!QFile::rename(current, previous)) {
		LOG(("Update Error: could not move %1 aside.").arg(current));
		QFile::remove(fresh);
		return false;
	} else if (!QFile::rename(fresh, current)) {
		LOG(("Update Error: could not put the new build at %1.").arg(current));

		// Put the running binary back where it was, so the next launch still
		// finds a program at the expected path.
		QFile::rename(previous, current);
		QFile::remove(fresh);
		return false;
	}
	LOG(("Update Info: installed, previous build kept at %1.").arg(previous));
	return true;
}

void DownloadAndApply(not_null<State*> state, const QString &url) {
	const auto reply = state->manager.get(PrepareRequest(url));
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Update Error: download failed, %1."
				).arg(reply->errorString()));
			return;
		} else if (ApplyUpdate(reply->readAll())) {
			ShowRestartBox();
		}
	});
}

void CheckLatestRelease(not_null<State*> state) {
	const auto reply = state->manager.get(PrepareRequest(kLatestReleaseUrl));
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Update Info: check failed, %1.").arg(reply->errorString()));
			return;
		}
		const auto json = QJsonDocument::fromJson(reply->readAll());
		if (!json.isObject()) {
			LOG(("Update Error: malformed reply from GitHub."));
			return;
		}
		const auto root = json.object();
		const auto tag = root.value(u"tag_name"_q).toString();
		if (tag.isEmpty()) {
			LOG(("Update Error: release carries no tag."));
			return;
		} else if (tag == QString::fromLatin1(LOOGRIGRAM_BUILD_TAG)) {
			DEBUG_LOG(("Update Info: already on %1.").arg(tag));
			return;
		}
		const auto assets = root.value(u"assets"_q).toArray();
		for (const auto &value : assets) {
			const auto asset = value.toObject();
			if (asset.value(u"name"_q).toString() != kAssetName) {
				continue;
			}
			const auto url = asset.value(
				u"browser_download_url"_q).toString();
			if (url.isEmpty()) {
				break;
			}
			LOG(("Update Info: %1 is newer than %2, downloading."
				).arg(tag, QString::fromLatin1(LOOGRIGRAM_BUILD_TAG)));
			DownloadAndApply(state, url);
			return;
		}
		LOG(("Update Error: release %1 has no %2."
			).arg(tag, QString::fromLatin1(kAssetName)));
	});
}

} // namespace

void StartUpdateCheck() {
#ifdef Q_OS_WIN
	if (Started) {
		return;
	} else if (!TagLooksReal(QString::fromLatin1(LOOGRIGRAM_BUILD_TAG))) {
		LOG(("Update Info: locally built, not checking for updates."));
		return;
	}
	Started = true;

	const auto state = Ui::CreateChild<State>(qApp);
	state->timer.setCallback([=] { CheckLatestRelease(state); });
	state->timer.callOnce(kStartDelay);
#endif // Q_OS_WIN
}

} // namespace Core::LoogriGram
