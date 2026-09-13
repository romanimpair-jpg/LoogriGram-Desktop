# LoogriGram — fork notes and handoff

Personal fork of Telegram Desktop. Three original goals: no ads, no non-essential
telemetry, no auto-updates *from Telegram* (we have our own, pointed at our own
releases) — plus an always-on-by-default "ghost mode". Started
2026-09-07.

Upstream's `AGENTS.md` is still canonical for code style. This file covers only
what is specific to the fork.

---

## Status

| Part | State |
|---|---|
| Desktop features | Implemented, built, installed and in daily use |
| Desktop CI | Green. Dependencies cached (~9s), incremental compile ~46m |
| **Android** | **Not started.** See "Android" below |

Verified working in the installed build: no sponsored messages, no premium
badges or purchase surfaces, ghost mode toggle in the main menu, Last Seen and
read date hidden server-side, no hover reactions, no suggestion popups, no
Telegram help rows.

Installed app lives at `C:\LoogriProjects\LoogriGram\app\LoogriGram.exe`.
**To update: replace only the .exe.** tdesktop keeps its profile *beside the
executable*, so `app\tdata\` holds the session, settings and ghost-mode state and
must stay put. Running a copy of the exe from anywhere else silently creates a
second empty profile and looks like a logout.

---

## Repo layout

- **`dev`** — pristine upstream baseline. Root commit is byte-identical to
  tdesktop `80158983dba09d3bf5d96701f21473d6c34bf5f5` (tree `a01747a`), imported
  as a single commit because a shallow clone cannot be pushed to a fresh remote.
- **`patches`** — all fork work. **Build from this branch** (see cache scoping).
- Every deviation from upstream carries a `LoogriGram:` comment, so
  `grep -rn "LoogriGram:" Telegram/SourceFiles` lists the entire behavioural diff.
- Upstream remote is configured as `upstream`; `origin` is
  `github.com/romanimpair-jpg/LoogriGram-Desktop` (public).
- Upstream's own workflows (Linux, macOS, Snap, Docker, issue bots) are
  **disabled** on purpose — they fired on every push and did nothing useful.

Note: upstream's `AGENTS.md` forbids `Co-Authored-By:` trailers, which conflicts
with some assistant configurations. Ask before adding them.

---

## Building

Manual dispatch only — a push never triggers a build:

```
gh workflow run "Windows." --repo romanimpair-jpg/LoogriGram-Desktop \
  --ref patches -f mode=build -f config=Release
```

`mode`: `validate` (config + secrets only, ~3 min) · `cache` (also builds
dependencies) · `build` (everything, produces the artifact).
`config`: `Release` is what we ship. `Debug` exists but is unused.

Credentials are Actions secrets `TG_API_ID` / `TG_API_HASH` — one api_id serves
both platforms, since my.telegram.org allows only one per phone number.

Timings: dependency restore is ~2-9s off cache. The Telegram compile itself was
**2 hours** measured, for all 2157 objects. A configuration mistake fails in
~10 min.

To cut that 2 hours there is now an **incremental build tree cache**. ninja
decides what to rebuild from mtimes, and a fresh checkout stamps everything with
the checkout time, so a restored `out/` always looked stale. Two pieces fix that:

- **`Normalize source timestamps`** dates each tracked file by the commit that
  last touched it (hence `fetch-depth: 0`), so unchanged files stay older than
  their cached objects. Submodule files get one fixed old timestamp instead —
  `lib_ui` and `lib_base` are submodules and their fresh-checkout headers would
  otherwise rebuild most of the tree. **If a submodule pointer is ever bumped,
  raise `OUT_CACHE_SALT`**, or stale objects will be reused.
- **`out/` is cached per commit**, restored by prefix so a run picks up the
  newest tree, and saved even on failure so a broken compile still leaves its
  objects behind. Old entries are pruned to the newest two, because the tree is
  gigabytes against a 10GB pool shared with the dependency caches — leaving it
  unpruned would evict them, which is exactly how sccache cost a rebuild.

**Measured, and it works.** Compile step only:

| Tree cache | Compile |
|---|---|
| none | 2h 03m |
| cold, populating it | 2h 06m |
| **warm** | **46m** |

That 46 minutes was a change to `core_settings.h` — a widely included header and
therefore the *worst* case, since everything including it must rebuild anyway. A
`.cpp`-only change should do considerably better. The first run after any change
to `prepare.py`, the SDK version or `OUT_CACHE_SALT` pays full price again,
because those are all in the cache key.

Two things to keep in mind rather than rediscover:

- **Bump `OUT_CACHE_SALT` if a submodule pointer changes.** Submodule files are
  pinned to a fixed old mtime, so a genuine submodule update would otherwise be
  invisible to ninja and stale objects would be reused — a silently wrong binary,
  which is worse than a slow build.
- **Do not let the tree cache go unpruned.** It is ~1.5GB per entry against a
  10GB pool shared with the dependency caches. This is not housekeeping: an
  unbounded cache evicts the dependency caches, and that already cost one
  two-hour rebuild when sccache filled the pool.

If more speed is ever needed, the remaining options are a self-hosted runner
(persistent workspace, true incremental, but needs the local MSVC toolchain) or a
paid larger runner.

Fetch the result:

```
gh run download <run-id> --repo romanimpair-jpg/LoogriGram-Desktop
```

---

## CI lessons that cost real time

Each of these burned at least one multi-hour build. Do not relearn them.

1. **GitHub caches are branch-scoped.** A run restores caches from its own branch
   or the default branch (`dev`) only. Our dependency caches were created on
   `patches`, so any side branch pays the full ~2h library build. Build on
   `patches`, or seed the caches on `dev` once.
2. **`cd` does not switch drives in cmd.** `TBUILD` is `C:\b` while steps start on
   the `D:` workspace. Plain `cd` changes the directory *on* C: without switching
   to it, so `configure.bat` is silently not found. Use `cd /d`.
3. **MAX_PATH.** The deepest object paths (kimageformats via CMake's out-of-tree
   `__/__/...` form) are ~205 chars; GitHub's workspace prefix is 57, giving 262 —
   two over the limit, surfacing as `C1083` with an *empty* filename. Fixed by
   building through the short `C:\b` alias (~217). Shortening the repo directory
   alone achieves nothing: CMake swaps a hashed object dir for the longer
   out-of-tree form depending on prefix length, keeping the total pinned near 260.
4. **`actions/cache` paths must stay inside the workspace.** It keys archives to
   workspace-relative paths, so pointing cache paths at `C:\b` misses silently.
   Only the *build* uses the alias; `CACHE_ROOT` and `LibrariesPath` stay on the
   workspace path.
5. **Debug info is all-or-nothing.** Upstream strips `/Zi` from CMake's debug
   flags *and* passes an empty `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`. Doing only
   the strip leaves `/Fd` and `/FS` with no `/Zi`, which fails `C1083` on
   kimageformats. Doing neither leaves `-Z7` in 2157 objects, which the linker
   merges into one oversized PDB and fails `LNK1201`. For Release, blank the
   format: no debug arguments at all, self-consistent, and faster.
6. **kimageformats is the only target that links `libdav1d.a`**, while ffmpeg's
   libavcodec references dav1d regardless. Disabling the Qt plugins therefore
   produces 14 unresolved `dav1d_*` symbols. `libdav1d.a` is now passed to the
   linker explicitly and verified to exist before compiling, so it is settled
   either way.
7. **sccache does not work here.** CMake ignores `CMAKE_<LANG>_COMPILER_LAUNCHER`
   for MSVC — it logged *zero* compile requests across a full build. Removed.
   Caching `out/` instead will not help either: ninja decides by timestamp and
   every run checks out fresh sources.
8. **Save caches explicitly, not in post steps.** `actions/cache`'s post step is
   skipped when any step fails, so a compile error used to discard 80 minutes of
   dependency work. The workflow now uses `cache/restore` plus an explicit
   `cache/save` after `Libraries` succeeds — placed *after* the disk-strip step so
   the cached tree matches upstream's stripped shape.
9. **`skip-release` hides two problems.** Dropping it activates breakpad's
   `dump_syms`, which needs ATL headers the CI toolset lacks (`dump_syms` is
   deleted from `prepare.py` — it only symbolises crash dumps, which are off), and
   it roughly doubles dependency disk use, which exhausted the runner until the
   reclaim step was added.
10. **`ninja -k 0`** is set so one 50-minute run reports every error rather than
    stopping at the first.

---

## Ghost mode

One master toggle, **default on**, in the main menu beside Night Mode
(`window_main_menu.cpp`, `menuIconStealth`). Stored via the KV prefs facility
(`Core::Settings::ghostMode`), not the binary stream — upstream's `AGENTS.md`
recommends this for simple flags and it avoids the append-only ordering trap.

Suppressed: typing/activity broadcasts (group-call *speaking* is exempt), online
presence, story views, delivery receipts. Plus, unconditionally and not tied to
the toggle: reading telemetry (`api_read_metrics` — per-message dwell time and
scroll depth) and post view-count contributions.

Server-side half, applied once per account on first login and on every explicit
enable, never reversed: **Last Seen → Nobody** and **hide read date**
(`hide_read_marks`, free, not Premium). Exception lists are deliberately left
intact rather than cleared.

### Read receipts are deliberately NOT suppressed

This was tried and reverted. `messages.readHistory` both notifies the sender and
sets the read position other devices sync from, and the API has no way to do one
without the other. Suppressing it made everything read on desktop reappear as
unread on the phone. Hiding the read *date* is the part that can be had at no
sync cost. **Do not re-attempt this** — it is architectural, not a bug.

Story views have the same coupling at lower stakes (stories expire in 24h); they
are still suppressed, so story rings may reappear as unread on other devices.

### Traps in the ghost-mode code

- `Updates::updateOnline` also drives `checkAutoLock`, `saveCurrentDraftToCloud`
  and, when quitting, `quitPreventFinished()`. Early-returning breaks passcode
  auto-lock and cloud drafts and **hangs shutdown**. Only the reported value is
  changed.
- Story pending sets feed `checkQuitPreventFinished()`. Queueing work that never
  sends hangs exit; the guard is at queue time.
- `Stories::markAsRead`'s `bumpReadTill` is local state — guarding before it would
  leave every story permanently unread locally.
- Do **not** intercept at the MTProto layer. Request ids are returned to callers
  before the send, so dropping there strands callbacks and deadlocks
  `Histories::sendReadRequest`'s queue.

---

## Other desktop changes

- **Ads**: `SponsoredMessages::canHaveFor` (both overloads) and `isTopBarFor`
  return false. `request()`/`inject()` early-return, so nothing is fetched and the
  view/click beacons never fire.
- **Premium**: two one-line gates in `main/main_session.cpp` do almost all of it.
  `premiumBadgesShown() → false` removes emoji statuses *and* the gold star
  everywhere; `premiumCanBuy() → false` drops the Premium/Stars/TON/Business/Gifts
  block from settings and sends every limit box down upstream's existing
  `!premiumPossible` branch — an explanation with an OK button. Nulling
  `emojiStatusId()` instead does **not** work: it promotes premium users to the
  static star and still reserves badge width.
- **No auto-update from Telegram**: `DESKTOP_APP_DISABLE_AUTOUPDATE=ON` (also
  drops the `Updater.exe` target, so the artifact step must not try to move it).
  Upstream's updater stays off; ours replaces it — see below.

### Our own updater

`core/loogrigram_update.cpp`, started once per launch from `Application::run()`.
It reads **our** GitHub releases, never Telegram, and the request carries
nothing about the account — an unauthenticated GET of a public endpoint.

- **Releases, not artifacts.** Downloading an Actions artifact needs an
  authenticated token even on a public repo, so a shipped binary cannot fetch
  one. Release assets are a plain anonymous HTTPS GET. CI publishes a release
  on every `mode=build` + `config=Release` run.
- **The version is the commit.** CI stamps the short sha into
  `core/loogrigram_build_tag.h` before configuring and tags the release with
  the same value; the updater compares the two as strings. No arithmetic, so it
  cannot drift against upstream's `AppVersion`, and re-running a commit
  replaces its release rather than looking like a new version. Only
  `loogrigram_update.cpp` includes that header, so the stamp costs one object
  per build, not a full rebuild.
- **A running exe can be renamed, just not deleted or overwritten** — the image
  is mapped by file object, not by path. Verified on this machine: rename
  succeeded with the process still live, writing a new file at the freed path
  succeeded, deleting the running image failed with access denied. So the swap
  is: write `.new` beside it, move the running binary to `.previous`, move
  `.new` into place. `Core::Restart()` then relaunches `cExeDir() + cExeName()`
  — the same path, now holding the new build. `JustRelaunch` does not touch
  `Updater.exe`, so it works with upstream's updater disabled.
- **A locally built binary never updates itself.** The committed tag is `"dev"`,
  which matches no release; the updater refuses to run unless the tag looks
  real, otherwise every local build would immediately overwrite itself with CI's.
- **Trust is GitHub over TLS**, deliberately — no signature checking. The
  exposure is that whoever controls the GitHub account can push a binary this
  machine will run. The size and `MZ` checks before swapping are not security;
  they only stop an error page or truncated download being installed as the
  program. If that tradeoff ever stops being acceptable, sign the asset in CI
  and verify before the swap.
- **Branding**: `AppName`/`AppFile` = `LoogriGram` in `core/version.h` — `AppName`
  is what `psAppDataPath()` appends to `%APPDATA%`. Fresh `AppId` GUID so Windows
  registry entries cannot collide. `CompanyName` is `LoogriMedia`, not Telegram
  FZ-LLC. The main menu keeps a "Based on Telegram Desktop" attribution link,
  which the API Terms want visible anyway.
- **Removed UI**: hover quick-reaction strip (right-click reactions kept), the
  Telegram FAQ / Features / Ask a Question rows, the "is this still your number?"
  nag (the 2FA password reminder is kept on purpose — losing that locks you out).
- **No suggestion popups above the message field**: `suggestEmoji()`,
  `suggestStickersByEmoji()` and `suggestAnimatedEmoji()` return false at the
  getter. Forced there rather than by unticking the settings, because unticking
  did not reliably suppress them. Setters and stored fields are kept so the
  settings rows and serialization still work.
- **Auto-download defaults**: photos + GIFs only, across all three categories.
  Voice and Music keep upstream values because the box does not expose them.
- **Notification defaults**: muted chats excluded from the unread badge (but kept
  in folder counters), pinned-message notifications off. The other rows on that
  page — per-chat notify toggles, reactions, contact joined, accept calls — are
  per-account or per-session server-side settings and cannot be defaulted here.

---

## Open: media takes a beat to start loading

Unresolved, and worth pursuing. Opening a channel pauses noticeably before the
**photos** start loading, and clicking a video pauses before it starts
downloading. It is not felt in the official client with the same auto-download
settings. Whether the photo and the video case are one problem or two is not
established.

Capture a debug log with `debugmode` typed on the Settings page (the codes are
fed by `Main::keyPressEvent`, so the search box swallows them — or just launch
with `-debug`). Files land in `app\DebugLogs\`; `mtp_*.txt` timestamps every
request and `log_*.txt` carries the session and connection events.

Measured:

| Trigger | → first `upload_getFile` |
|---|---|
| Channel open, cold media DC | 781 ms |
| Video open, cold media DC | 1092 ms (1183 ms to first byte) |
| Channel open, warm session | **8 ms** |
| Channel open, warm session | **4 ms** |

Two separate costs. Only the second is a lead:

1. **Connection setup on a cold media DC, ~250-800 ms.** The download is
   enqueued within 3 ms of the channel opening; the rest is connect, transport
   probe and auth bind. `DownloadManagerMtproto` drops every session on a media
   DC 15 s after the last outstanding byte (`kKillSessionTimeout`), so this is
   paid constantly — the log shows three full teardown-and-rebuild cycles in
   under two minutes. **This is upstream code and unchanged.** Raising the
   timeout was tried and reverted: if upstream does not have the problem, the
   cause is ours, and tuning an upstream constant only hides it.
2. **834 ms between the viewer appearing and the download being enqueued**, on
   a video open. Client side, and unexplained.

### Leading hypothesis: the views / read-marking path

Channels collect view counts per post, and the same visible-area sweep that
reports them also drives read-marking — and both photos and videos hang off
messages becoming visible. We changed that area, so look here first:

- `ViewsManager::scheduleIncrement` is gutted. Upstream's version also
  maintained `_incremented`, the per-peer set that stops an item being
  scheduled twice; ours populates nothing, so `removeIncremented` now clears an
  always-empty map. Check what else reads that bookkeeping.
- The callers are the visible-area sweeps in `HistoryInner` (~line 1531) and
  `HistoryView::ListWidget` (~line 3183), both guarded by `markingAsViewed &&
  item->hasViews()`. The same loops populate `readContents` and drive
  `readInboxTill`.
- The log shows dozens of repeated `Reading: readInboxTill ... in guard, unread
  0` lines inside the delay window. Read-marking is the one area the fork tried
  to change and reverted (commit `9f12a25`); the diff against `dev` shows no
  residue, but the sheer volume of these calls is worth understanding.
- Also inside the window: `Audio Info: recreating audio device` on every video
  open, with `Closing audio playback device` a second or two after each. Opening
  an OpenAL device on Windows can block the main thread for hundreds of ms.

### The lens that found this

Worth reusing: **look for anything we plugged with a forced failure that a
caller is still waiting on.** Auditing every fork change that way turned up one
real instance — `SponsoredMessages::request` drops its `done` callback, see
below — which was not the cause here, but is exactly the shape to hunt for.
Everything else checked out: the other suppressions all drop work at the queue
stage, where nobody is waiting on a reply.

---

## Finish the removals properly

Nearly everything here was removed by forcing a getter or early-returning from a
sender, which is cheap, safe, and keeps local state coherent because upstream
already ships the "unavailable" branch. It is **not** the intended end state:
the target is a leaner tree with no dead code behind guards. All of this is our
code and upstream's structure is not a boundary, so rewriting callers — even
substantially — is in scope.

The Android fork has the same backlog; see `loogrigram-android/LOOGRIGRAM.md`.

Still sitting at the "forced getter" stage:

- **Ads.** `SponsoredMessages::canHaveFor` (both overloads) and `isTopBarFor`
  return false, and `request()`/`inject()` early-return, leaving `append`,
  `state`, `fillTopBar` and the beacon paths in the tree, inert. **This one has
  an actual defect, not just dead code:** `request()` returns without ever
  calling its `done` callback, which four call sites pass. Upstream's own
  sibling `requestForVideo()` calls `done({})` on the same check — upstream
  could return silently because `canHaveFor` was false only for peers where
  nobody was listening, and forcing it false everywhere broke that. Nothing
  hangs today (three of the four sites also call `checkState()` synchronously,
  and the flag it sets gates only another sponsored request), but it violates
  the fork's own "answer requests, don't drop them" rule. `state()` can now
  never return anything but `None`, so `_sponsoredMessagesStateKnown` is
  permanently false and `HistoryWidget::loadMessagesDown`'s branch on it is
  unreachable.
  Scope, when this is done: 973 occurrences across 76 files. Sponsored **peer
  search** (`api_peer_search.cpp`, `dialogs_inner_widget.cpp`) was never gated
  at all, so sponsored channels in search results are presumably still being
  requested and shown — that is a separate subsystem from the message pipeline
  and a gap in the ad removal, not just leftover code.
- **Premium.** `premiumBadgesShown()` and `premiumCanBuy()` are two lines that
  neutralise the badge painters, the settings block and every limit box. What
  they neutralise is all still compiled.
- **Suggestion popups.** `suggestEmoji()`, `suggestStickersByEmoji()` and
  `suggestAnimatedEmoji()` return false at the getter, with the setters and
  stored fields deliberately kept so the settings rows and serialization still
  work. If the rows go too, the fields can go with them.
- **Ghost-mode suppression sites**, all early returns rather than removals:
  `SendProgressManager::skipRequest`, the `MTPaccount_UpdateStatus` block in
  `Updates::updateOnline`, `ViewsManager::viewsIncrement`,
  `Histories::reportPendingDeliveries`, `RepliesList::sendReadTillRequest` and
  `ReadMetrics::send`.

Two of these cannot simply be deleted and need the caller rewritten instead,
which is the work rather than a reason to stop: `updateOnline` also drives
`checkAutoLock`, `saveCurrentDraftToCloud` and `quitPreventFinished()`, and the
suggestion getters are read by the settings UI.

## Constraints and known limits

- **API ToS §3.3** requires third-party clients to support sponsored messages. Ad
  removal knowingly violates it. Exposure is the registered `api_id`, which
  Telegram can revoke; mitigation is re-registration. Accepted, personal use.
- §2.3/§2.4 bar "Telegram" in the app title and use of its logo — "LoogriGram"
  complies, hence also the publisher-metadata change.
- Sending a message is inherently visible; none of this hides anything from
  Telegram's own servers.
- Last-seen concealment is reciprocal: only vague "recently" for everyone else.
  Hiding the read date likewise costs seeing other people's.
- No AVIF/HEIF/JPEG XL/QOI decoding *if* the Qt plugins ever get disabled again.
- Per-account settings (auto-download, sticker order) live in
  `Main::SessionSettings`, encrypted with the account key — they do **not**
  transfer between installs. App-level settings (`tdata/settingss`) do: the salt is
  in the file and the passcode is empty, so that one file is portable between any
  tdesktop builds.

---

## Android — not started

The original approved plan, including the full Android section, is at
`C:\Users\Loogris\.claude\plans\glittery-watching-nest.md`. Read it first.

Decisions already made: fork **official `DrKLO/Telegram`** (verified official via
telegram.org/apps), not a third-party fork; build on GitHub Actions; fully
Google-free because the phone runs GrapheneOS without sandboxed Play Services;
strip location *sending* entirely while keeping received locations viewable via a
`geo:` intent.

Mapped call sites, all in `TMessagesProj/src/main/java/org/telegram/`:

- **Ads**: `messenger/MessagesController.getSponsoredMessages()` → `return null`.
  `ui/ChatActivity.addSponsoredMessages` then hits its own `res == null` guard, so
  the `viewSponsoredMessage` impression beacon never fires either. ~2 lines.
- **Typing**: the 5-arg `MessagesController.sendTyping()` → `return false`. `false`
  is already a routine return; covers secret chats, which share the method.
- **Presence**: force `MessagesController.ignoreSetOnline = true` (already
  `public volatile`) and neutralise its reset. Control then falls to the
  `offline = true` branch, which latches `offlineSent`.
- **Read receipts**: `MessagesController.completeReadTask`. **Given the desktop
  finding above, do not suppress these.** If ever revisited, keep
  `readEncryptedHistory` regardless — suppressing it breaks secret-chat TTL.
- **Emoji status**: two parallel accessors with *different* null sentinels —
  `DialogObject.getEmojiStatusDocumentId` (returns `0`) and
  `UserObject.getEmojiStatusDocumentId` (returns `null`). Patch both, then kill the
  `isPremiumUser` star fallback in `DialogCell`, `ProfileSearchCell`, `UserCell`
  and `ChatAvatarContainer` — otherwise premium users keep a static star and a
  reserved-width layout gap.
- **Premium upsell**: intercept the single `LimitReachedBottomSheet` constructor
  (58 call sites untouched) and show a plain dialog. **Do not just delete it** —
  `AlertsCreator` uses that sheet as the only surfacing of a server
  `CHANNELS_TOO_MUCH` error, so removing it swallows real failures.
- **Stars/TON**: hide entry points only (`SettingsActivity` MyTON row, two
  `ProfileActivity` presenters). Deleting `ui/Stars/` breaks `MessageObject`, which
  calls `StarsIntroActivity.replaceStars()` during message text rendering, and
  `SendMessagesHelper`, which imports `TONIntroActivity`.
- **Degoogling**: delete `GcmPushListenerService`, keep
  `PushListenerController.processRemoteMessage` (the transport-independent payload
  decryptor), skip `initPushServices()`, and promote `NotificationsService` to a
  foreground service with an `AlarmManager` watchdog. Android 8+ then *requires* a
  permanently visible notification. Remove the `google-services` plugin from both
  modules plus the root classpath, and every gms/firebase/mlkit/vision/wallet/
  safetynet/recaptcha/billing dependency. For billing, force
  `BuildVars.useInvoiceBilling() → true` — the path `isStandaloneBuild()` already
  exercises, which resolves most of the 11 affected files.
- **`BuildVars`**: own `APP_ID`/`APP_HASH`, `CHECK_UPDATES = false` (this is the
  no-auto-update requirement), `SUPPORTS_PASSKEYS = false`.

Android CI requirements: `ubuntu-latest`, JDK 17, Gradle wrapper 8.11.1,
`submodules: recursive` **and** shallow (10 submodules, one from
`chromium.googlesource.com`; a missing one is a *configuration*-time failure), NDK
`27.2.12479018` and CMake `3.22.1` hard-pinned, SDK 36. Cut `abiFilters` to
`arm64-v8a` — the 4-ABI native build is 30-60+ min and the largest single cost.
Task: `:TMessagesProj_App:assembleAfatRelease`. Generate an own signing keystore;
the committed one is a dummy with public passwords. **Back the keystore up** —
Android refuses updates signed with a different key.

Gotcha: the `google-services` plugin validates its JSON's `package_name` against
`applicationId`, so renaming the package *forces* the degoogling edit. Do them in
one commit. Keep `IS_PRIVATE=false` (the `checkVisibility` task throws otherwise)
and keep `buildSrc/`.
