# Removing Premium — handoff

Written 2026-09-15, mid-job, for whoever picks this up. Read
`LOOGRIGRAM.md` first; this only covers the premium removal.

## Where the work lives

| Branch | What | Verified? |
|---|---|---|
| `patches` | Everything up to and including the macOS/Linux removal, plus one include fix | Building as `07d1f5568`. The build *before* it failed on one file, fixed by that commit |
| `premium-work` | 9 commits, the premium removal so far. Rebased onto `patches` | **No. Never compiled.** |

`premium-work` is 76 files, −33,159 lines. Merge it into `patches` and build
*that* once `patches` is known green — do not stack more on an unverified
base than you have to.

**Build from `patches` only.** GitHub scopes caches by branch, so a side
branch pays the full two hours. And ask before dispatching: a
`mode=build` + `config=Release` run publishes a release, and the installed
app offers it as an update on next launch.

## The decision everything rests on

**Gifts, giveaways and paid posts are hidden at the view, not refused at
parse.** The `HistoryItem` is still created and still lives in history.

This is not fussiness. Telegram tracks what has been read by message id, and
the read position only advances past messages we have. Drop one at parse
time and nothing ever marks it read, so the chat keeps an unread badge that
scrolling cannot clear — the same coupling that made read-receipt
suppression get reverted here, arriving from the other direction.

`LoogriGram::HiddenContent()` in `core/loogrigram_hidden_content.cpp` is the
one predicate. Three places ask, and all three are needed or the hiding
leaks: `Element::isHidden()`, `History::computeChatListMessageFromLast` (or
the sidebar advertises a gift the chat will not show), and
`System::skipNotification` (or a toast announces a message that is not
there). `Element::refreshMedia` also stops before building a view for one.

Upstream already supports all of this — hidden elements cost zero height,
built for photo albums. Nothing was invented.

## Decisions already taken — do not relitigate

- **Two non-purchase things are hidden deliberately.** `MediaGiftBox` is a
  container, not a purchase marker. A birthday suggestion ("X suggests you
  add your date of birth") and a collectible chat theme are filed under it,
  and the user chose to hide both. There is a comment in the predicate
  saying so, because the obvious "fix" is to narrow the check.
- **Limit boxes keep their explanations.** The caps are the server's and
  apply either way, so "the document can't be sent, it is larger than 2 GB"
  is an error message, not a premium surface. What went is the way around
  it — the Increase Limit buttons, the comparison bars, the account-switch
  offer.
- **Inbound content is not stubbed.** Because none of it renders, there are
  no recipient stubs to write. This is why the four `createView` overrides
  return nullptr rather than being deleted: `Media::createView` is pure
  virtual.
- **Paid posts vanish without trace**, knowingly.
- Kept: the verified check, and scam / fake / direct badges — warnings, not
  purchases.

## Still to do, in the order I would take it

1. **The upsell entry points — 52 files.** Every "you need Premium for
   this" call: `Settings::ShowPremium` (31 files) and
   `ShowPremiumPreviewBox` (21). Each needs a judgement about what happens
   instead — nothing, a disabled control, or a brief "not available". This
   is the gate on everything below, because `settings_premium.cpp` and
   `premium_preview_box.cpp` cannot go while they have callers.
2. **Premium proper** — `settings_premium.cpp` (2,165),
   `premium_preview_box.cpp` (1,836). Unreachable by any link or settings
   row already.
3. **Business** (7,938), **boosts/giveaways** (6,463), **star referrals**
   (3,561). Seller-side, little inbound coupling.
4. **Credits / Stars / TON** (11,342).
5. **Gifts** (26,488) — last, and the biggest. `history_view_unique_gift`
   and `history_view_premium_gift` are already deleted; what remains is
   `star_gift_box.cpp` (5,354) and friends. `ResolveAndShowUniqueGift` in
   `local_url_handlers.cpp` goes here — stories and the premium section
   still call it, which is why `star_gift_box.h` is still included there.
6. **`premiumCanBuy()` and `premiumPossible()` last of all.**
   `premiumPossible()` is now exactly `premium()`, so every
   `(premium || !premiumPossible)` is provably always true — but there are
   104 references, so do it when the surfaces are gone, not before.

Also still present, noted rather than done: the **bot verification icon**
(`PeerBadge::drawVerified`), an arbitrary server-supplied custom emoji drawn
*before* a name from a path no gate touches — the only coloured emoji left
beside a name; **"Set as status"** in the emoji picker menu, which sets
something nothing here displays; and the **collectible status gradients** in
`ui/top_background_gradient.cpp`, `calls/calls_panel_background.cpp` and
`history_view_about_view.cpp`, which read `emojiStatusId().collectible`
directly and were never gated.

## Traps this job has already sprung

- **Removing an include is a behaviour change.** This broke the build:
  nothing in `unread_badge.cpp` changed, but two headers it had been getting
  transitively went away with the includes that supplied them. There is an
  audit for this at
  `Telegram/build/audit_includes.py` — for every include dropped from a
  surviving file, take the names that header declares, check whether the
  file still uses any, then **discard any name another still-included
  header also declares**. That last step is what turns "`Session` appears 40
  times" into silence: 68 candidates became 5. It found one real bug.
- **`cmake/td_ui.cmake` holds the UI sources**, not
  `Telegram/CMakeLists.txt`. The "four lists, not one" trap in
  `LOOGRIGRAM.md`, live: deleting eleven `ui/effects/premium_*` files needed
  their entries removed from `td_ui.cmake`. What caught it was asserting
  every entry exists exactly once rather than deleting best-effort.
- **A regex collapse ate an `rpl::combine(` opening and left its closing**,
  and mangled the indentation of the call after it — and brace *and* paren
  counts both still balanced, so the cheap checks said fine. Caught by
  reading the result back. Prefer exact-text edits; when a range delete is
  unavoidable, read both sides of the seam.
- **Heredocs mangle Python with `\n` in string literals.** Write the script
  to a file and run it.

## Checks worth re-running after any deletion here

All cheap, all have caught something:

```
# every build-list path still resolves on disk
# every icon named in any .style resolves under Resources/icons
# brace balance on every touched file
# the Class::method definitions lost are exactly the intended ones:
for f in $(git diff --name-only BASE HEAD -- '*.cpp'); do
  diff <(git show BASE:$f | grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' | sort -u) \
       <(grep -oE '^[A-Za-z_][A-Za-z0-9_:<>*& ]*::[A-Za-z_~][A-Za-z0-9_]*\(' $f | sort -u) | grep '^<'
done
```

Brace balance alone is not enough — see the regex trap above.
