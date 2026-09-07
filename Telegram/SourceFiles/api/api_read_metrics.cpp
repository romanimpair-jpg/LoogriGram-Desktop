/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_read_metrics.h"

#include "apiwrap.h"
#include "data/data_peer.h"

namespace Api {
namespace {

constexpr auto kSendTimeout = crl::time(5000);

} // namespace

ReadMetrics::ReadMetrics(not_null<ApiWrap*> api)
: _api(&api->instance())
, _timer([=] { send(); }) {
}

void ReadMetrics::add(
		not_null<PeerData*> peer,
		FinalizedReadMetric metric) {
	// LoogriGram: reading telemetry is never collected. Each metric carries
	// per message dwell time, active dwell time, the fraction of the message
	// on screen and how far through it was scrolled - engagement analytics,
	// not anything the client needs to work. Dropping it here rather than in
	// send() leaves no queue to grow and no unreachable code; send() simply
	// iterates a map that is always empty. Deliberately not tied to ghost
	// mode: that setting governs what other users see, while this is off
	// unconditionally.
}

void ReadMetrics::send() {
	for (auto i = _pending.begin(); i != _pending.end();) {
		if (_requests.contains(i->first)) {
			++i;
			continue;
		}

		auto metrics = QVector<MTPInputMessageReadMetric>();
		metrics.reserve(i->second.size());
		for (const auto &m : i->second) {
			metrics.push_back(MTP_inputMessageReadMetric(
				MTP_int(m.msgId.bare),
				MTP_long(m.viewId),
				MTP_int(m.timeInViewMs),
				MTP_int(m.activeTimeInViewMs),
				MTP_int(m.heightToViewportRatioPermille),
				MTP_int(m.seenRangeRatioPermille)));
		}
		const auto peer = i->first;
		const auto finish = [=] {
			_requests.erase(peer);
			if (!_pending.empty() && !_timer.isActive()) {
				_timer.callOnce(kSendTimeout);
			}
		};
		const auto requestId = _api.request(MTPmessages_ReportReadMetrics(
			peer->input(),
			MTP_vector<MTPInputMessageReadMetric>(std::move(metrics))
		)).done([=](const MTPBool &) {
			finish();
		}).fail([=](const MTP::Error &) {
			finish();
		}).send();

		_requests.emplace(peer, requestId);
		i = _pending.erase(i);
	}
}

} // namespace Api
