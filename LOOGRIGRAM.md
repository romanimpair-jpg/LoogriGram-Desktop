# LoogriGram Desktop — maintainer notes

Personal fork of Telegram Desktop for Windows, started 2026-09-07. No ads, no
non-essential telemetry, no money features in either direction, no Premium, no
stories, our own updater, and a ghost mode that is on by default.
`README.md` is the public summary; this file is for whoever works on the tree
next.

Upstream's `AGENTS.md` is still canonical for code style. Where it and this
file disagree, this file wins on anything fork-specific.

---

## Status (2026-09-20)

**Finished and conserved.** Every planned removal is done, the tree builds
green, and the installed app is `g7b24c5a` (run `35459450326`), in daily use.
Work has moved to Android; see `loogrigram-android/LOOGRIGRAM.md`.

| | |
|---|---|
| Diff against `dev` | 2,150 files, +7,913 / −279,303 lines |
| Behavioural diff | 481 `LoogriGram:` comments; `grep -rn "LoogriGram:"` lists them |
| Platforms | Windows x64 only; macOS and Linux code deleted |
| Qt | **Qt 5**, as the official client ships (see below — this matters) |
| Updates | Ours, from this repository's releases |

Open items are in "Open and known" below. None of them block anything.

---

## Settled rules — do not re-litigate

These were decided with the user and cost real discussion. Ask before
revisiting any of them.

- **No money operations at all, in either direction.** Not paying, not being
  paid: Premium, Stars, TON, gifts, giveaways, paid media, paid posts, paid
  reactions, paid messages, boosts, Business, earnings. Delete, don't guard.
  Don't ask per surface, and don't stop at the paying half.
- **Premium is honoured for nobody** — ours or anyone else's. Everyone looks
  the same, and Premium-locked tools are not drawn at all (no padlocks).
  Channel boost levels are treated the same way. Two exceptions the user
  chose: the notice for someone who accepts messages only from Premium users
  keeps its wording, and chat folders past the free limit stay drawn dimmed,
  opening the plain limit box.
- **Delete removed features; don't guard them.** No forced getters, no
  `if (true) return;`, no inert stubs. Remove the code, its call sites, its
  resources and its dependency. Rewriting upstream code deeply is in scope.
- **Hidden, not refused.** Inbound money and story messages are still
  created and live in history, just empty and invisible (see "Hidden content").
- **Server state is round-tripped, not changed**, where we merely stopped
  showing a setting: admin story rights are kept on save; `stories_muted` and
  the story reaction notify setting are read and sent back unchanged. The one
  deliberate exception is revoking a bot's emoji-status permission.
- **Read receipts are sent.** Architectural; see Ghost mode. Do not
  re-attempt suppressing them.
- **Delivery reports are sent.** Only Gateway login codes use them, and
  blocking them makes services resend by SMS.
- **Describe UI by position and function, not by `lang.strings` text.** The
  cloud language pack renames many labels ("Invites", not "Groups &
  channels"), so the source text is often not what is on screen.

---

## What changed

### Ghost mode

One master toggle, **default on**, in the main menu beside Night Mode
(`window_main_menu.cpp`, `menuIconStealth`). Stored via the KV prefs facility
(`Core::Settings::ghostMode`), not the binary stream — upstream's `AGENTS.md`
recommends this for simple flags and it avoids the append-only ordering trap.

Suppressed while on: typing/activity broadcasts (group-call *speaking* is
exempt, `SendProgressManager::skipRequest`) and online presence
(`Updates::updateOnline` reports offline). Deleted outright, not tied to the
toggle: reading telemetry (per-message dwell time and scroll depth) and post
view-count contributions — `ReadMetrics`, `ViewsManager` and the list-side
tracker are gone. Story views went with stories.

Server-side half, `Session::applyGhostModePrivacy`: **Last Seen → Nobody** and
**hide read date** (`hide_read_marks`, free, not Premium). Applied once per
account on first login (a KV pref remembers it, so changing Last Seen by hand
later is not undone on the next start) and again on every explicit enable.
Never reversed. Exception lists are deliberately left intact.

**Delivery reports are sent, deliberately.** Ordinary chats have no delivery
receipt (one tick means the server has it). The only report the client sends
is `messages.reportMessagesDelivery`, for messages carrying
`report_delivery_until_date` — login codes sent through Telegram Gateway — and
it tells the sending service the code arrived. Kept by choice (2026-09-19).

#### Read receipts are deliberately NOT suppressed

Tried and reverted. `messages.readHistory` both notifies the sender and sets
the read position other devices sync from, and the API has no way to do one
without the other. Suppressing it made everything read on desktop reappear as
unread on the phone. Hiding the read *date* is the part that can be had at no
sync cost. **Do not re-attempt this** — it is architectural, not a bug.

#### Traps in the ghost-mode code

- `Updates::updateOnline` also drives `checkAutoLock`, `saveCurrentDraftToCloud`
  and, when quitting, `quitPreventFinished()`. Early-returning breaks passcode
  auto-lock and cloud drafts and **hangs shutdown**. Only the reported value is
  changed.
- Do **not** intercept at the MTProto layer. Request ids are returned to callers
  before the send, so dropping there strands callbacks and deadlocks
  `Histories::sendReadRequest`'s queue.

### Removed

All deleted, not gated. Roughly in the order they were done.

- **Ads** (~4,500 lines): the message pipeline, the viewer's playback ads,
  `Data::SponsoredMessages`, `MessageFlag::Sponsored`, the fake-webpage
  `HistoryItem` constructor ads were built on, and sponsored **peer search**,
  which had never been gated at all.
- **AI compose** (~8,000 lines): rewrite, translate, tone presets, the
  article-editor pill, the caption button, the shortcut, the experimental
  toggle, the `addstyle/` deep link, and later the article editor itself.
  Extracted rather than deleted, because they were not AI: the paste-as-file
  helpers (`ui/controls/compose_text_helpers.cpp`), `ComposeTooltipManager`
  (`history_view_compose_tooltip.cpp`, used by the send-as-file tooltip) and
  the button style now called `historyComposeInnerButton`.
- **Large emoji**, with `EmojiImageLoader::prepare` and
  `Ui::Emoji::SourceImages`. `st::largeEmojiSize`/`Outline` survive:
  `data_custom_emoji.cpp` sizes `SizeTag::Isolated` from them.
- **Premium, entirely.** `premium()`, `premiumPossible()`,
  `premiumCanBuy()`, `premiumBadgesShown()` and their producers are gone; the
  account is treated as never premium, every caller took its non-premium
  branch, and anything only a premium account could reach was deleted. The
  upsell entry points (`Settings::ShowPremium` and friends), the premium 3D
  renderers (with `Resources/art/premium/` and its `.obj` → `.binobj` build
  step), the subscription and Stars/TON settings. Premium stickers, the
  Premium sticker category and locked message effects are not drawn. Limit boxes still explain the server's limits; only the link that
  sold a way past them went. Where the text was nothing but a pitch, the
  surface went.
- **Premium badges and emoji statuses, for everyone.** Not one painter but
  two, plus a slot in the message bubble that belonged to neither. The
  machinery that only kept an animated status moving went too
  (`PeerListRow::_statusIconRect`, `updateRowStatus`, the badge rect in every
  `CachedRow`), and statuses no longer tint profile headers or call panels.
  Bots asking to set one are refused with `USER_DECLINED`; a bot already
  holding the permission has it **revoked** when its full info arrives, and
  mini apps asking for it are answered "cancelled". Left on purpose: the
  verified check and scam / fake / direct badges (warnings, not purchases),
  and `EmojiStatusPanel`, which is also the custom emoji picker for topic
  icons.
  **Lesson: a gate in front of two painters is not a removal, and a third
  painter can have its own slot.**
- **Money, in both directions.** Stars, TON, gifts (sending, receiving,
  collections, resale, auctions), giveaways, paid media, paid posts, paid
  reactions (`Data::Parse` drops `paid_reactions_available`; display drops any
  paid reaction a message arrives with), paid messages (the price setting,
  and see "Paid messages" below), invoices and payments, suggested posts with
  a price, affiliate programs, channel earnings and ad revenue, `Ui::StarsRating`.
- **Boosting**, both sides: applying a boost (it spends a Premium slot), the
  boost box, the Boosts page, `AskBoostBox`, `?boost` links (they open the
  channel) and every level-locked admin option — auto-translate, the
  appearance box (colours, background emoji, profile colour and emoji,
  emoji status, wallpaper), the group emoji pack chooser, and channel custom
  reactions (channels get the standard ones). Kept: free voice transcription
  in boosted groups, because it is not an admin option.
- **Telegram Business**, the parts that are ours to set: quick replies, away
  and greeting messages, chat links, chatbots and the business bot bar, and
  Business sponsored-message settings. `data_business_common` / `info` stay:
  they carry *other people's* opening hours, location and chat intro.
- **Stories** (2026-09-19), in stages: the chat list strip, the
  `info/stories` section and profile tab, the viewer (`media/stories/`, its
  mode in the media viewer, the renderers' story paths), story state on peers
  and lists, the story parts of statistics, the data layer (`data_stories`,
  `data_story`, `data_stories_ids`), the export step and story reporting.
  `updateStory` / `updateReadStories` are ignored. A reply to a story keeps
  its text and loses the quote; story links open the peer; story link
  previews are plain articles. The story message-id range in `data_msg_id.h`
  is kept as a gap so the special ids after it keep their values. Saved export
  settings drop the old Stories bit (0x800) on read, or `validate()` would
  reset them.
- **Suggestion popups**: the emoji suggestion controller, the emoji panel's
  `:shortcode:` tooltip and the sticker half of field autocomplete (a lone
  emoji offered stickers even with the setting forced off), with their three
  settings; their slots in the stored settings streams are read into nothing.
- **Greeting stickers**: an empty chat offered a random sticker to send. It
  now shows the plain "No messages here yet" line. A chat intro the peer set
  up themselves still shows, with their own sticker.
- **The bot verification icon** before names (the verifier's text stays).
- **Nags and help**: the hover quick-reaction strip (right-click reactions
  kept), the FAQ / Features / Ask a Question rows, the "is this still your
  number?" prompt. The 2FA password reminder is kept — losing that password
  locks you out.
- **Telegram's updater** (`DESKTOP_APP_DISABLE_AUTOUPDATE=ON`, which also
  drops the `Updater.exe` target) and **crash-report uploads**
  (`DESKTOP_APP_DISABLE_CRASH_REPORTS=ON`; Windows still writes a local dump).
- **macOS and Linux** (272 files), and upstream's other workflows.

Left on purpose, being different features: the **promoted / proxy-sponsor
channel** (`Data::PromoSuggestions`, `help.getPromoData`,
`History::isPromoted`, and the `lng_proxy_sponsor_warning` label, which is
honest disclosure and also carries PSAs), and the **reader side of
auto-translation** (a channel that has it on still shows the translate bar).

### Paid messages

A user who charges Stars per message is **locked**, like one who only accepts
Premium senders: no compose field, a padlock in the empty chat, padlocked
rows in share and recipient pickers, and one line from
`LoogriGram::Lang::PaidMessagesLocked`. Tracked as
`UserDataFlag::HasRequirePaymentToWrite` (the hint in the user object) and
`RequiresPaymentToWrite` (the answer for us, from full info or
`users.getRequirementsToContact`). A payment-required error from the server
locks them too (`Api::LockPaymentRequired`). Messages *they* sent us say
nothing about what they charge us, so unlike the Premium case the history
heuristic does not clear it.

### Hidden content

Gifts, giveaways, invoices, paid media, payments, prizes, priced suggested
posts, boost notices and story-carrying messages are hidden
(`core/loogrigram_hidden_content.*`).

**The item is still created and still lives in history — but empty.** The
server tracks what we have read by message id, and the read position only
advances past messages we have. Drop one and nothing ever marks it read, so
the chat keeps an unread badge that scrolling cannot clear — the same
coupling that made read-receipt suppression get reverted.

So the **TL type is checked before anything inside the message is parsed**
(`MoneyMedia`, `MoneyAction`, `MoneyMessage`, `StoryMedia`). A hidden message
becomes a bare service item: id, date, sender and `MessageFlag::ContentHidden`.
No media, text, price or images, and no view is built for it. Three places
ask `LoogriGram::HiddenContent()`, and all three are needed:

- `Element::isHidden()` — upstream's album machinery already collapses a
  hidden element to nothing: no gap, no placeholder.
- `History::computeChatListMessageFromLast` — otherwise the chat list row
  advertises a message that is not in the chat. It walks back to the newest
  one we display, so the chat does not even rise to the top.
- `System::skipNotification` — no toast for a message that is not there.

The birthday suggestion is hidden too: it is not money, but it used the gift
card to display itself. A chat theme change is shown unless the theme is a
collectible gift. **Known and accepted:** paid posts in channels vanish
without trace.

### Changed defaults

- Auto-download: photos and GIFs only, across all three categories. Voice
  and Music keep upstream values because the box does not expose them.
- Muted chats excluded from the unread badge (kept in folder counters);
  pinned-message notifications off. The other rows on that page are
  per-account or per-session server settings and cannot be defaulted here.

### Our own updater

`core/loogrigram_update.cpp`, started once per launch from `Application::run()`.
It publishes its state — None, Checking, Downloading, Ready, plus progress —
and the main menu row follows it: the label changes and a ring fills around
the icon. Ready is terminal and the row then offers the restart. The asset's
size can be unknown until the redirect resolves, so the label drops the
percentage rather than claiming 0%. It reads **our** GitHub releases, never
Telegram, with an anonymous GET that carries nothing about the account.

- **Releases, not artifacts.** Downloading an Actions artifact needs an
  authenticated token even on a public repo. Release assets are a plain
  anonymous HTTPS GET. CI publishes one on every `mode=build` +
  `config=Release` run that succeeds.
- **The version is the commit.** CI stamps the short sha into
  `core/loogrigram_build_tag.h` and tags the release with the same value; the
  updater compares the two as strings. Only `loogrigram_update.cpp` includes
  that header, so the stamp costs one object per build.
- **A running exe can be renamed, but not deleted or overwritten.** Measured:
  rename succeeded with the process live, writing a new file at the freed path
  succeeded, deleting the running image failed with access denied. So the swap
  writes `.new`, moves the running binary to `.previous`, and moves `.new` into
  place.
- **Use `Core::RestartAfterUpdate()`, not `Core::Restart()`.** `Restart()`
  sets `RestartingToSettings` (the new instance opens Settings) and inherits
  `-startintray` / `-autostart`, either of which hides the new window.
  `RestartAfterUpdate` sets `RestartingAfterUpdate`, and `launcher_win.cpp`
  drops both arguments when it sees it. Don't clear the stored
  `StartMinimized` instead: that overwrites the user's choice.
- **A locally built binary never updates itself.** The committed tag is
  `"dev"`, which matches no release, and the updater refuses to run unless the
  tag looks real.
- **Testing the updater needs two builds and a commit held back**: the fix
  lives in the build *doing* the restarting. Install a baseline by hand, then
  push a visibly different commit and let it update.
- **The asset is gzipped**: 220MB raw, 71MB gzip, 57MB xz. gzip wins because
  zlib already decodes gzip streams in the tree, so it cost about thirty lines.
- **Why not update only our code, not Qt?** Static linking: the linker keeps
  only referenced objects (`/OPT:REF`) and lays them out among ours, so
  identical Qt code lands at different offsets whenever our code changes.
  Splitting it means dynamic linking of every dependency plus Telegram's
  packed-archive `Updater.exe` design, reinvented. The `Report size
  composition.` CI step ranks the static libs if anyone wants to price it.
  The bulk is `tg_owt` (WebRTC, 275MB archive), not Qt.
- **Deltas** were considered and not done: executables delta poorly because
  relinking shifts every address, and a corrupt patch still passes the `MZ`
  check.
- **Trust is GitHub over TLS**, deliberately — no signature check. Whoever
  controls the GitHub account can push a binary this machine will run. The
  size and `MZ` checks only stop an error page being installed as the program.

### Branding

`AppName`/`AppFile` = `LoogriGram` in `core/version.h` (`AppName` is what
`psAppDataPath()` appends to `%APPDATA%`). Fresh `AppId` GUID, so registry
entries cannot collide. `CompanyName` is `LoogriMedia`. The main menu keeps a
"Based on Telegram Desktop" attribution link, which the API Terms want anyway.
The Windows Startup shortcut reads "LoogriGram autorun link".

Four files carry the artwork, all ours:

| File | Used for |
|---|---|
| `Resources/art/icon256.ico` | exe, taskbar, Alt-Tab — via `Telegram.rc` |
| `Resources/art/logo_256.png` | tray and window icon — `Window::Logo()` |
| `Resources/art/logo_256_no_margin.png` | small tray sizes, **and the Saved Messages avatar** |
| `Resources/icons/tray_monochrome.svg` | dark-mode monochrome tray — the only vector |

The `.ico` is hand-packed by a stdlib-only script (Pillow is not installed):
16/20/24/32/48/64 as 32bpp BMP and 128/256 as PNG, downscaled by
area-averaging on premultiplied alpha. Upstream shipped only 16/24/32/48/256,
which is why it softened in Alt-Tab. Our artwork and upstream's originals are
in `C:\LoogriProjects\LoogriGram\branding\`, outside the checkout. The
official portable client, for comparing behaviour, is in
`C:\LoogriProjects\TelegramOfficial\`.

If the taskbar or Start Menu shows upstream's plane, it is the Windows shell
icon cache, not the binary: the exe has exactly one icon group, byte-identical
to ours. Clearing Explorer's cache fixed the taskbar; the Start Menu keeps its
own.

### Our strings

Our strings live in `core/loogrigram_lang.cpp` as accessors (`GhostMode()`,
the updater row, `TranscribeTrialsOver`, `PaidMessagesLocked`), never as
`lang.strings` keys — see "Never add or remove keys in `lang.strings`".

---

## Open and known

- **Media takes a beat to start loading.** Opening a channel pauses before
  photos load, and a video pauses before it downloads; not felt in the
  official client. Measured with `-debug` (logs land in `app\DebugLogs\`):
  a cold media DC costs 250-800 ms of connection setup, which is upstream code
  and unchanged — `DownloadManagerMtproto` drops media DC sessions 15 s after
  the last byte (`kKillSessionTimeout`). Raising it was tried and reverted:
  if upstream doesn't have the problem, the cause is ours. Separately, **834 ms
  between the viewer appearing and the download being enqueued** on a video
  open, client side and unexplained. Leads: the visible-area sweeps in
  `HistoryInner` and `HistoryView::ListWidget` that used to feed view counts
  (now deleted) and still drive read-marking — the log shows dozens of
  `Reading: readInboxTill ... in guard, unread 0` lines in the delay window —
  and `Audio Info: recreating audio device` on every video open, which can
  block the main thread. **Compare against the official client before
  theorising.**
- **Two files still named for boosts** hold only shared helpers:
  `boxes/peers/replace_boost_box.*` (userpic rows used by calls and ownership
  transfer) and `ui/boxes/boost_box.*` (`Ui::StartFireworks`). Renaming them
  is cosmetic.
- **MicroTeX warnings on full builds.** `D9025 overriding /W4 with /W0` for
  every MicroTeX source, and C5038 / C4265 from its headers via
  `iv_markdown_microtex.cpp`. Upstream's too; only a full rebuild shows them.
  A real fix means forking `desktop-app/MicroTeX`.
- **Rebasing onto upstream will conflict widely.** Removing macOS and Linux
  touched 272 files, the money and story removals far more. Take ours for
  anything deleted, and expect to re-resolve `#ifdef` chains by hand.

---

## Building

Manual dispatch only — a push never triggers a build. Build from `patches`:

```
gh workflow run "Windows." --repo romanimpair-jpg/LoogriGram-Desktop \
  --ref patches -f mode=build -f config=Release
```

`mode`: `validate` (config + secrets only, ~3 min) · `cache` (also builds
dependencies) · `build` (everything, publishes a release). `config`:
`Release` is what we ship; `Debug` exists but is unused. Credentials are
Actions secrets `TG_API_ID` / `TG_API_HASH`; one api_id serves both
platforms, since my.telegram.org allows only one per phone number.

**A `build` + `Release` run publishes a release**, and the installed app
offers it as an update on its next launch. That is why every dispatch is
asked about first. A failed build publishes nothing.

The workflow file must also *exist* on `dev` for `workflow_dispatch` to work,
but a run uses the file from `--ref`, so CI changes go only to `patches`.
`dev`'s copy of `win.yml` has been hundreds of lines behind for a long time.

To install a build by hand, **replace only `app\LoogriGram.exe`**. tdesktop
keeps its profile beside the executable, so `app\tdata\` holds the session,
settings and ghost-mode state. A copy run from anywhere else silently creates
a second, empty profile, which looks exactly like being logged out.

### The incremental build tree

A full compile is ~2 hours for 2157 objects; with a warm tree cache a
change to a widely included header took 46 minutes, and the full rebuilds
after a cache reset ran 75-95.

ninja decides what to rebuild from mtimes, and a fresh checkout stamps every
file now, so a restored `out/` always looks stale. **`out/` records the commit
it was last built from** in `.loogrigram-build-base`, written only after a
compile that succeeded, and "Age sources against the cached build tree" diffs
the checkout against it: changed files are dated now, everything else
2000-01-01. No marker means dating everything now. A submodule bump shows up
as that path in the diff, so it needs no salt bump.

**Only a successful build's tree is saved** (2026-09-19). Saving after a
failure, so the next run could reuse its objects, didn't pay: fix commits
touch widely included headers, so the next run took 59-74 minutes anyway,
while the 8-18 minute upload (once 56) held back the log needed to fix the
failure. Old trees are pruned to the newest two: ~1.5GB each against a 10GB
pool shared with the dependency caches, and an unbounded cache evicts those,
which already cost one two-hour rebuild.

The cache key includes `prepare.py`, the SDK version and **`OUT_CACHE_SALT`**;
raising the salt throws every tree away. Raise it when a change can't be
seen by the diff — adding or removing `lang.strings` keys is the known case.

#### Two bugs from one cause: source that never reached the binary

`g2533f37` shipped upstream's artwork *and* went on painting the emoji status
beside message authors, from a tree where both had been fixed for hours. The
cause was the incremental build, not the code.

A rebase on 2026-09-14 stamped three commits `13:34:59Z`. The run that built
the commit *before* them finished at `14:12Z` and cached its tree. The next
run dated the three commits' files by git — and every one looked older than
objects built from the previous content, so ninja reused them.

**A git date and an object's mtime are different clocks measuring different
things**, and a commit's date can precede a build that did not contain it.
That is why the ageing step compares commits, not dates. **If a binary ever
disagrees with the source again, suspect this before the code.** Searching the
exe for an asset's bytes settles the artwork half in a minute.

### Qt 5, not Qt 6 — the white flash was the build

Three sessions went into a white flash in the media viewer on the assumption
it was our drawing. **We were building Qt 6 and the official client ships
Qt 5**, and the two use different renderers:

```
Qt 6  ->  no ANGLE  ->  QRhi/D3D11  ->  media_view_overlay_rhi.cpp   -> flashes
Qt 5  ->  ANGLE     ->  OpenGL      ->  media_view_overlay_opengl.cpp -> does not
```

`DESKTOP_APP_USE_ANGLE` is defined only for `QT_VERSION < 6`, so the Qt
version silently picks the viewer implementation. Upstream builds x64 twice,
and the Qt 5 variant is what ships; our trimmed matrix had kept the wrong one.

**The official client is a fact you can check in ten minutes:** run the
portable build and read its `log.txt` (`Renderer: [OpenGL] (Window)`, `Using
DirectX compiler`). All three earlier sessions theorised instead.

### CI lessons that cost real time

Each of these burned at least one multi-hour build.

1. **GitHub caches are branch-scoped.** A run restores caches from its own
   branch or the default branch (`dev`) only. The dependency caches live on
   `patches`, so a side branch pays the full ~2h library build.
2. **`cd` does not switch drives in cmd.** `TBUILD` is `C:\b` while steps start
   on the `D:` workspace, so `configure.bat` is silently not found. Use `cd /d`.
3. **MAX_PATH.** The deepest object paths plus the workspace prefix came to 262,
   surfacing as `C1083` with an *empty* filename. Fixed by building through the
   short `C:\b` alias. Shortening the repo directory alone achieves nothing:
   CMake swaps in a longer out-of-tree form depending on prefix length.
4. **`actions/cache` paths must stay inside the workspace**, or the cache misses
   silently. Only the build uses the alias.
5. **Debug info is all-or-nothing.** Strip `/Zi` *and* pass an empty
   `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, or it fails `C1083` or `LNK1201`.
   The two targets holding our own code set it back per target, which is what
   makes crash dumps readable; the PDB is moved out of `out/` before saving.
6. **kimageformats is the only target that links `libdav1d.a`**, while ffmpeg
   references dav1d regardless, so `libdav1d.a` is passed to the linker
   explicitly and checked to exist before compiling.
7. **sccache does not work here**: CMake ignores the compiler launcher for
   MSVC. Removed.
8. **Save caches explicitly, not in post steps.** `actions/cache`'s post step is
   skipped when any step fails, which once discarded 80 minutes of dependency
   work.
9. **`skip-release` hides two problems**: breakpad's `dump_syms` needs ATL
   headers the CI toolset lacks (deleted from `prepare.py`), and it doubles
   dependency disk use (hence the reclaim step).
10. **`ninja -k 0`** is set so one run reports every compile error rather than
    stopping at the first. The linker only runs once everything compiles, so
    expect link errors to surface one build later.

### Debugging, and how not to waste builds

- **A failed `Expects`/`Assert` writes its message, file and line to `log.txt`
  before it dies**, via a deliberate null write — so it appears as an access
  violation `0xc0000005`. Don't read that as memory corruption.
- **`log.txt` is overwritten on every start.** Reproduce, then read it before
  relaunching.
- **The Windows Application event log** records both the crash time and the
  process start time.
- **Reading a dump.** Every `mode=build` run uploads a private artifact,
  `LoogriGram x64 Release symbols <tag>`, with the PDB for exactly that exe.
  Windows writes full dumps to `%LOCALAPPDATA%\CrashDumps`; open one with
  `cdb -z <dump> -y <dir>`, `!analyze -v`, `kb`. Builds up to `bd75c09660`
  have no PDB.
- **Don't dispatch a build to test what logging could settle.** Debug logs:
  launch with `-debug` (or type `debugmode` on the Settings page, outside the
  search box); they land in `app\DebugLogs\`.
- **Compare against upstream and the official client before theorising**,
  and against the full fork diff.

---

## Traps in the tree

### Editing `lang.strings` alone changes nothing at runtime

`lang.strings` supplies only the compiled defaults. At startup the cached
cloud pack — about eleven thousand keys of Telegram's English — is replayed
over them and `Lang::Instance::applyValue` overwrites every value it carries.
So renaming the app in `lang.strings` alone did nothing; the tray still said
"Quit Telegram". `LoogriGram::Lang::KeepCompiledString` lists the keys whose
cloud value is ignored. **Only strings that name this program belong there**;
references to Telegram the service stay as they are.

### Never add or remove keys in `lang.strings`

It feeds a code generator whose key indices are **positional**, so inserting
one string renumbers every key after it. The incremental tree did not cope:
adding one key rebuilt five objects, not `lang_instance.cpp`, which sizes the
value array — and the binary asserted `"key < _values.size()"` on opening the
main menu. Changing a string's *text* is safe. Our own strings go in
`core/loogrigram_lang.cpp`. If a key must be added or removed — deleting a
feature removes its strings — **raise `OUT_CACHE_SALT` in the same commit**
(done for the five story keys, salt 5).

The wider hazard is unresolved: that build showed a generated-header change
not propagating to its dependents at all.

### Deleting a file: four lists, not one

Five builds in a row died before compiling a single object, each because
something still named a deleted file. Check all four:

| Where | How it names the file |
|---|---|
| `Telegram/CMakeLists.txt` | path relative to `SourceFiles` |
| `Telegram/cmake/*.cmake` | **a separate list** — `td_ui.cmake` holds the UI sources |
| `*.qrc` | path relative to the qrc |
| `*.style` | **bare icon name**, no extension, and **no platform guard** |

The style codegen parses every `.style` file on every platform, so deleting a
macOS-only icon breaks the *Windows* build; the fix is to delete the style
entry, not restore the asset. `generate_models.cmake` bakes every `*.obj`
under `Resources/art` into `.binobj`, so a `.obj` with no apparent reader is
a build input. `loogrigram-tools/check_lists.py` covers the first three and
the icon names.

### Scripted edits are the biggest source of self-inflicted damage

- **`sed '/marker/,+3d'` assumes it knows how long a block is.** One cut a
  composed icon in half; another took `setScreenIsLocked` with the function
  beside it; a third stranded `: nullptr)` in an initialiser list. Prefer
  exact-text edits.
- **Never cut a range between two markers without checking what is inside.**
  Slicing "to the next function" took nine unrelated methods once, because the
  function assumed to follow sat 370 lines further down. It compiled; only the
  linker caught it, after a 1h47m build. Anchor the end on the *next
  function's signature*, never on `}` or `};`, and include the leading tab
  when the anchor is indented, or the cut eats it.
- **Assert, don't just call, a guard.** A check whose result is ignored lets a
  range delete run anyway.
- **A script that edits several files should write them only at the end**, so
  a failed assertion leaves nothing half-applied.
- **Bash heredocs mangle backslashes.** Write Python edit scripts to a file
  and run that.
- **Read the seam back** — both sides of it, and ask what *else* went, not
  only whether the target is gone. `check_orphan_heads.py` diffs definition
  heads against a base commit; nothing catches it for `.style`, so
  brace-balance those by hand.
- **Transitive includes.** Deleting a header can drop an include another file
  got through it. After a deletion, check what the removed headers pulled in
  (`base/timer.h`, `base/weak_ptr.h` and similar).

### Other traps

- **Answer requests; don't drop them.** Both clients hand request tokens to
  native code before the send, so a silently dropped request strands its
  caller. Reply with a failure the caller already handles.
- **Preserve load-bearing side effects** — see `updateOnline` above.
- **Check what a dependency drags in before removing it.** Only kimageformats
  linked `libdav1d.a`, so disabling the Qt plugins produced 14 unresolved
  symbols.
- **Look for anything plugged with a forced failure that a caller still waits
  on.** That lens found `SponsoredMessages::request` dropping its callback,
  before ads were deleted outright.

---

## Tools

`loogrigram-tools/`, run from the repository root with Python 3:

| Script | Does |
|---|---|
| `check_lists.py` | every source in CMake lists and `.qrc` files exists; every icon a `.style` names resolves. Two lib_base false positives (`base_windows_safe_library.*`) are known. |
| `check_orphan_heads.py <base>` | declaration heads left glued to the next definition by a removal, against a base commit |
| `check_styles.py` | what the style codegen would reject: undefined types, parents, fields and names |
| `unused_styles.py <file.style>` | style entries nothing reaches. Verify with grep; `colorIndex*` are upstream's deliberate `[[maybe_unused]]` |
| `prune_styles.py` | removes unreachable style entries repeatedly until none remain |
| `drop_styles.py <file.style> <names...>` | removes named definitions, brace-checked |
| `unused_icons.py` | icon files nothing names. `tray_monochrome.svg` and `poll/*.tgs` are known false positives |
| `removal_defs.py` | brace-aware helpers for scripted C++ removals |

---

## Repo layout

- **`dev`** — the upstream baseline: tdesktop
  `80158983dba09d3bf5d96701f21473d6c34bf5f5`, imported as one commit (a
  shallow clone cannot be pushed to a fresh remote), plus the workflow and
  README. The default branch, so it is what the repo page shows.
- **`patches`** — all fork work. Build from it.
- Every deviation carries a `LoogriGram:` comment. `grep -rn "LoogriGram:"
  Telegram/SourceFiles` is the whole behavioural diff and the practical way to
  audit or rebase the fork. It is worth more than tidy code.
- Remotes: `upstream` is tdesktop; `origin` is
  `github.com/romanimpair-jpg/LoogriGram-Desktop` (public).
- Upstream's `AGENTS.md` forbids `Co-Authored-By:` trailers; this fork's
  commits carry them.

---

## Constraints and known limits

- **API ToS §3.3** requires third-party clients to show sponsored messages.
  Removing ads knowingly violates it. The exposure is the registered `api_id`,
  which Telegram can revoke; the mitigation is re-registration. Accepted, for
  personal use.
- §2.3/§2.4 bar "Telegram" in the app title and use of its logo; "LoogriGram"
  and our artwork comply.
- Sending a message is inherently visible; nothing here hides anything from
  Telegram's servers.
- Last-seen concealment is reciprocal: you see only a vague "recently" for
  others. Hiding the read date likewise costs seeing other people's.
- Per-account settings live in `Main::SessionSettings`, encrypted with the
  account key, and do not transfer between installs. App-level settings
  (`tdata/settingss`) do.

---

## Android

Built, signed with our own key and in use since 2026-09-10. It has its own
notes: **`loogrigram-android/LOOGRIGRAM.md`**. Several lessons are shared —
answering requests rather than dropping them, dependencies that drag others
out with them, reading scripted edits back — and the read-receipt finding
here is one the Android side depends on.
