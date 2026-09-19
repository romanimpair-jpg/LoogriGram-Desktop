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
| Desktop CI | Green. Builds **Qt 5**, not Qt 6 — see below, this matters |
| Ads, AI, large emoji, paid reactions, emoji statuses | **Deleted**, not gated |
| macOS and Linux | **Deleted** — this tree builds Windows and nothing else |
| Updater | Ours, live, with progress on the row that starts it |
| **Android** | **Built, signed, installed and in use since 2026-09-10.** The "Android" section at the bottom of this file is stale history — read `loogrigram-android/LOOGRIGRAM.md` instead |

Verified in the installed build (`g2533f37`): no sponsored messages, no premium
badges anywhere including group author names, ghost mode toggle, Last Seen and
read date hidden server-side, no suggestion popups, no AI compose button, no
large emoji, no paid reactions, no stories reachable from profiles, no rating
bar, no "Premium users" privacy row, and the tray menu says LoogriGram.

### Where to pick up

`g07d1f55` built green on 2026-09-14, installed itself and is in daily use.
Verified in it: the new artwork, and no emoji status beside any name - the
two things `g2533f37` got wrong from a correct tree, so the build-cache fix
below is proven. The update progress ring could not be checked, because it
lives in the build *doing* the downloading and `g2533f37` did not have it;
it is verifiable on the next update.

**Since then `patches` has gained 34 commits - 388 files, −63,374 lines -
and not one of them has been compiled.** That is the premium and monetisation
removal; see `PREMIUM-HANDOFF.md` for the rule it now follows, what is done,
what is left, and the four checks that stand in for a compiler. The `out/`
cache is warm, so the next build should be 45-75 minutes rather than two
hours, and building only at the end of the removal is the agreed plan.

The rule, settled and not to be re-litigated: **no money operations
whatsoever, neither paying nor being paid.** Stars, TON, gifts,
subscriptions, in either direction. Inbound content is still *rendered* -
hidden at the view, never refused at parse - which is a different question
and is why `LoogriGram::HiddenContent()` exists.

One thing that looks like a bug and is not: the taskbar and Start Menu still
show upstream's plane. The binary does not contain it - the exe's resource
table has exactly one icon group with our eight images, and the 256px one
extracted from it is byte-identical to `branding/LoogriGram/icon256.ico`. It
is the Windows shell icon cache. Clearing Explorer's fixed the taskbar; the
Start Menu keeps its own and did not follow.

### Two bugs from one cause: source that never reached the binary

`g2533f37` shipped upstream's artwork *and* went on painting the emoji status
beside message authors, from a tree where both had been fixed for hours. One
cause, and it was the incremental build, not the code.

A rebase on 2026-09-14 stamped three commits `13:34:59Z`. The run that built
the commit *before* them finished at `14:12Z` and cached its `out/` tree. The
next run picked those three up, dated their files by git - and every one of
them looked older than objects built from the previous content. ninja reused
them. `logo_256.png` reached the exe through a `.qrc` whose generated resource
object was stale; `history_view_message.cpp` simply was not recompiled.

The lesson is not "use committer date instead of author date" - that was the
first fix and it would have lost to this too, because all three commits shared
one committer date that still predated the cached tree. **A git date and an
object's mtime are different clocks measuring different things**, and a
commit's date can precede a build that did not contain it.

So the comparison is gone. `out/` records the commit it was last built from
in `.loogrigram-build-base`, written only after a compile that *succeeded*,
and "Age sources against the cached build tree" diffs the checkout against it:
changed files are dated now, everything else 2000-01-01. A failed run still
caches its half-rebuilt tree but keeps the older marker, so the next run is
compared against a commit the tree was wholly consistent with. No marker means
dating everything now, which is the right answer for a tree we know nothing
about.

That alone threw a failed build's work away: every file changed since the
last success was dated now again, so everything the failed run compiled
recompiled. So a failed run also asks ninja (`-n -d explain`) which outputs
are still out of date, deletes exactly those, and records
`out/.loogrigram-build-attempt` - its commit and the date it gave each file.
The next run gives a file whose content is unchanged since that attempt the
same date back: objects the attempt compiled are newer and are reused, the
ones it failed or never reached are gone and rebuild. The record is consumed
before compiling, so a cancelled run can never pass a stale one on. A submodule bump no longer needs a manual salt bump either - it shows
up as that path in the diff.

**If a binary ever disagrees with the source again, suspect this before the
code.** Searching the exe for the bytes of an asset settles the artwork half
in a minute; for behaviour, check whether the commit that fixed it shares a
committer date with a rebase.

### The white flash is fixed, and the cause was the build, not the code

Three sessions went into this on the assumption it was our drawing. It was
not. **We were building Qt 6 and the official client ships Qt 5**, and the two
use different renderers:

```
Qt 6  ->  no ANGLE  ->  QRhi/D3D11  ->  media_view_overlay_rhi.cpp   -> flashes
Qt 5  ->  ANGLE     ->  OpenGL      ->  media_view_overlay_opengl.cpp -> does not
```

`DESKTOP_APP_USE_ANGLE` is defined only for `QT_VERSION < 6` (lib_ui,
`ui/gl/gl_detection.h`), so the Qt version silently decides which of two
viewer implementations you run. Upstream builds x64 twice - `qt: ["", qt6]` -
and the empty variant is Qt 5 and is what ships. Our workflow had been trimmed
to a single matrix entry and kept the wrong one. Nobody decided that.

**The lesson is bigger than the flash.** The official client is a fact you can
check in ten minutes: install the portable build, run it, read its `log.txt`.
It prints `Renderer: [OpenGL] (Window)` and `Using DirectX compiler` - that
second line only compiles when ANGLE is on, which only happens under Qt 5. All
three earlier sessions theorised instead. `branding/original/` and the
portable zip are kept for exactly this kind of comparison.

The earlier white-flash investigation notes are deleted rather than kept: the
premise was wrong, so the conclusions were about a renderer we no longer use.

### Editing `lang.strings` alone changes nothing at runtime

This cost a build to discover. The tray still read "Quit Telegram" in a binary
whose `lang.strings` said "Quit LoogriGram", and the rename was not wrong - it
was **irrelevant**.

`lang.strings` supplies only the *compiled defaults*. At startup the cached
cloud pack is replayed over them - about eleven thousand keys of Telegram's own
English - and `Lang::Instance::applyValue` overwrites every value it carries.
The log has been saying so all along: `Lang Info: Loaded cached, keys: 10993`.

So renaming anything in `lang.strings` needs a second half:
`LoogriGram::Lang::KeepCompiledString` in `core/loogrigram_lang.cpp` lists the
keys whose cloud value `applyValue` ignores. **Add the key there or the change
is cosmetic.** The list is sorted for binary search because it is consulted
once per key at every launch. It also covers a custom `.strings` file, which
reaches the same function.

Only strings that name *this program* belong in that list. References to
Telegram the service stay as they are - they are still true, and we do not run
the servers. `lng_terms_delete_warning` shows the split inside one string.

### Never add or remove keys in `lang.strings`

Our strings live in **`core/loogrigram_lang.cpp`**, with their own keys and
lookup. Add an accessor there; that is the whole process.

`lang.strings` feeds a code generator whose key indices are **positional**, so
inserting one string renumbers every key after it. The incremental build tree
does not cope: adding one key rebuilt five objects, and `lang_instance.cpp` —
which sizes the value array from `kKeysCount` — was not among them. The
resulting binary asserted `"key < _values.size()"` on opening the main menu and
returned off-by-one strings elsewhere, which is what the `Lang Error:
Unexpected tag` spam at startup had been reporting all along.

Changing an existing string's *text* is safe; only adding and removing keys
renumbers. If you ever must, **bump `OUT_CACHE_SALT`** in the same commit.

The wider hazard is unresolved: that build showed a generated-header change not
propagating to its dependents at all. It is sidestepped for our strings, not
fixed, and could bite on any future codegen change.

### Deleting a file: four lists, not one

Five builds in a row died before compiling a single object, every one of them
because something still named a file that had been deleted. Each failure was
the same mistake wearing different clothes, so check all four every time:

| Where | How it names the file |
|---|---|
| `Telegram/CMakeLists.txt` | path relative to `SourceFiles` |
| `Telegram/cmake/*.cmake` | **a separate list** - `td_ui.cmake` holds the UI sources |
| `*.qrc` | path relative to the qrc |
| `*.style` | **bare icon name**, no extension, and **no platform guard** |

That last one is the nastiest. The style codegen parses every `.style` file in
full on every platform, so deleting a macOS-only icon breaks the *Windows*
build - there is no `#if` in a style file. And the fix is to delete the style
entry, not to restore the asset.

Both checks below are scripted and were run over the macOS and Linux removal;
the icon one is the reason that landed without a wasted build. Worth writing
them out again rather than trusting a grep: a `.style` name has no extension,
and `-WxH` and `-flip_*` are codegen directives rather than part of it.

`Telegram/cmake/generate_models.cmake` bakes every `*.obj` under
`Resources/art` into `.binobj` at build time, so a `.obj` with no apparent
reader is a build input, not an orphan.

Two checks worth re-running after any deletion, both cheap:

- resolve every source path in all four lists against the filesystem;
- resolve every icon name used in any `.style` against `Resources/icons`,
  stripping `-WxH` and `-flip_*` suffixes, which are codegen directives rather
  than part of the filename.

### Scripted range edits are the single biggest source of self-inflicted damage

`sed '/marker/,+3d'` assumes it knows how long a block is. One such edit cut a
six-line composed icon in half and left its last layer glued to the line
above; another took `setScreenIsLocked` out with the function beside it; a
third stranded `: nullptr)` in a constructor initialiser list.

Prefer exact-text edits. When a range delete is unavoidable, **read the seam
back afterwards** - both sides of it, not just that the target is gone. The
method-definition diff below catches this for `.cpp`; nothing catches it for
`.style`, so brace-balance those by hand.

`python3` exists on the dev machine; plain `python` does not and will hang on
stdin, taking the rest of the shell command with it.

### Editing lessons, learned expensively

- **Never cut a range between two markers without checking what is inside it.**
  Removing one function by slicing from its opening line to "the next function"
  took nine unrelated `InnerWidget` methods with it, because the function
  assumed to follow sat 370 lines further down. It compiled — nothing in that
  translation unit referenced them — and only the linker caught it, after a
  1h47m build. Diff the set of `Class::method(` definitions against the
  pre-edit baseline and confirm only intended names disappeared:

  ```
  for f in $(git diff --name-only BASE HEAD -- '*.cpp'); do
    diff <(git show BASE:$f | grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' | sort -u) \
         <(grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' $f | sort -u) | grep '^<'
  done
  ```

- **Reading the result back is not enough by itself.** It was done, and it
  confirmed the sponsored symbols were gone — which is the wrong question. Ask
  what *else* went with them.
- **A running executable can be renamed, but not deleted or overwritten.**
  Measured rather than assumed: rename succeeded with the process live, writing
  a new file at the freed path succeeded, deleting the running image failed
  with access denied. The whole updater rests on this.
- **Actions artifacts need an authenticated token even on a public repo.**
  Release assets do not. That is why the updater reads releases.
- **The binary's bulk is `tg_owt` (WebRTC), not Qt.** Release archives:
  tg_owt 275MB, Qt6Gui 97MB, Qt6Widgets 97MB, Qt6Core 81MB. Archive size
  overstates contribution, but the ranking holds — so shrinking the download
  means asking whether the calls stack is needed, not whether Qt can be split
  out. There is no CMake flag for it; `lib_webrtc` is linked unconditionally.

Installed app lives at `C:\LoogriProjects\LoogriGram\app\LoogriGram.exe`.
**To update: replace only the .exe.** tdesktop keeps its profile *beside the
executable*, so `app\tdata\` holds the session, settings and ghost-mode state and
must stay put. Running a copy of the exe from anywhere else silently creates a
second empty profile and looks like a logout.

---

### Debugging, and how not to waste builds

Three sessions were burned guessing at the viewer flash and shipping the guess.
What actually solved the crash took one log line. Prefer ground truth:

- **A failed `Expects`/`Assert` writes its message, file and line to `log.txt`
  before it dies** (`base/assertion.h` in lib_base: `log()`, then a deliberate
  null write). So the answer is usually already on disk.
- **That deliberate null write means a failed assertion appears as an access
  violation, `0xc0000005`.** Do not read that code as memory corruption.
- **`log.txt` is overwritten on every start.** To capture a crash: reproduce
  it, then *do not relaunch* until the file has been read.
- **The Windows Application event log records both the crash time and the
  process start time**, so "how long was it alive" is measurable — enough to
  tell a startup-path fault from something triggered by a user action.
- **Crash reports are off** (`DESKTOP_APP_DISABLE_CRASH_REPORTS=ON`): that is
  upstream's breakpad, which offers to upload dumps to Telegram, so it stays
  off. Windows writes a full dump anyway, to `%LOCALAPPDATA%\CrashDumps`.
- **Reading a dump.** Every `mode=build` run uploads a private artifact,
  `LoogriGram x64 Release symbols <tag>`, holding `LoogriGram.pdb` for exactly
  that executable (debug info is on for the `Telegram` and `td_ui` targets
  only; see the comment in `Telegram/CMakeLists.txt`). Put the PDB beside the
  crashed exe and open the dump in WinDbg / `cdb -z <dump> -y <dir>`, then
  `!analyze -v` and `kb`. A dump is only readable against the PDB of the build
  that crashed, so builds from before this was added (up to `bd75c09660`) have
  none. Frames inside `lib_ui` and other dependencies still resolve to
  exported names only.
- **Do not dispatch a build to test a hypothesis that logging could settle**,
  and ask before dispatching at all. A wrong 25m build is cheap only once;
  the habit of building instead of thinking is what cost the time here.
- **A `mode=build` + `config=Release` run is not just spent compute - it
  publishes a release.** The installed app polls that endpoint once per
  launch, so a build that merely compiles will offer itself to this machine
  as an update. That is why the dispatch gets asked about every time, not
  only when the build looks risky. A build that *fails* publishes nothing:
  `Publish release.` carries no `always()`, so a failed compile skips it.

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
  Their subjects are deleted now too, so re-enabling any of them would only
  fail; `win.yml` is the only workflow with anything left to build.
- **Rebasing onto upstream will now conflict widely.** Removing macOS and
  Linux touched 272 files, mostly by taking branches out of files upstream
  keeps editing. Take ours for anything under a deleted platform, and expect
  to re-resolve `#ifdef` chains by hand where upstream adds one.

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

- **`Age sources against the cached build tree`** diffs the checkout against
  the commit `out/` was last built from (hence `fetch-depth: 0`) and dates
  changed files now, everything else 2000-01-01. Do **not** replace this with
  a git date per file: that is what put upstream's artwork and a removed emoji
  status into `g2533f37`, and the section above says why it cannot work.
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

- **A submodule pointer bump no longer needs a salt bump.** It shows up as
  that path in the diff and the whole submodule tree is dated now. Adding or
  removing keys in `lang.strings` still does — see above.
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
   format: no debug arguments at all, self-consistent, and faster. The two
   targets holding our own code set it back per target in
   `Telegram/CMakeLists.txt`, which is what makes crash dumps readable; the PDB
   is moved out of `out/` before the cache is saved.
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

Suppressed: typing/activity broadcasts (group-call *speaking* is exempt) and
online presence. Story views went with stories. Deleted outright, not tied to
the toggle: reading
telemetry (per-message dwell time and scroll depth) and post view-count
contributions — `ReadMetrics`, `ViewsManager` and the list-side tracker are
gone.

**Delivery reports are sent, deliberately.** Ordinary chats have no delivery
receipt at all (one tick means the server has it). The only report the client
sends is `messages.reportMessagesDelivery`, for messages carrying
`report_delivery_until_date` — login codes sent through Telegram Gateway —
and it tells the sending service the code arrived. The user chose to keep it
(2026-09-19): blocking it mostly makes services resend codes by SMS. Earlier
versions of these notes wrongly listed delivery receipts as suppressed.

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

### Traps in the ghost-mode code

- `Updates::updateOnline` also drives `checkAutoLock`, `saveCurrentDraftToCloud`
  and, when quitting, `quitPreventFinished()`. Early-returning breaks passcode
  auto-lock and cloud drafts and **hangs shutdown**. Only the reported value is
  changed.
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
It publishes what it is doing - None, Checking, Downloading, Ready plus a
progress pair - and the main menu row reads that: the label follows the state
and a ring fills around the icon. Ready is terminal, and the row then offers
the restart, because the build is already on disk by then. The asset's total
size can be unknown until the redirect resolves, so the label drops the
percentage rather than claiming 0%.
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
- **`Core::Restart()` is the wrong call after an update — use
  `Core::RestartAfterUpdate()`.** Verified working. `Restart()` is built for a
  restart the user asked for from a settings row, so it always sets
  `RestartingToSettings` and the new instance opens Settings on top of the chat
  list. It also inherits how *this* process was launched, and there are **two**
  independent ways that hides the new window: `-startintray`, and `-autostart`,
  which `MainWindow::firstShow` turns into a hidden window whenever the stored
  `StartMinimized` is on. Clearing `StartMinimized` is the wrong lever - it is a
  stored preference, so you would either overwrite the user's choice or change a
  value the new process re-reads from disk. `RestartAfterUpdate` sets
  `RestartingAfterUpdate`, and `launcher_win.cpp` drops both arguments when it
  sees it.
- **Testing the updater needs two builds and a commit held back.** The fix lives
  in the build *doing* the restarting, so it can only be proven by installing a
  build that has it and then updating *from* that one. Keep one commit unbuilt,
  install the baseline by hand, then push the held-back commit. Make it a change
  you can see - the icons were chosen for that - so you can tell an applied
  update from a mere restart. CI builds `--ref patches` and a side branch cannot
  see the dependency caches, so the branch tip has to *be* the commit you want
  built.
- **A locally built binary never updates itself.** The committed tag is `"dev"`,
  which matches no release; the updater refuses to run unless the tag looks
  real, otherwise every local build would immediately overwrite itself with CI's.
- **The asset is gzipped.** Measured on a real build: 220MB raw, 71MB with
  gzip, 57MB with xz. gzip wins because zlib is already linked *and already
  decoding gzip streams* - `inflateInit2(&stream, 16 + MAX_WBITS)` appears
  twice in the tree - so it cost about thirty lines, while xz would add a
  dependency for another 14MB. The uploaded CI artifact stays uncompressed;
  only the release asset is packed.
- **Why not ship Qt and OpenSSL separately and update only our code?** Because
  static linking means they do not exist as separate things: the linker takes
  only the objects we reference, drops the rest (`/OPT:REF`), and lays the
  survivors out among ours, so identical Qt code lands at different offsets
  whenever our code changes size. Getting that would mean linking dynamically -
  rebuilding every dependency as a shared library, which is the most fragile
  part of this build - and multi-file updates then lose atomicity, need a
  helper process that runs after exit, and need a manifest of files and hashes.
  That is Telegram's packed-archive plus `Updater.exe` design, reinvented. The
  `Report size composition.` CI step exists to price this properly before
  anyone tries: it ranks the dependency static libs by size. Read it as a
  ranking only - `/OPT:REF` means a 60MB `.lib` may contribute a fraction of
  that, and only a linker map answers it exactly.
- **Deltas are a separate question from file count** - a single 220MB file can
  be binary-diffed fine. Not done: executables delta poorly under naive bsdiff
  because relinking shifts every address (this is why Chrome built Courgette),
  our builds relink wholesale every time since the stamped tag guarantees it,
  and a corrupt patch produces a subtly broken binary that still passes the
  `MZ` check. Worth revisiting only if updating becomes frequent.
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
- **No suggestion popups above the message field**: the emoji suggestion
  controller, the emoji panel's `:shortcode:` tooltip and the sticker half of
  the field autocomplete (a lone emoji offered stickers even with the setting
  forced off) are deleted, with the three settings; their slots in the stored
  settings streams are kept and read into nothing.
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

- ~~**Ads.**~~ **Done** — deleted across three commits, ~4,500 lines: the
  message pipeline, the viewer's playback ads, `Data::SponsoredMessages`
  itself, `MessageFlag::Sponsored` and the fake-webpage `HistoryItem`
  constructor that ads were built on, and sponsored **peer search**, which had
  never been gated at all and was still requesting and showing sponsored
  channels in search results.

  Deliberately left, being different features rather than ads shown to us: the
  **promoted / proxy-sponsor channel** (`Data::PromoSuggestions`,
  `help.getPromoData`, `History::isPromoted`, `SubItem::Sponsored`, and the
  `lng_proxy_sponsor_warning` label in `connection_box.cpp` — that label is
  honest disclosure, and the same slot also carries PSAs), and the
  channel-owner side: Business "Sponsored Messages",
  `channels.restrictSponsoredMessages`, `account.toggleSponsoredMessages`, and
  ad revenue in `api_earn` / `info_channel_earn_list`. Those belong with the
  premium and monetisation removal — and the Business one is wanted gone.
- ~~**AI compose.**~~ **Done** — ~8,000 lines. Rewrite, translate, tone
  presets, the article-editor pill, the caption button, the shortcut, the
  experimental toggle, the `addstyle/` deep link. Three things in the way were
  *not* AI and were extracted rather than deleted: the paste-as-file helpers
  (now `ui/controls/compose_text_helpers.cpp`), `ComposeTooltipManager` (now
  `history_view_compose_tooltip.cpp`, and the send-as-file tooltip uses it),
  and `st::historyAiComposeButton`, which was the base style for three
  unrelated buttons and is now `historyComposeInnerButton`.
- ~~**Large emoji.**~~ **Done.** Took a whole subsystem with it:
  `EmojiPack::image` was the only caller of `EmojiImageLoader::prepare`, so
  the loader, its clear timer and `Ui::Emoji::SourceImages` are all gone.
  `st::largeEmojiSize`/`Outline` survive - `data_custom_emoji.cpp` sizes
  `SizeTag::Isolated` from them.
- ~~**Paid reactions.**~~ **Done.** `Data::Parse` drops the server's
  `paid_reactions_available`, which covers sending, the button and the admin
  toggle at once; display is separate, so `InlineListDataFromMessage` also
  drops any paid reaction a message arrives carrying.
- ~~**Premium emoji statuses.**~~ **Done.** Not one painter but two, plus a
  slot inside the message bubble that belonged to neither, and
  `premiumBadgesShown()` was a gate in front of the first two rather than a
  removal of any of them. All deleted, along with the machinery that only
  existed to keep an animated status moving: `PeerListRow::_statusIconRect`,
  `PeerListContent::updateRowStatus` and the badge rect in every `CachedRow`
  all existed to punch the status out of a scrolling row cache and repaint it
  live. Nothing animates in a row name now, so a cached row is just the row.
  `BadgeType::Premium` is gone with them, which took the only clickable badge
  and so the three places that opened the status picker from one.

  Left on purpose: the verified check and the scam / fake / direct badges,
  which are warnings rather than purchases; the bot verification icon, drawn
  *before* the name from a separate ungated path - a different feature; and
  `EmojiStatusPanel`, which is also the custom emoji picker for topic icons
  and profile pattern emoji. "Set as status" in the emoji picker menu still
  exists and now sets something no client here displays.
- ~~**Stories on profiles, rating, profile backgrounds.**~~ **Done.** The
  userpic click opened stories from four places, all removed, with the rings.
  `Ui::StarsRating` deleted outright. The gradient/solid/pattern profile
  background is off at its three inputs.
- ~~**The upsell entry points.**~~ **Done.** No call to
  `Settings::ShowPremium`, `ShowPremiumPreviewBox`, `ShowPremiumPreviewToBuy`
  or `ShowPremiumPromoToast` survives outside files that later steps delete
  whole. Where a restriction is the server's, its wording is kept verbatim
  and only the link into the page selling a way past it is removed. Where
  the text was nothing but a pitch, the surface went.
- ~~**The premium 3D effect renderers.**~~ **Done**, with
  `Resources/art/premium/` and the `.obj` -> `.binobj` build step that
  existed only to feed them.
- ~~**Boosting.**~~ **Done.** Giving a boost spends a subscription slot, so
  the whole flow was a way of paying: `resolveBoostState`, `applyBoost`,
  `MTPpremium_ApplyBoost`, five boxes, the slot-reassignment screen, the
  menu item and `?boost` links. Kept, being the channel-owner side rather
  than a purchase: `AskBoostBox`, the Boosts statistics page,
  `ParseBoostCounters` and `LookupBoostFeatures`.
- ~~**Telegram Business.**~~ **Done**, ~7,400 lines - but only half of what
  wears the name. See `PREMIUM-HANDOFF.md`: the data layer stays, because
  `data_shortcut_messages` threads a shortcut id through the whole send
  pipeline and `data_business_common`/`info` carry *other people's* opening
  hours, location and chat intro.
- ~~**Emoji statuses, the rest of them.**~~ **Done.** They still tinted
  profile headers and call panels from a collectible's own palette, could
  still be set from two context menus, and were still being requested by
  bots - now refused with `USER_DECLINED`, the answer the declined box sent,
  because an unanswered bot request strands the page.
- **Premium.** `premiumCanBuy()` is one line that neutralises the settings
  block and every limit box, and what it neutralises is all still compiled.
  `premiumBadgesShown()` is gone - the badges it gated are deleted rather than
  hidden. The lesson it left stands: **a gate in front of two painters is not
  a removal, and a third painter can have its own slot.** That is exactly how
  the author-name status survived it.
- ~~**Suggestion popups.**~~ **Done** (2026-09-18) - see "Other desktop
  changes". The setting was never the whole story: sticker suggestions by
  emoji still came from installed sets with it forced off.
- **Ghost-mode suppression sites.** Telemetry and view counts are deleted.
  Typing (`SendProgressManager::skipRequest`), online status
  (`Updates::updateOnline`) stay: they follow the ghost-mode switch, so they
  are settings, not dead code. `updateOnline` also drives
  `checkAutoLock`, `saveCurrentDraftToCloud` and `quitPreventFinished()` -
  only the reported value changes. `Histories::reportPendingDeliveries` and
  `RepliesList::sendReadTillRequest` were listed here by mistake: neither was
  ever suppressed (see Ghost mode).
- ~~**The bot verification icon.**~~ **Done** (2026-09-18, the user chose
  "icon only"). Not drawn before any name, on the profile or in the new-chat
  intro; the verifier's text stays on the profile and in that intro.
  `PeerBadge` is the free function `Ui::DrawPeerBadgeGetWidth`.
- ~~**`specific_win.cpp:450`**~~ **Done** earlier: the Startup shortcut reads
  "LoogriGram autorun link".
- **Level-locked channel and group admin options** (2026-09-19, the user:
  remove entirely, at every level). Channel levels come from boosts, which
  only Premium subscribers give. Auto-translate is gone; the appearance box
  (colour, background emoji, profile colour and emoji, emoji status,
  wallpaper) and the group emoji pack chooser are in progress; channel custom
  reactions are next. Kept: free voice transcription in boosted groups - it
  is not an admin option and removing it would only take a working feature
  away.

### Stories: removed

Removed in full (2026-09-19), in stages: the chat list strip, the
`info/stories` section and profile tab, the viewer (`media/stories/`, its
mode in the media viewer and the renderers' story paths), story state on
peers and lists, the story parts of statistics, and the data layer
(`data_stories`, `data_story`, `data_stories_ids`). `updateStory` and
`updateReadStories` are ignored.

- **Messages carrying a story are hidden like gifts** - a forwarded story or
  a story mention is `ContentHidden` from its TL type, before parsing
  (`LoogriGram::StoryMedia`). A reply to a story keeps its text and loses
  the quote. Story links open the peer; previews of them are plain articles.
- **Server state is round-tripped, not changed:** admin story rights are not
  shown but kept on save; `stories_muted` and the story reaction notify
  setting are read and sent back unchanged.
- The story message-id range in `data_msg_id.h` is kept so the special ids
  after it keep their values.
- Still to go: the stories part of "Export Telegram data".

## Gifts, giveaways and paid posts are hidden, not refused

We take no part in any of it, so none of it is shown: not a gift someone
sent, not a giveaway a channel is running, not a post we would have to pay to
read, and no notification for any of them.

**Hidden at the view. The item is still created.** This is the load-bearing
part. Telegram tracks what we have read by message id, and the read position
only advances past messages we have. Refuse one at parse time and nothing
ever marks it read, so the chat keeps an unread badge that scrolling cannot
clear - the same coupling that made read-receipt suppression get reverted,
arriving from the other direction. So the item lives in history exactly as
before and every view asks `LoogriGram::HiddenContent()` instead.

The machinery is upstream's, built for photo albums: a hidden `Element`
already collapses to nothing, because `Message::marginTop`/`marginBottom` and
`Service::marginTop` all return zero when `isHidden()`, and
`Message::resizeContentGetHeight` returns only its margins. No gap, no
placeholder.

Three places ask, and all three are needed:

- `Element::isHidden()` - the message itself.
- `History::computeChatListMessageFromLast` - otherwise the chat list row
  advertises a gift that is not in the chat when you open it. It walks back
  to the newest message we do display, the same way upstream skips the group
  migration message, so the chat does not even rise to the top.
- `System::skipNotification` - a toast for a message that is not there is
  worse than no toast.

Recognition is by media, which works because every gift arrives as
`MediaGiftBox` whichever action delivered it. Service messages that are only
a line of text - "X boosted this channel", a refund, a price change - carry no
media, so those six actions are routed to `PrepareEmptyText` instead, which is
upstream's own way of saying an action displays nothing.

`PeerGiftsCountValue` returns zero, which removes the gift row, the gift tab
and the request behind them at once. That one is a forced getter on purpose
and temporarily: the tab machinery belongs to the gifts subsystem and goes
with it.

**Known and accepted:** paid posts in channels vanish without trace. There is
no sign a post existed.

## Branding

Four files carry it on Windows, and all four are ours now:

| File | Used for |
|---|---|
| `Resources/art/icon256.ico` | exe, taskbar, Alt-Tab — via `Telegram.rc` |
| `Resources/art/logo_256.png` | tray and window icon — `Window::Logo()` |
| `Resources/art/logo_256_no_margin.png` | small tray sizes, **and the Saved Messages avatar** |
| `Resources/icons/tray_monochrome.svg` | dark-mode monochrome tray — the only vector |

The `.ico` is hand-packed by a stdlib-only script (Pillow is not installed):
eight entries, 16/20/24/32/48/64 as 32bpp BMP and 128/256 as PNG, downscaled by
area-averaging on *premultiplied* alpha so edges do not darken. Upstream shipped
only 16/24/32/48/256, which is why it softened in Alt-Tab. The monochrome SVG is
traced from the raster mark - it is drawn as a mask, so only the silhouette
matters.

Sixty-eight unused icon files were deleted: the UWP store assets, the Linux
hicolor PNGs, the macOS set and the `icon_green` alpha variants. `mac_tray_icon`
had to stay deleted *and* have its `window.style` entry removed - see the four
lists above.

Originals are kept in `branding/original/` alongside the official portable zip,
outside the checkout, for comparison.

The artwork was correct in the tree the whole time - it was the build that
kept serving a stale resource object. See "Two bugs from one cause".

## Constraints and known limits

- **API ToS §3.3** requires third-party clients to support sponsored messages. Ad
  removal knowingly violates it. Exposure is the registered `api_id`, which
  Telegram can revoke; mitigation is re-registration. Accepted, personal use.
- §2.3/§2.4 bar "Telegram" in the app title and use of its logo — "LoogriGram"
  complies, hence also the publisher-metadata change. The artwork closes the
  logo half of that.
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

## Android

**Started, finished, shipped.** It was built, signed with our own key and
installed on 2026-09-10, and has been in use since.

Everything below this line used to be the plan for starting it - mapped call
sites, CI requirements, decisions taken. It is history now and several of its
guesses about file layout were wrong by the time the work happened, so it is
removed rather than left to mislead.

Read **`loogrigram-android/LOOGRIGRAM.md`** instead. It carries the real state,
the three-mode CI and its measured costs, the traps that each cost real time,
and what still needs doing. Several lessons are shared with this file - the
dependency that drags an unrelated one out with it, answering requests rather
than dropping them, and reading scripted edits back - and the read-receipt
finding in this file is one the Android side depends on.
