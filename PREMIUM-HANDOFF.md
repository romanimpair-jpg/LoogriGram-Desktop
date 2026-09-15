# Removing Premium — handoff

Rewritten 2026-09-15, second session. Read `LOOGRIGRAM.md` first; this covers
only the premium and monetisation removal.

## Where the work lives

**All of it is on `patches`, merged and pushed.** The `premium-work` side
branch is fully contained in `patches` now and can be deleted; it is left
only so the earlier history is easy to find.

| | |
|---|---|
| `patches` | 21 commits past the last build, 248 files, **−44,930 lines** |
| Last commit that actually compiled | `07d1f5568d`, green 2026-09-14, installed and in use |
| Everything since | **Never compiled. Not once.** |

That last line is the whole risk. Twenty-one commits of deletion, every one
of them checked by the audits below and none of them by a compiler.

### Building it

The `out/` tree cache from the green build is warm and valid:
`Windows-x64-out-Release-v4-…-07d1f5568d`, 1.34 GiB. **Expect 45–75 minutes,
not two hours.** The two-hour figure is the *cold* case, and the last run was
cold on purpose because `OUT_CACHE_SALT` had been raised to 4 to discard
every tree built under the old timestamp scheme.

45–75 rather than the measured 46 because this diff touches
`window_session_controller.h`, `mtp_instance.h`, `main_session_settings.h`
and several `.style` files, whose generated headers are included nearly
everywhere. The useful corollary: **those wide headers are already dirty, so
piling more removal into the same build costs very little extra.**

And ask first. `mode=build` + `config=Release` publishes a release, and the
installed app offers it as an update on next launch.

## The one rule that decides everything

**We take no part in it; we still render what arrives.** Settled three
separate times now, and it is the difference between a removal that works and
one that breaks ordinary chats:

- **Gifts** are hidden at the *view*, never refused at parse. The item stays
  in history or the read position never advances past it and the chat keeps
  an unread badge scrolling cannot clear - the read-receipt coupling arriving
  from the other side. `LoogriGram::HiddenContent()` is the one predicate,
  and three places ask: `Element::isHidden()`,
  `History::computeChatListMessageFromLast` and `System::skipNotification`.
- **Business** is two things under one name. The settings that configure
  *our* business account are deleted; the data layer is not, because
  `data_business_common` and `data_business_info` carry *other people's*
  opening hours, location and chat intro.
- **Shortcut messages** looked like a Business feature and are not.
  `data_shortcut_messages.h` has sixteen includers outside Business - the
  send pipeline threads a shortcut id through `api_sending`, `api_editing`,
  `apiwrap`, `history_item`, `share_box` and more.

Before deleting any directory, count what includes it from outside. Twice now
the count has changed the plan.

## What is done

Ten commits this session, on top of the nine in the first one.

- **The upsell entry points are closed.** There is no call to
  `Settings::ShowPremium`, `ShowPremiumPreviewBox`, `ShowPremiumPreviewToBuy`
  or `ShowPremiumPromoToast` left outside files that later steps delete
  whole. That was the gate on everything else.
- Where a restriction is the server's and real, its wording is kept verbatim
  and only the link is removed - "Telegram Premium" as plain semibold text
  instead of a link into the page selling it. Where the text was nothing but
  a pitch, the whole surface went.
- **Boosting is gone** - `resolveBoostState`, `applyBoost`,
  `MTPpremium_ApplyBoost`, five boxes, the slot-reassignment screen, the menu
  item, `?boost` links. Giving a boost spends a subscription slot, so the
  flow was a way of paying and nothing else.
- **The speed-limit nag is gone**, six layers down to the MTProto flag that
  detected the server throttling a non-subscriber.
- **Telegram Business is gone** - ten modules, ~7,400 lines.
- **Emoji statuses are finished** - they still tinted profile headers and
  call panels from a collectible's palette, could still be set from two
  context menus, and were still being requested by bots.

## What is left, in the order to take it

1. **Credits / Stars / TON** (~11,000 lines). The big one and the next one.
   Measured coupling, which is why it was not started at the tail of a
   session: 30 includers of `data/components/credits.h`, 35 of
   `settings/settings_credits_graphics.h`, 25 of `api/api_credits.h`, 20 of
   `ui/effects/credits_graphics.h`, 14 each of `boxes/send_credits_box.h` and
   `data/data_credits.h`. `core/credits_amount.h` is a core money type with
   11 includers.

   Expect the same inbound/outbound split: a message that merely *mentions* a
   star amount has to keep rendering, exactly as a received gift does.

2. **Gifts** (~26,000 lines), which are priced in stars and therefore bound
   to the above. `star_gift_box.cpp` alone is 5,354 lines.

3. **`settings_premium.cpp` (2,165) and `premium_preview_box.cpp` (1,836).**
   These were step 2 in the old plan and are now nearly last, because what
   holds them is not upsells any more - it is gifts. Two things must be
   extracted first, both found by the include audit rather than by reading:
   - `ShowStickerPreviewBox` lives in `premium_preview_box.cpp` and is not a
     premium surface at all. `window/section_widget.cpp` uses it.
   - `Settings::MakeEmojiStatusPreview` has no callers left as of this
     commit, but check again rather than assuming.

4. **`premiumCanBuy()` and `premiumPossible()` last of all.**
   `premiumPossible()` is exactly `premium()` now, so every
   `(premium || !premiumPossible)` is provably always true - but there are
   ~100 references, so do it when the surfaces are gone.

Also still present, noted rather than done:

- **The bot verification icon.** `PeerBadge::drawVerified` paints an
  arbitrary server-supplied custom emoji *before* a name, from a path no gate
  ever touched. A third party paid to mark that account, and it is the only
  coloured emoji left beside a name. Four call sites. Take it and `PeerBadge`
  holds no state at all, so it becomes a free function and every `_badge`
  member that exists to carry that state goes with it.
- **`specific_win.cpp:450` hard-codes "Telegram autorun link. You can disable
  autorun in Telegram settings."** into the Startup shortcut's description.
  A branding leak of the same class as the `lang.strings` one, in a file that
  survives. Found by reading the shortcut, not the source.

## Checks to run after every removal here

All four are cheap and every one of them has caught something real.

```
# 1. brace AND paren balance, compared against HEAD rather than to zero -
#    two files in this tree are already off by one inside string literals
# 2. the Class::method definitions lost are exactly the intended ones
for f in $(git diff --name-only HEAD -- '*.cpp'); do
  [ -f "$f" ] || continue
  diff <(git show HEAD:$f | grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' | sort -u) \
       <(grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' $f | sort -u) | grep '^<'
done
# 3. python3 Telegram/build/audit_includes.py HEAD
# 4. every build-list path resolves on disk, and every icon named in any
#    .style resolves under Resources/icons or lib_ui/icons. The baseline is
#    257 false positives (generated qrc, folder icons); compare to that
#    number, not to zero.
```

## Traps sprung this session

Four, and three of them were invisible to the cheap checks.

- **A guard that was never asserted.** A scripted range delete of
  `MainWidget::showNonPremiumLimitToast` was protected by a callback counting
  the method definitions inside the range - written as a lambda whose result
  was never asserted, so it evaluated to false and did nothing. The range ran
  on and took `showBackFromStack` with it. Brace *and* paren counts both
  stayed balanced. Only check 2 saw it. **Assert, do not merely call.**
- **A replacement comment with no trailing newline** glued the following `}`
  onto the end of the comment, commenting out the closing brace of a
  constructor. Balanced, and wrong, because the brace is still a character in
  the file. Only reading the seam saw it.
- **The include audit found two real breaks** among 31 dropped includes:
  `section_widget.cpp` needs `ShowStickerPreviewBox` and
  `bot_attach_web_view.cpp` needed `MakeEmojiStatusPreview` - both
  non-premium functions that happen to live in premium files. Dropping an
  include because the file no longer uses *the premium thing* is not the same
  as the file no longer using the header.
- **A generated id list with CRLF endings** made 379 consecutive
  `gh cache delete` calls fail, silently, because each id carried a trailing
  `\r`. Same shape as the `MediaController` NUL byte on the Android side:
  Windows line endings in a file another tool reads.

## The app icon is not a build problem

Worth recording because it looks exactly like the stale-resource bug that
shipped `g2533f37`, and is not.

The taskbar and Start Menu show upstream's plane. The binary does not contain
it. Read out of the running `app\LoogriGram.exe` with a PE resource parser:
one `RT_GROUP_ICON` (id 640), eight `RT_ICON` entries, sizes 16/20/24/32/48/64
BMP plus 128/256 PNG - ours. Upstream ships five. The 256px image extracted
from the exe is MD5-identical to `branding/LoogriGram/icon256.ico`.

So it is the Windows shell icon cache. Clearing
`%LocalAppData%\Microsoft\Windows\Explorer\iconcache*.db` and restarting
Explorer fixed the **taskbar**. The Start Menu kept its own copy and did not
follow, even after deleting `IconCache.db`, the `{AFBF9F1A-…}` app-list
caches and re-saving both shortcuts. Left there deliberately - it is cosmetic
and costs nothing in the repo.

## CI caches

Cleaned this session: 385 entries down to 5, 4,791 MB down to 3,826 of the
10,240 available. What went was 378 orphaned `sccache/*` entries (sccache was
removed from the workflow long ago for logging zero compile requests),
`qt6-Release-v1` (the matrix builds Qt 5 only) and `libs-Release-v1`
(superseded by v2). What is left is all live.

**The two `out-Release-v4` trees are deliberate, not waste.** The workflow
prunes to the newest two on purpose: a failed run also caches its
half-rebuilt tree, and the older entry is the last *good* one to fall back
to. Only the newest is ever restored.
