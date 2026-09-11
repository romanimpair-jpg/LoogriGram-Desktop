/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct PeerSearchResult {
    QString query;
    std::vector<not_null<PeerData*>> my;
    std::vector<not_null<PeerData*>> peers;
};

class PeerSearch final {
public:
    explicit PeerSearch(not_null<Main::Session*> session);
    ~PeerSearch();

    enum class RequestType {
        CacheOnly,
        CacheOrRemote,
    };
    void request(
        const QString &query,
        Fn<void(PeerSearchResult)> callback,
        RequestType type = RequestType::CacheOrRemote);
    void clear();

private:
    struct CacheEntry {
        PeerSearchResult result;
        bool requested = false;
        bool peersReady = false;
    };

    void requestPeers();

	void finish(PeerSearchResult result);
	void finishPeers(mtpRequestId requestId, PeerSearchResult result);

    const not_null<Main::Session*> _session;

    QString _query;
    Fn<void(PeerSearchResult)> _callback;

	base::flat_map<QString, CacheEntry> _cache;
	base::flat_map<mtpRequestId, QString> _peerRequests;

};

} // namespace Api
