/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_peer_colors.h"
#include "mtproto/sender.h"

class ApiWrap;

namespace Ui {
struct ColorIndicesCompressed;
} // namespace Ui

namespace Api {

class PeerColors final {
public:
	explicit PeerColors(not_null<ApiWrap*> api);
	~PeerColors();

	[[nodiscard]] Ui::ColorIndicesCompressed indicesCurrent() const;
	[[nodiscard]] auto indicesValue() const
		-> rpl::producer<Ui::ColorIndicesCompressed>;

	[[nodiscard]] std::optional<Data::ColorProfileSet> colorProfileFor(
		not_null<PeerData*> peer) const;
	[[nodiscard]] std::optional<Data::ColorProfileSet> colorProfileFor(
		uint8 index) const;

private:
	void request();
	void requestProfile();
	void apply(const MTPDhelp_peerColors &data);
	void applyProfile(const MTPDhelp_peerColors &data);

	MTP::Sender _api;
	int32 _hash = 0;
	int32 _profileHash = 0;

	mtpRequestId _requestId = 0;
	mtpRequestId _profileRequestId = 0;
	base::Timer _timer;
	rpl::event_stream<> _colorIndicesChanged;
	std::unique_ptr<Ui::ColorIndicesCompressed> _colorIndicesCurrent;
	base::flat_map<uint8, Data::ColorProfileData> _profileColors;

};

} // namespace Api
