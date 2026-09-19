# LoogriGram Desktop

A personal, privacy-minded build of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) for Windows. It has no ads, no Premium, no Stars or other paid features, no stories, and no non-essential telemetry, and ghost mode is on by default.

**Not affiliated with Telegram.** LoogriGram is an unofficial third-party client built on the official Telegram Desktop source and the public Telegram API. It talks to Telegram's servers exactly as the official client does, so nothing here hides your activity from Telegram itself.

It's built for one person's daily use and published because the license requires the source. There are no support or feature promises.

## What's different

### Ghost mode (on by default)

One switch in the main menu, next to Night Mode.

- No "typing…" or other activity indicators are sent. Speaking in a group call still shows.
- You always appear offline.
- It sets **Last Seen** to *Nobody* and turns on **hide read time** on the server, once when an account first logs in and again each time you switch ghost mode on. Turning ghost mode off doesn't change them back, and your exception lists are left as they are.

**Read receipts are still sent.** Telegram uses the same request both to tell the sender you've read a message and to sync your read position to your other devices, so blocking it made everything read on the desktop show up unread on the phone. Hiding the read *time* is the part that can be had without breaking sync.

### Removed

- **Ads.** No sponsored messages in channels, in the media viewer, or in search results.
- **Telemetry that isn't needed to use Telegram.** Per-message reading time and scroll depth, and view-count reporting from channels.
- **Everything to do with money, in either direction.** Premium subscriptions and upsells, Stars, TON, gifts, giveaways, paid media, paid posts, paid reactions, paid messages, boosts, Telegram Business and channel earnings. Messages that carry a gift, a giveaway, a payment or a price are hidden rather than shown. Chats with people who charge per message are locked, with a note explaining why.
- **Premium is shown for nobody.** Everyone looks the same: no Premium badges or emoji statuses on anyone, and no Premium-only tools in the interface. Limits that Telegram's servers enforce on free accounts still apply.
- **Stories**, entirely: the strip, profile rings and tabs, the viewer, statistics and data export. Messages that carry a story are hidden. Replies to a story keep their text.
- **AI compose**, **large animated emoji**, the **greeting sticker** in empty chats, and **emoji and sticker suggestion popups** above the message field.
- **Bots can't set your emoji status.** The permission is revoked if a bot already had it.
- **Nags and help links:** the quick-reaction strip on hover (reactions are still on right-click), the FAQ / Features / Ask a Question rows, and the "is this still your number?" prompt. The two-step verification password reminder is kept, because forgetting that password locks you out.
- **Telegram's updater and crash-report uploads.** Updates come from this repository instead (see below).
- **macOS and Linux builds.** This tree builds for Windows only.

### Changed defaults

- Media auto-download: photos and GIFs only.
- Muted chats don't count toward the unread badge (folder counters still include them).
- Notifications for pinned messages are off.

### Kept on purpose

- Read receipts (see above).
- Delivery confirmations for login codes sent through Telegram Gateway. Without them, services tend to resend the code by SMS.
- Verified, scam and fake badges. They're warnings, not purchases.
- The proxy sponsor channel label, because it discloses who runs the proxy.

## Installing and updating

1. Download `LoogriGram.exe.gz` from the [latest release](https://github.com/romanimpair-jpg/LoogriGram-Desktop/releases/latest) and unpack it (7-Zip or any gzip tool).
2. Put `LoogriGram.exe` in a folder of its own and run it.

Your session and settings are stored in a `tdata` folder next to the executable, so **always run it from the same folder**. A copy started from somewhere else starts a fresh, logged-out profile. To update by hand, replace only the `.exe`.

The app checks this repository's releases once per launch and offers new builds from the main menu. The check is an anonymous request to GitHub and carries nothing about your account. Releases are verified only by GitHub's HTTPS, not by a signature.

## Building

Builds run on GitHub Actions and are started by hand. See [`.github/workflows/win.yml`](.github/workflows/win.yml). The fork's work lives on the `patches` branch, and `dev` holds the upstream baseline it was forked from. [`LOOGRIGRAM.md`](LOOGRIGRAM.md) on `patches` holds the maintainer notes: build details, pitfalls, and the reasons behind each change.

Every change from upstream in the source carries a `LoogriGram:` comment, so

```
grep -rn "LoogriGram:" Telegram/SourceFiles
```

lists the complete behavioural difference from Telegram Desktop.

## License

GPLv3 with the OpenSSL exception, the same as Telegram Desktop. See [LICENSE](LICENSE) and [LEGAL](LEGAL).
