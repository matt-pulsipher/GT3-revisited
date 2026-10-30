# Menus: behavior

This page describes how GT3 Revisited runs the game's menus: the page manager
(pages, items, focus, popups), how menu screens are drawn and animated, the
transitions, and the flow between the title menu, GT Mode, arcade mode,
options, the save pages and the Replay Theater. Code comments cite its
sections as `menus §n`.

GT Mode's own rules (prices, entry checks, prizes, licenses, championships)
are on a separate page. This page covers them only where the menus need them.
The menu file formats (`.mbl` flow files, `.pmb` layouts, IMGS sets) are in
`docs/formats/menu.md`, which this page assumes.

Code: `src/menu/` (scene, renderer, text), `src/menu/flow/` (the interactive
runtime), `src/app/screen_gt3menu.cpp` (the menu screen), `src/race/results.*`
and `src/app/screen_race_quick_menu.cpp` (the race quick menus),
`src/race/post_run.*` and `src/app/screen_race_post_run.cpp` (what follows a
run, the race behind the quick menu),
`src/app/online_pickers.cpp` (the online lobby's pickers, which copy the
arcade pickers' motion).

Conventions used throughout:

- **Frames** are 60 Hz menu frames. Every timing below is in frames unless it
  says seconds. The menu screen runs as many fixed 1/60 s steps as real time
  calls for (`Gt3MenuScreen::update`).
- **Menu units**: the layout space is 640 × 448 (the NTSC frame buffer). Its
  origin is at the bottom left and y points up. A box (x, y, w, h) has its
  bottom-left corner at (x, y). The engine shows the frame at 4:3 in the
  middle of the window (`Gt3MenuScreen::render`).
- **SE** means a named sound effect of `sound/se.inf` (§14).

---

## §1 Menu sets, pages and controls

A **menu set** is one flow file `menu/<name>.mbl` plus its layouts
`menu/<LANG>/<name><N>.pmb` (`flow::MenuSet`). The flow file does not depend
on the language. Each flow page names its pmb number N. A page whose name is
not in the flow file is looked up in pmb 1. Layout files load on first use and
then stay cached. A page can name an **import page** (for example
`00cmn/status`, the GT Mode status bar). The import page is drawn under the
page from its own pmb (§4.5). Mods can edit a set's flow and layouts in memory
as it loads (`flow::menu_patches`). GT Mode uses this to add mod cars and
makers to the dealers.

The disc's 24 menu sets resolve completely: all 576 pages are found in their
pmb, and all 2,026 links, back targets and imports exist (test
`menu_flow_disc_targets`, run on the disc).

A **control** is one menu set driven by game code. `flow::GameFlow` runs one
control at a time, with one `flow::MenuController` for its pages:

| Control (set) | What it is |
|---|---|
| `gt_mode` | the title menu (page `root`), the Replay Theater page, and every GT Mode page (map, home, dealers, shops, races, licenses) |
| `arcade` | arcade mode (`ar/...` pages, §8) |
| `option` | the title Options menu (§10) |
| `memorycard` | the save page, the save name entry and the Replay Theater file list (§12, §13) |
| `logger` | the Analyzer (§13) |
| `setting`, `records` | GT Mode car settings and records (§12.4), opened from a race's quick menu |
| `car_get`, `lc_get`, `trophy_get`, `name_entry`, `arcade_mini`, `gtmode_mini`, `consolation_prize`, `not_get_car` | presentations after a result: prize car, license card, trophy, record name entry, race prize pages (§7.6) |

The race quick menus (§11) use the same page manager in the race screen, one
set per layout (`qm_*`).

## §2 Input: logical keys

The menus never read the controller directly. Each frame the pad becomes a
set of **logical keys** (`menu_keys` in `src/app/screen_race.cpp`,
`keys_from_pad` in `src/app/screen_gt3menu.cpp`; both do the same mapping):

| Logical key | Pad |
|---|---|
| Up / Down | D-pad up / down, or the left stick's Y below 0x40 / above 0xC0 (center 0x80) |
| Left / Right | D-pad left / right, or the left stick's X below 0x40 / above 0xC0 |
| Confirm | Circle **or** Cross |
| Cancel | Triangle **or** Square |
| Start, Select, L1, R1 | the buttons of the same name |

Both Circle and Cross confirm and both Triangle and Square go back. There is
no regional swap. The left stick doubles the D-pad. Player 2's pad is read the
same way, but only the 2 Player car page uses it (§9.6).

**Edges and auto-repeat** (`MenuController::tick`). A key "fires" on the frame
it is pressed. Directions, L1 and R1 also repeat while held. The first repeat
comes 31 frames after the press, then one every 4 frames. In 40 frames of
holding Down, the focus moves on frames 1, 32, 36 and 40 (test
`menu_flow_navigation`). All repeatable keys share one counter, and any new
press resets it. Confirm and Cancel never repeat.

While a page change, a confirm feedback or a page build is running (the
controller is **busy**), while a progress popup holds the controls (§3) and
after the menu has finished, no key fires. Keys held through that time do not
count later, because only press edges count.

**Priorities on one frame** (`MenuController::handle_input`):

- Up beats Down, Left beats Right, and a vertical move beats a horizontal one.
- Confirm counts only while no direction is held.
- Cancel counts only while no direction is held and Confirm did not fire.

**Other code-driven pages** use their own edge logic with similar timing:

| Page | Repeat |
|---|---|
| save page, Replay Theater list (`SavePage::tick`, `TheaterPage::tick`) | directions only: the press, 31 frames later, then every 4 frames |
| 2 Player car page, per pad (`GameFlow::two_player_frame`) | directions: the press, then from the 31st frame held every 3 frames |
| Analyzer top page (`GameFlow::logger_tick`) | presses only |

Keys already held when the save page, the Theater list or a race quick menu
opens are ignored until they are released.

**Touch and pointer** (`MenuController::tap`, `hover`, `GameFlow::tap`):

- A **tap** is a left-button or finger press released within 12 UI units of
  where it started (`Gt3MenuScreen::on_event`).
- A tap on the focused item confirms it, the same as Circle or Cross. A tap on
  another item moves the focus there (SE `select`).
- The hit test uses the boxes items are drawn in, scrolling selectors
  included. It tries the last item first, and the point must also lie inside
  the selector's window.
- On the arcade pickers (§9.1), a tap on a strip icon jumps to that entry, or
  confirms if that entry is already chosen. A tap elsewhere acts by thirds of
  the width: the left third is Left, the middle third Confirm and the right
  third Right.
- A key made by a tap is held for one frame only.
- A hovering pointer (mouse or VR controller, not a finger) focuses the item
  under it, so one click then confirms. On a horizontal scrolling row (such
  as every dealer page, test `menu_flow_disc_dealer_strips`), the row then
  holds still (it is **pinned**) until the focus next moves by key.
  Vertical scrolling selectors ignore hovering, because they center the
  focused item by design. GT Mode's code-drawn lists handle hover and tap
  themselves (§9.7), and so does the save page (§12.1).
- A mouse's right click is Cancel, held for one frame (`GameFlow::back`); on
  the title menu it leaves for our main menu, as Cancel does.
- Taps and hovers are ignored while the menu is busy, while a progress popup
  holds the controls (§3), while switching controls or while waiting for
  gameplay.

The touch overlay's menu layout (D-pad and face buttons) is described in
`docs/INPUT.md`.

## §3 Confirm, intercepts and popups

**What Confirm does on the focused item** (`MenuController::confirm`,
`run_item`):

- A **multi item** (§4.3) passes the confirm to its current choice. The chosen
  child item runs steps 2 and 3. Multi items skip the enable test and the
  intercept.
- A **disabled** item stays focusable but refuses: SE `disable`, and nothing
  else happens. Game code decides which items are disabled
  (`MenuController::enabled`).

Then, in order:

1. **Intercept.** Game code may answer with a popup page to open instead of
   running the item (`MenuController::intercept`, filled from
   `GtModeHooks::intercept`). There are two kinds of answer:
   - A **question** (`00cmn/cd_ride_sure`, "ride this car now?") opens with
     the normal confirm feedback (step 3) and then appears as a popup.
   - Any other answer is a **refusal**: SE `disable`, and the popup opens at
     once with no feedback.

   When the intercept answers, the item itself does not run.
2. **Action.** If the item has an action string, an `Action` event goes to
   game code.
3. **Effect** of the item's kind (§4.4), after the page's confirm feedback
   (§5.4).

The main refusals GT Mode uses (`intercept` in
`src/game/gt_mode_menu/prechecks.cpp`)
are listed below. When each one applies is a GT Mode rule.

| Popup | Shown when |
|---|---|
| `00cmn/no_money` | the price is more than the credits |
| `00cmn/max_car` | the garage is full |
| `00cmn/not_for_buy` | the guest car cannot be bought (Trade) |
| `ts/already_you_have`, `00cmn/already_you_have` | the part or wheel is already owned |
| `lc/no_license_<id>`, license and regulation popups | the event's entry rules are not met |
| `not_to_change`, `dirt_tire_only` | car settings: a part change would break the event's rules, or tires cannot change for a dirt event |
| `ar/cannot_ending` | arcade Clear Status "Goal" before an ending is open |

**One popup level.** A popup is a second level drawn over the base page. The
page below stays drawn and keeps its focus. Opening a popup from a popup
replaces it, so closing always returns to the base page. One example is the
tune shop's chain "Purchase?" → "Install parts now?" → "Installed" (test
`menu_flow_popup_replaces_popup`). Popups open in three ways:

- an item of kind 1 (§4.4),
- an intercept,
- game code (`MenuController::popup`, SE `ok`; refused while busy).

Cancel (if the popup page has flag 0x100, §4.4) or an item of kind 3 closes
the popup.

**Progress popups.** A page whose `defaultLayer` has a widget of class
`ignore_control` takes the controls away when it is built
(`MenuController::ignoring_control`). Building another page does not take
them away. Until game code gives them back (`give_control`):

- no key fires and taps and hovers are ignored;
- the page's own buttons (its items) are not drawn, while the rest of the
  page is.

The GT Mode car wash (`at/cw/cw_now`, "Washing . . .") and oil change
(`at/ot/ot_now`, "Changing oil . . .") popups are such pages. Game code gives
the controls back when its animation is over; the "Complete" button then
appears and leads back to the shop. Counted in frames from the shop button's
confirm, the popup holds the controls from frame 30; the wash gives them back
at frame 212 and the oil change at frame 325 (test
`gtmode_menu_disc_wash_and_oil`). What the animations show is GT Mode's. In GT
Mode, a progress popup with no wash or oil change running (for example one
opened directly) gets the controls back once it has settled.

## §4 Pages, items and focus

### §4.1 Building a page

When a page opens (`MenuController::build`), its items are the flow items
whose name matches a page item of the layout, in flow order. Flow items with
no layout element are skipped. Building a base page discards any popup. The
build emits a `PageChanged` event and then blocks input for 10 frames (the
"settle"; measured on the disc: 10).

### §4.2 Initial focus

The first rule that gives an answer decides:

1. The control's own choice for the page (`initial_focus`, set in
   `GameFlow::open_control`), in this order:
   - the arcade top page focuses the game last chosen (Single Race when
     arcade mode is entered from the title);
   - the level page focuses the difficulty last chosen, capped at Hard;
   - the title page `root` always opens on the title focus (§7.1), however it
     is reached;
   - a GT Mode race series or license page opens on the item of the stored
     race code (below);
   - a page opened with an explicit focus uses it;
   - otherwise game code may name the item (`initial_item`): a GT Mode wheel
     shop brand page (`at/ws/ws_...`) opens on the wheel the ride car wears,
     when that page sells it (test `gtmode_menu_disc_garage_wheel_list`).
2. The first item that links to the page we came from. Going back therefore
   lands on the item you entered by, or on the first of several items that
   lead to the same page.
3. The first item.

Nothing remembers the item confirmed last: a page opened again starts by the
same rules (test `menu_flow_initial_focus_rule`).

**Race series and license pages** (`race_page_hook`, `race_page_focus` in
`src/menu/flow/game_flow.cpp`). The `gt_mode` control keeps one stored race
code (§7.4) with its type (race, spot race or championship). Which pages
read it:

| Page | Which ones | Focus |
|---|---|---|
| license page | `lc/lc_<x>` with `go_license` items (6 on the disc) | the code's last digit − 1; 0 if that is outside 0..7 |
| race series | `r/<class>/<series>`, not an `_entry` page, with `set_race_code` items (49) | the code's last digit |
| championship series | as above, with a `set_race_code_champ` item (27) | the code's last two digits as a number, plus 1 unless the stored code is a championship's |

- A missing digit or a negative result gives the first item; an index past
  the list is clamped to its last item.
- On the disc the race information item comes first, then the championship
  item (if any), then the races, so each race's own code gives its own item
  (test `menu_flow_disc_race_page_hooks`, all 403 race items).
- Page names must be lower case: the nine endurance series pages spelled with a
  capital E (such as `r/end/Eakasaka_n`) are not hooked and use rules 2 and 3.
- Every build of a hooked page reads the stored code and clears it; closing
  a popup builds nothing. So the first visit opens on the first item (the
  race information). Coming back from a race or from the race's entry page
  opens on that race. After the race information popup, the next build of a
  hooked page opens on its race. A later visit with nothing stored again
  opens on the first item (test `gtmode_synth_race_page_focus`).

### §4.3 Moving the focus

- **Axis and ends.** Page flag 0x001 makes the list horizontal (Left / Right
  move the focus). Otherwise Up / Down move it. Flag 0x002 wraps at the ends;
  otherwise the focus stops at the ends without a sound.
- **What a move does.** SE `select`, the focus pulse restarts (§5.3), and the
  item layers follow (§4.5).
- **Multi items.** An item whose name starts with `multi`, `free` or
  `concurrent` and whose kind is 4 with children is a multi item: a choice
  such as ON / OFF. Bit 0x10 of the item's kind field makes its children
  horizontal, and bit 0x20 makes the choice wrap (otherwise it clamps).
  - A focused multi item handles its own axis first. A change of choice plays
    `select` and emits a `Choice` event with the child's action.
  - If the choice runs along the same axis as the page, the outer list does
    not move.
  - Choices are remembered per page and item for the whole session. Game code
    can preset them; the options pages do this from the stored options.
- **Free rows** (the name entry's key rows, items named `free...`) keep the
  column when the focus changes row. Keys are 24 wide at a 28-unit pitch. If
  the last move was Left / Right, or the old row has at least as many keys as
  the new one, the new key is

  `key = clamp(⌊centre / 28⌋, 0, keys − 1)`

  where center = 28 × (old key) + 12, or 223 for a one-key row. Otherwise the
  new row keeps the key it last had.

### §4.4 Item kinds and cancel

The low nibble of an item's kind field selects what it does after the
feedback:

| Kind | Effect |
|---|---|
| 0 | go to the linked page (a page change, §6) |
| 1 | open the linked page as a popup |
| 2 | finish: the menu ends with result 1 and a `Finish` event carrying the item's action and index; game code takes over |
| 3 | back: as Cancel, but with no cancel sound (the feedback's sound has played) |
| 4 | a multi container (nothing on its own) |

**Cancel** works only on pages with flag 0x100 (`MenuController::cancel`):

- With a popup open, it closes the popup (SE `return`).
- On a page with no back page, it emits the page's back action (if any), then
  `Exit`. The menu ends with result 0 (SE `return`).
- Otherwise it plays SE `cancel`, emits the back action (e.g. `commit`), and
  changes page to the back page. A back page of `return` means "the base
  page we last left by a kind-0 link".

**Pages without items** are driven by game code. Confirm emits a `Confirm`
event. Cancel emits a `Cancel` event, or follows the rules above if the page
has flag 0x100.

After an `Exit`, game code may **resume** the same page (`resume()`). The
arcade car page does this to step back from its sub-steps (§8).

| Event | When |
|---|---|
| `PageChanged` | a page (base or popup) was built |
| `Action` | an item with an action string was confirmed (before its effect) |
| `Choice` | a multi item changed its choice |
| `BackAction` | a cancel ran the page's back action |
| `Finish` | a kind-2 item finished the menu |
| `Exit` | cancel on a page without a back page |
| `Confirm`, `Cancel` | keys on a page without items |

### §4.5 Layers shown with the focus

A page always shows its `defaultLayer`. If the page has an import page, that
page's `defaultLayer` is drawn first. Each item can name two more layers,
which are shown while it has the focus (`MenuController::refresh_layers`):

- the **plain layer** (`.mbl` item +0x080), which appears and disappears in
  place;
- the **sliding layer** (item +0x040). It enters from the direction of the
  move: from 448 units above when moving up, 448 below when moving down,
  640 to the left when moving left and 640 to the right when moving right. The
  old layer leaves the opposite way. Both move on a spring (§5.2) for 31
  frames.

A layer that is dismissed fades out over 9 frames if its elements carry the
fade-out flag; otherwise it vanishes at once. When a page is built, nothing
slides. Game code can force any layer on or off (`set_layer`); presentations
use this, for example the trophy's message layers.

### §4.6 Scrolling selectors

A page whose items are all parked off-screen, to the right (x ≥ 640) or below
the screen (top edge y + h ≤ 0; the Toyota dealer list parks four of its cars
there), and that has a window widget is a **scrolling selector**
(`scroller_window`). The window widget is the first of `menu_top` (the title
menu), `menu_car_goods` (dealer car lists), `menu_dealer` (the England and
Germany dealers) and `menu` that the page has. Every car dealer page (the
countries' maker lists and the makers' car lists, 45 pages) is such a strip,
with the focused tile inside its window (test `menu_flow_disc_dealer_strips`).
The list places the items itself:

- **Vertical:** the focused item sits in the middle of the window. The other
  items stack above and below at the spacing of the first two items' layout
  positions (32 units if unknown).
- **Horizontal** (page flag 0x001, e.g. GT Mode league and series pages, the
  dealers' maker and car lists, and the race quick menus, whose icons are
  parked above the screen): the items
  form a row with 4 units between them.
  - If the whole row fits in the window, it is centered.
  - Otherwise, near the start it is left-aligned (while the focused item's
    left edge is less than half a window width from the row's start), near
    the end it is right-aligned (while less than half a window width follows
    the focused item), and in between the focused item is centered.
  - The row's offset in the window eases towards the offset for the focused
    item by 30 % of the remaining distance per frame. A page that is built,
    or a focus set by game code, places the row at once.
  - A `scroll_bar` widget draws nothing at all, track included, while the
    row is no wider than the bar. Otherwise it draws its track, and the
    page's hidden `pinch` element over it: as wide as window width × bar
    width / row width, at bar x − offset × bar width / row width, where the
    offset is the row's start from the window's left edge as it eases. On
    the disc, 113 pages are horizontal selectors; only the three GT Mode
    league pages (`r/beginner/beginner`, `r/amateur/amateur`, `r/pro/pro`,
    with 20, 23 and 22 event tiles) have a row longer than their bar (tests
    `menu_flow_horizontal_scroll_bar`, `menu_flow_disc_league_scroll_bars`,
    `menu_flow_disc_horizontal_selectors`).

The scroll position (in items) eases towards the focus by 30 % of the
remaining distance per frame; it places the vertical lists. A confirm
feedback waits until it is within 1/32 of an item of the focus (a row pinned by a hovering pointer does
not wait). Items are clipped to the window.

On the title list (window `menu_top`) the neighbors fade with their distance
d from the window center:

`alpha = 1 − d / (1.8 × item height)`, and 0 beyond that.

The focused item is always fully drawn.

Page items that the flow does not list (and that are not choices of a multi
item) are drawn as static art, unless the page is a selector or the item is
parked off-screen.

**Code-drawn lists** (§9.7) use the same button-list placement
(`menu::flow::row_layout`, `column_layout`, `ease_scroll`):

- **Rows** (the wheel shop's tiles, the garage's wheel list) are placed as a
  horizontal row above, 4 units between buttons, with the same easing and
  scroll bar.
- **Vertical lists** (the garage and trade lists, Cars Permitted, the
  records page) stack the page's row 4 units apart from the top of their
  window. A list that fits the window stays at its top (it is not
  centered), and its `scroll_bar` draws nothing, track included. A longer
  list is held at the window's top while the focused row starts less than
  half a window down the list, at the window's bottom while less than half a
  window follows the focused row, and in between the focused row is
  centered. Its offset eases 30 % per frame towards that place, it is placed
  at once when its page opens, and the rows are clipped to the window. Its
  scroll bar (the garage, trade and Cars Permitted pages have one; the
  records page has none) then shows the track and the `pinch`, as high as
  window height × bar height / list height and as far down the bar as the
  list has scrolled, times bar height / list height (tests
  `menu_flow_column_layout`, `gtmode_menu_disc_vertical_lists`,
  `gtmode_synth_garage_list_scroll`).

## §5 Drawing

### §5.1 Draw order and element parts

Each frame the controller builds a list of elements to draw
(`MenuController::draw_list`), drawn back to front by z. Elements of the popup
level add 1,000 to their z, so a popup covers its base page. Full-screen fades
are at z 10,000 and the decide glow (§5.4) at 100,000.

An element's parts are drawn as in `docs/formats/menu.md`: the image
modulated by the four corner colors (alpha 0..255), then the text. The
layout lists the corner colors from the bottom-left (bottom-left,
bottom-right, top-right, top-left; menu space is y up), and the renderer
paints them on those corners (`MenuRenderer::draw_elements`). Text
colors use the GS alpha scale, so 0x80 is opaque. The runtime can change, per
element:

- the offset, a scale about the box center, the alpha, a brightness (0.5 is
  the disabled look), a per-channel tint and per-corner alpha;
- replacement corner colors, in the same order, modulated as usual (the GT Mode oil gauge
  blends two elements' colorings this way);
- an additive second pass (over-bright highlights), a replacement box and a
  clip rectangle;
- the text (code-filled widgets) and the text color;
- whether the element's own art is skipped (`MenuRenderer::draw_elements`).

The renderer also draws four primitives with no element behind them:

| Primitive | What it draws |
|---|---|
| additive gray quad | the focus highlight of items that have no focused look |
| glow strip | the decide effect (§5.4) |
| flat fill | full-screen fades |
| ring segment | the car page gauges (§9.3) |

### §5.2 Fades and springs

- **Element fade-in.** Elements with the fade-in flag (element flags bits
  4–7) go from alpha 0 to 1 linearly over 24 frames after their page or layer
  appears (test `menu_flow_disabled_and_fade`).
- **Element fade-out.** Elements with the fade-out flag (bits 8–11) fade out
  over 9 frames when their layer is dismissed.
- **Springs.** Sliding layers, the arcade pickers' cross and the car page's
  steps all use one integrator per frame, with k = 600 and damping c = 30:

  v ← v + k (target − x) / 60;  v ← v − c v / 60;  x ← x + v / 60

### §5.3 Focus look

Items have a focus style (element type bits 12–13): pulsing (0x2000), static
(0x1000) or none.

- **The pulse value** p is 0.25 right after the focus changes. It then falls
  by 1/60 per frame and jumps back to 0.5 below 0, so it is a 30-frame
  sawtooth from 0.5 to 0. Only the top level pulses; items of the page under
  a popup keep 0.25.
- **Items with a focused look** (the element's alternative state) show that
  look. With the pulsing style, the normal look is drawn over it at alpha
  2 (0.5 − p), which makes the highlight breathe. With the static style the
  focused look shows fully.
- **Where the focused look is drawn.** At the item's own place: its layout
  position, or where a scrolling selector (§4.6) puts it, at the focused
  look's own width and height. It takes the item's place in the draw order
  (the item's z), and the pulsing normal look goes just above it. The focused
  look's own x, y and z in the layout are not used. Many of the disc's
  focused looks lie elsewhere in the layout: shifted (the GT Auto page's
  Wheel Shop highlight is 10 units right of its button), parked off-screen
  (the dealer car pages' Spec), at 0, 0 or at another z. All 1,454 buttons with a
  focused look in the disc's 24 sets (1,199 of them placed elsewhere) draw it
  once, on themselves (tests
  `menu_flow_highlight_on_its_item`, `menu_flow_disc_highlights_on_their_items`).
  The chosen child of a multi item is drawn the same way.
- **Items without a focused look** get an additive gray quad of level p
  (pulsing) or 0.5 (static).
- **A `focus` widget** on the page replaces that quad. It is drawn centered on
  the focused item at alpha min(1, p + 0.5).
- **Disabled items** are drawn at half brightness, or under the page's
  `disable` widget if it has one (a focused one's highlight then at half
  brightness under it; test `menu_flow_disabled_overlay`).

### §5.4 Confirm feedback

Page flags 0xC0 choose what a confirmed item does before its effect runs
(`start_feedback`). With feedback the SE is `next`; without it, `ok`.

| Flags | Name | Frames | Look |
|---|---|---|---|
| 0x00 | none | 0 | the effect runs at once |
| 0x40 | decide glow | 26 | an additive white strip over the item: center alpha 0.4 × clamp(f/3) up to frame 11, then 0.4 × clamp(1 − (f − 11)/6); edge alpha 0.4 × clamp((f − 4)/6) up to frame 18, then 0.4 × clamp(1 − (f − 18)/6) |
| 0x80 | flicker | 30 | the focused look, with the normal look over it at alpha 1 − q[f mod 4], q = (0.4, 0.6, 0.8, 1.0) |
| 0xC0 | blink | 30 | the focused look at alpha × q[f mod 5], q = (1, 0.8, 0.6, 0.4, 0.2) |

Here f is the feedback frame, counted from 1. The feedback does not start
until a scrolling list has settled (§4.6). The title menu (flags 0x0A2) uses
the flicker, and confirming an item there takes 30 frames before the control
switch starts (measured on the disc).

### §5.5 Text

Text is drawn with the game fonts (`menu::make_game_font_drawer`). When the
fonts are missing it falls back to SDL's debug font at the font's pixel size.
The low nibble of the text style's align field picks a 3 × 3 anchor in the
element's box (0 top-left … 4 center … 8 bottom-right; values above 8 count
as center). Two align flags animate text (`apply_text_effects`):

- **0x10, marquee.** The text enters from the box's right edge and scrolls
  left by one box width every 240 frames, clipped to the box. It restarts once
  it has passed completely. The period is (box width + text width). The text
  width is estimated as bytes × font size × x-scale × ½ rather than measured,
  so it is approximate.
- **0x20, typewriter.** One more character appears each frame, up to 255. A
  two-byte character (EUC-JP, or an ISO-8859 escape) counts as one.

### §5.6 Animated widgets

| Widget class | Behavior |
|---|---|
| `blink` | alpha over a 32-frame cycle: (f + 1)/6 for f < 6, 1 at f = 6, then 1 − (f − 6)/24 (floored at 0) |
| `diagonal_in` / `diagonal_out` | the arcade pages' logo pair, a 10 s cycle. `in` is hidden for 2 s; its top-left and bottom-right corners rise over 0.5 s, then the other two over the next 0.5 s; it shows for 4 s and goes in reverse order. `out` is the complement, corner by corner |
| `mpeg`, `mpeg_course`, `mpeg_class` | a movie drawn by the menu screen. The widget's corner colors modulate the movie (GS scale, 0x80 = 1) instead of drawing a quad, and their alphas blend it into the page: the arcade car pages' `mpeg` (alpha 0 at its bottom corners) fades out at its bottom edge |
| `focus`, `disable` | overlays for the focused or disabled item (§5.3) |
| `scroll_bar` | the visible share of a horizontal row or a code-drawn list; nothing while the row or list fits (§4.6) |

### §5.7 Text inset

Text that is left- or right-aligned in its box is drawn 12 menu units in from
the box's edge (`menu::kTextInset`, `menu::apply_text_inset`): +12 for the
left column of anchors and −12 for the right column. Centered text is
unchanged. The engine applies the inset where its screens were checked
against the game's:

- the record and save name entry pages (`GameFlow::draw_list`);
- the race quick menus (`src/race/results.cpp`);
- the GT Mode garage car pages `hm/hm_car*`, for the page's own texts only
  and not the status bar;
- the regulated-car list of `00cmn/car_reguration` (`extra_draws` in
  `src/game/gt_mode_menu/extra_draws.cpp`).

Other pages draw text at the box edge. The name entry's caret uses the same
inset to sit right after the text (§12.3).

## §6 Transitions

### Within a control (page changes)

A kind-0 link or a cancel to a back page runs these steps
(`MenuController::perform`, `tick`):

| Step | Frames | Input | Drawn |
|---|---|---|---|
| confirm feedback (§5.4) | 0, 26 or 30 | blocked | the page with the feedback |
| fade to black, only if the page has flag 0x008 | 16 | blocked | black over the page, alpha min(1, n/15) |
| teardown | 2 | blocked | nothing (the old page is gone) |
| build and settle (§4.1) | 10 | blocked | the new page |
| fade from black, only if the new page has flag 0x004 | 30 (from the build) | allowed after the settle | black, alpha 1 − n/30 |

Popups skip the teardown: a kind-1 item or an intercept builds the popup
right after the feedback.

### Between controls (loading transitions)

When the flow leaves one control for another (`GameFlow::switch_control`) or
starts gameplay (`request_race`), the menu control ends with the **default
transition type 0, Fadeout**. The engine keeps the last menu frame and draws
the transition over it for **40 frames**. By then the Fadeout has darkened
the picture to about 2 %. Then the next control (or the movie) appears as a
**cut**, with no fade-in. Tests confirm that leaving the title for arcade
mode starts a Fadeout (`menu_flow_disc_title_to_time_trial`), and the probe
measured 40 frames to the arcade top page.

**While a race loads** (`flow::LoadingPicture`, `race_loading_picture`). When
the menus start a race, the menu screen hands its last frame and the
transition running on it to the race screen. The race screen keeps drawing
that transition over that frame, every frame, for as long as the race takes
to load, so the picture keeps darkening and the loading indicator (below)
appears after 3 s. The race then appears as a cut. The engine's load blocks,
so the race screen catches up the frames the load took: one frame of the
transition per drawn frame at least, else one per 1/60 s of real time, at
most 600 (10 s) at once (`LoadingPicture::frames_for`, test
`race_loading_picture_frames`). A race started with no menu frame handed over
shows a Fadeout over black instead.

**Leaving a run** (race → what follows the run, §11, or the menus). When the
race loop ends after a run, or on EXIT from the pause menu of a live run
(`RaceScreen::pause_exit`, unless that EXIT counts as the run's finish,
§11.2), the last race frame stays up with nothing simulated for 10 frames.
Then the DelayFadeout's gray wash (type 1 below) builds up over it for 8
frames: gray 0.1 (12.8 / 255) at alpha 1 − 0.5^(n − 10) after n frames
(`ScreenTransition::delay_fadeout_cover`, test `race_exit_wash`). Then what
follows (§11.1: the prize page, the name entry, the replay, the quick menu, or
the menus) appears as a cut. The same transition runs over the last frame of
every replay that goes back to a quick menu, at its end or on its pause
EXIT, before the quick menu cuts in. EXIT from the pause menu of a Replay
Theater run, an online run or the attract demo leaves at once, without the
wash.

Changes made with `open_control` are cuts. Most stay in the same control,
such as entering the GT Mode map from the title (same `gt_mode` control) or
stepping through the arcade pickers. A few change control as a cut: the car
settings' Load / Save settings page and the way back, the Analyzer's return
from its file list, and the presentations (§7.6).

**The four transition types** (`ScreenTransition`) are drawn every frame on
top of the previous picture without clearing, so their effects accumulate.
The flow only ever asks for type 0. Type 1's wash is what a run's exit draws
(above); types 2 and 3 are implemented but nothing uses them.

| Type | Name | Per frame | Covered after |
|---|---|---|---|
| 0 | Fadeout | color (12, 12, 12) at alpha 0x0C / 0x80 over the whole frame (the picture keeps 0.906 of itself each frame) | 48 frames (≈ 1 % left) |
| 1 | DelayFadeout | nothing for 10 frames, then color (0.1, 0.1, 0.1) at alpha 0.5 | 18 frames |
| 2 | Lineout | opaque black 1-unit columns, every other one, sweeping in from both edges at 16 units per frame (even columns from the left, odd from the right) | 21 frames |
| 3 | Combout | 256 bars, each 2 units high, growing from the left edge at 640/24 units per frame, color 32/255 at alpha 0x60 / 0x80; each bar starts at a random position in [−320, 0) | 36 frames |

The Combout start positions come from a linear congruential generator seeded
with 100. The pattern is a stand-in and not the game's own sequence.

**Loading indicator.** 3 s (180 frames) after any transition starts, a
16 × 16 square at (20, 20) from the top-left blinks between light gray (0.9)
and black every 32 frames. DelayFadeout's first 10 frames draw nothing at
all, not even the indicator.

## §7 Game flow

### §7.1 The title menu

The game starts on `gt_mode` page `root` with the focus on Arcade
(`GameFlow::start`). The title list is a vertical scrolling selector (§4.6)
over the looping title movie `stream/title_full.pss`, with no menu music.

| # | Item | Action | Goes to |
|---|---|---|---|
| 0 | Arcade | `go_arcade_menu` | the arcade control, `ar/top` (§8) |
| 1 | Gran Turismo Mode | `go_sitemap` | the GT Mode map, same control (a cut) |
| 2 | Replay Theater | (link) | page `replay_theater`: Load Replay, Demonstration, Analyzer (§13) |
| 3 | Load Game | `go_load_game_menu` | the save page, Load game (§12) |
| 4 | Save Game | `go_save_game_menu` | the save page, Save game |
| 5 | File Operation | `go_file_operation` | the save page, Files |
| 6 | Options | `go_option` | the option control (§10) |

The title page always opens on the **title focus**, which the flow sets when
a title command runs (`title_focus_after`), not when the focus moves on the
title. Coming back to the title therefore focuses the item of the mode you
return from (test `gtmode_synth_title_focus_on_return`):

- Arcade 0 (`go_arcade_menu`).
- GT Mode 1, both from the map's cancel and from the `go_top` back action.
- Replay Theater 2: after Load Replay (`go_replay_theater`), and on Cancel
  from the `replay_theater` page, whose back action
  `set_focus_replay_theater` sets it.
- Load Game 3, Save Game 4, File Operation 5, Options 6.
- Any other command (Demonstration, Analyzer), an attract demo and the boot
  set Arcade 0.

The title page has no cancel flag, so Cancel does nothing inside the menu. In
the app, Cancel or Select on the idle title page (or the "GT3R menu" button
beside the picture) leaves the game's menus for the engine's own main menu
(§15). Actions the engine does not implement are logged on screen and the
page reopens.

### §7.2 Attract mode

On the title page (base level, idle), the frames with no key held are
counted. When the count passes the title idle time, attract mode starts. The
idle time comes from the disc's game configuration and is 20 s, or 1,200
frames, on this disc (measured).

- **Demo races.** Attract mode runs demo races one after another (mode
  `attract_demo`). Each is an all-CPU race: the computer drives every car,
  the player's slot too, and nothing is recorded. They cycle through the
  configuration's demo list. Demos
  marked for the S license (the F1 demos) are skipped until the S license is
  held.
- **When a demo ends on its own,** the next demo starts, up to five. After
  the fifth, the opening movie `stream/opening.pss` plays, then the title.
- **A demo that does not finish** (the player pressed a key) ends attract
  mode and returns to the title. Starting a demo sets the title focus to
  Arcade, so the title comes back on Arcade, also after the opening movie.

### §7.3 Music and movies per page

Each **base-level** page build picks the menu music (`page_music`). Popups do
not change it.

- **The option set** plays no music: every options page stops it. The music
  list's previews are the only music there (§10).
- **Other sets** pick by page name (table below).
- **No match** keeps the current music.

| Page | Music (`music.inf` sequence) |
|---|---|
| `cd/...`, `00cmn/cd_ride_sure` | `dealer01` |
| `at/...`, `00cmn/ws_fit_sure` | `wash01` |
| `hm/...` | `home01` |
| `lc/...` | `license01` |
| `r/...`, `00cmn/race_info` | `gorace01` |
| `replay_theater` | streamed track 26 (0x1A, the "Are You Gonna Go My Way" remix), looping |
| `ts/...` | `tune01` |
| `map` | `gtmenu01` |
| `mt/...` | `machtest01` |
| `ar/...` | `arcade01` |
| `root` | none (music stops) |
| `license_get` | streamed track 33 ("champ-2") |
| `trophy_get` | streamed track 24 ("champ-3") |
| `car_get` | streamed track 25 ("getacar"), played once |
| `arcade_mini`, `gtmode_mini`, `consolation_prize` | streamed track 23 ("jg05") |

Menu music, sequenced or streamed, plays at the menu level (`bgm::kMenuVolume`,
music §6). The streamed presentation tracks stop when the presentation
closes. The Menu BGM option (§10) silences all menu music (not the music list's
previews). The race plays its own music, so the menus stop theirs when
gameplay starts.

Background movies are also set on base page builds and loop until changed:

| Page | Movie |
|---|---|
| `root`, `replay_theater` | `stream/title_full.pss` |
| `ar/top` | `stream/arcade_menu_full.pss` |
| `ar/level` | `stream/level_menu_full.pss` |
| `ar/select_car`, `ar/select_car_2p` | `stream/menu_bg2.pss` |

On every other page the movie stops. The course and class pickers then start
their entry's movie once the cursor rests (§9.1).

### §7.4 GT Mode (as far as the menus go)

- **Entering.** Gran Turismo Mode opens page `map` in the same control (a
  cut). If the save holds a suspended championship, its round starts at once.
- **The map** is driven by a pointer, not the list order (§9.7). Cancel on the
  map, or the back action `go_top`, returns to the title with the focus on
  Gran Turismo Mode.
- **Race codes.** The flow keeps one stored race code and its type, which
  the race series and license pages read for their focus (§4.2):
  - `set_race_code` (type race) and `set_race_code_spot` (type spot race)
    store the confirmed item's name;
  - `set_race_code_champ` stores the name with its last character advanced
    by one (the championship's code), type championship;
  - `set_race_code_info` (the race information) stores the item's name
    without its last `_…` part, which is the race's code, and keeps the type;
  - `go_license` and `go_machine_test` store the item's name;
  - `go_free_run` sets `ATAF0001`, and `clear_race_code` clears it;
  - an arcade Single Race, Time Trial or Free Run stores its race code (§8).
- **Starting gameplay.** Finish actions `go_race`, `go_license`,
  `go_machine_test` (whose race code is the item name), `go_free_run` and
  `go_preview` raise a race request (§7.5). Afterwards the menus resume on the
  same page, except that a race entry page (a name ending in `_entry`)
  resumes on its back page, the race list.
- **Save pages.** `go_save_in_home` opens the save page (Save game) and
  returns to the home page. `go_trade` opens the save page as "Trade" to load
  a friend's garage, then shows its cars on `hm/hm_gr_trade`; leaving that
  page returns to `hm/hm`.
- **Game code.** Every GT Mode event also goes to game code first
  (`GtModeHooks::on_event`). If game code consumes a Finish, the page is
  rebuilt with the focus on the item. Game code also fills text widgets
  (credits, day, names, prices), draws code-made content (lists, marks, 3D
  cars) and steps once per frame (`on_frame`).

### §7.5 Starting gameplay and coming back

When the menus reach a point where the game starts a race, a movie or a
replay, the flow raises a **request** (`flow::GameRequest`):

- the mode;
- the race code;
- the course, car, class and paint, plus the arcade transmission and tire
  choices;
- player 2's choices;
- for GT Mode, the garage car's parts and settings (the race builds the car
  from them) and its opponents; the same for player 2's garage car;
- the replay files.

The request comes after the 40-frame Fadeout (§6). The menu then stops
ticking until gameplay returns (`waiting_for_game`). When gameplay returns
(`GameFlow::game_finished`), the flow takes the posted result
(`flow::GameResult`) and continues:

1. **Replay Theater or Analyzer runs** go straight back to their page, with
   no result to process.
2. **Attract demos** continue as in §7.2.
3. **Arcade results** go to the arcade result handler (Single Race
   progress; no records, which the race entered itself, §11). The Single
   Race cup is queued here only when the race screen has not already shown
   it before the replay (`GameResult::prize_shown`, §11.1). Any presentation
   queued runs, then `ar/top` reopens with the same game focused.
4. **GT Mode results** go to the result handler (prizes, medals, odometer).
   Then, in order:
   - the queued presentations, one after another (§7.6);
   - the ending movies that are due, one after the other (§10 of the GT Mode page);
   - the next championship round, which starts straight away without
     returning to the menus;
   - otherwise the page the race was started from reopens.

If nothing handles a request, the menu screen goes straight back to the menu.

**A race that cannot be built.** If the race screen cannot set up the race
the menus (or the online lobby) asked for, it shows a black page reading "The
race cannot start." with the reason under it. Cross, Circle, Triangle or
Start returns to the menus (`RaceScreen::render`, `RaceScreen::update`).

### §7.6 Presentations

After a GT Mode or arcade result, game code can queue **presentations**:

- the record name entry (queued by the race screen as a run is recorded,
  §11);
- the license card (`lc_get`);
- a prize car (`car_get`);
- the championship trophy (`trophy_get`);
- the race prize pages (`gtmode_mini`, `arcade_mini`, `consolation_prize`),
  for a GT race, championship round or won arcade race queued by the race
  screen before the run's replay (§11.1);
- the "no car" page (`not_get_car`).

Each opens its own set as a new control, with the layers game code asks for
forced on. Each ends with its OK button: a kind-3 or kind-2 item, or Exit.
The presentations play one after another. The race screen can also run the
queued presentations on their own (`GameFlow::start_presentations`). What the
pages show and when OK becomes available are GT Mode rules.

## §8 Arcade mode

`ar/top` lists the arcade games. Confirming one (kind 2) chooses a list of
steps, and each step is a page:

| Item | Action | Steps | Race |
|---|---|---|---|
| Single Race | `single_race` | `ar/level` → `ar/select_course` → `ar/select_class` → `ar/select_car` | mode `single_race` with the race code below |
| Time Trial | `compulsory` | `ar/select_compulsory` | mode `time_trial` (the race code follows from the challenge) |
| Free Run | `time_attack` | `ar/select_course` → `ar/select_class` → `ar/select_car` | mode `free_run`, race code `ATAF0001` |
| 2 Player Battle | `two_player_battle` | `ar/select_course` → `ar/select_car_2p` | mode `two_player_battle`, race code `A2P0001` |
| Clear Status | `goodies` | `ar/goodies` | none; Goal opens `ar/select_ending` as a popup |
| Load Garage | `load_guest_garage` | the save page as Load Garage, then back to `ar/top` | none |
| i.LINK Battle | `i_link_battle` | the engine's online lobby instead of the link-cable pages (`docs/dev/ONLINE.md`); `ar/top` reopens when it closes | none |

Cancel on `ar/top` returns to the title with the focus on Arcade. Each
confirmed step opens the next. Cancel on a step goes back one step (SE
`cancel`), and from the first step back to `ar/top`. After the last step the
race is requested. After the race, `ar/top` reopens with the same game
focused (test `menu_flow_disc_title_to_time_trial`).

**Difficulty** (`ar/level`): Easy, Normal or Hard. Confirming Hard while L1
and R1 are both held selects **Professional** instead, with no unlock needed.
While L1 + R1 are held, the Hard button shows the "pro" art and the
Professional description replaces Hard's. The probe on the disc confirmed
that Hard with L1 + R1 gives difficulty 3.

**Single Race code.** The code is

`A` + level letter + class letter + `T0001`

where the level letter is E, N, H or P (Easy … Professional) and the class
letter is C, B, A or S. A dirt class gives `A` + level letter + `AD0001`. A
garage car may replace the code with one picked from its power and weight
(`arcade_prepare`). Examples: Easy class B is `AEBT0001`; the probe's
Professional class A is `APAT0001`. The final code becomes the flow's stored
race code (§7.4).

**Car page steps** (`ar/select_car`):

- **Step 1** chooses the car and its paint.
- **Step 2** chooses AUTOMATIC / MANUAL. It starts on the Options menu's
  transmission setting.
- **Step 3** chooses RACING / DRIFT tires. Step 3 is skipped for the home and
  guest garage classes and on a dirt course.

Confirm on step 1 or 2 opens the next step (SE `next`). Left / Right choose
within a step and clamp at the ends: SE `cursor` on a change, `disable` at an
end. Cancel steps back within the page (SE `cancel`). Confirm on the last
step requests the race. Each step's choice is kept while the arcade runs.

**Endings** (`ar/select_ending`): the two endings play
`stream/ending_short_full.pss` and `stream/ending_long_full.pss` (the
`normal` item plays the long one, and is disabled until the long ending is
open, a GT Mode rule). `ar/top` follows afterwards.

## §9 Code-drawn screens

Some pages have no list items of their own. Game code draws them from the
page's `hidden` layer art. These pages are the arcade pickers, the 2 Player
car page, the showrooms and several GT Mode screens.

### §9.1 Arcade pickers

The pickers are `ar/select_course`, `ar/select_class`, `ar/select_car` and
`ar/select_compulsory` (`GameFlow::build_carousel`, `animate_carousel`,
`draw_list_impl`).

**Entries.**

- Courses, classes and time-trial challenges are the hidden elements named
  `<id>_logo` that have a matching `<id>_doc`.
- Cars are the hidden pictures `<car>` that have a strip icon `<car>_mini`.
- Game code then removes entries that are not offered and puts the rest in
  the game's order (`arcade_entries`). The examples come from a new game on
  this disc:
  - Single Race courses: `super`, `midfield`, `smtnorth`, `smtsouth`,
    `trialmt` and `r_midfield`, in that order;
  - classes: C, B, A, S, Dirt and the home garage;
  - 10 cars for class C.

  Without game code, every entry in the layout appears (36 courses, 232 cars).
- **Remembered choice.** A picker opens on the entry chosen last time.
- **Class page on dirt.** If the remembered class cannot be used on this
  course, the class page moves the focus: on a dirt course to Dirt, and on a
  road course from Dirt to C.

**Moving.**

- Left / Right move the choice, with SE `select`.
- The class list wraps. The course, car and time-trial lists stop at their
  ends with SE `disable`, and their end arrows (`arrow_l`, `arrow_r`) are
  hidden there.
- An entry that cannot be chosen (for example a road class on a dirt course)
  is crossed out on the strip (`x`). Its reason text replaces its description,
  with a `disabled` mark, and confirming it plays `disable`.
- Confirm stores the entry and moves to the next step (SE `next`).

**The cross and the art.**

- A band runs across the screen at the `cross_bar` element, with a vertical
  bar that sweeps sideways. Both carry the entry's art (logo, description,
  picture, badges).
- Each move sends the cross 680 units sideways on the spring of §5.2.
- The new art starts 680 units to the side and fades in over 24 frames. The
  old art slides away with the cross and fades out over 16 frames.
- The vertical bar repeats with a 680-unit period.
- Game code adds badges and values that move with the art (`arcade_parts`):
  - the course area, surface and length;
  - the car's drive badge, power and torque (shown at 0.67 scale);
  - the time trial's top three records, power and weight.

  Time-trial values sit 22 units right of their labels. Each challenge's
  full-screen picture is swapped in place.

**The strip.** Every entry's `_mini` icon appears in a row along the strip
element, with 4 units between icons. The row is placed like a horizontal
selector (§4.6) inside the strip's width centered on the screen, and eases
30 % per frame.

- **Focused icon.** It pulses: its brightness factor b starts at 1 on a move,
  falls by 1/15 per frame and wraps to 1 below 0.25. The icon is drawn at
  b / 0.502 of its normal brightness, so up to twice as bright. The part above
  normal is an additive second pass.
- **Entering.** The strip glides in from the left when the page opens.

**Movies.** The course and class pages show the entry's still picture
(`stream/course.ipic`). When the cursor has rested 6 frames, the entry's
looping movie `stream/<id>_full.pss` replaces it. The guest garage has no
movie.

**Breadcrumbs** (Single Race only). Small icons from `ar/top`'s hidden layer
show the choices so far at the top left, 66 units apart starting at x = 20:
the difficulty, then the course's area, then the class. The newest one slides
in from 640 units to the right over 15 frames.

### §9.2 The car page

The car page shows the car itself in 3D, centered above the strip (§9.5).
Around it:

- **Paint column.** Up / Down choose the paint. The choice clamps (`cursor`
  on a change, `disable` at the ends), and a new car starts at paint 0.
  - The column shows the current paint's chip, one slot above and five below,
    18 units apart, each chip in light-grey outlines. Chips are clipped to
    that band.
  - The column eases to the current paint by 30 % per frame.
  - Up and down arrows show when more paints lie that way.
  - The paint's name appears under the column, except that a name starting
    with `-` shows nothing.
  - A garage car shows only its own paint.
- **Spec ticker** (`flow_spec`). A new label/value line starts every second
  and cycles through the car's figures (`arcade_specs`: Length, Height,
  Width and Wheelbase, in millimeters). The ticker also runs on the
  time-trial page. Each line fades in over 0.5 s, holds,
  and fades out over 0.5 s within 4 s, drifting up 40 units per second. The
  text is at 0.6 scale, with the value 14 units below the label.
- **Step buttons.** AUTOMATIC / MANUAL and RACING / DRIFT (§8) are the
  layout's `auto`/`manu` and `racing`/`dri` buttons. The `_yes` art marks the
  current choice and `_no` the other, centered at x = 220 and 420.
  - On each step the whole page springs up by 488 (step − 1) + 134.4 units, so
    the step's buttons sit in the cross band.
  - The strip fades out over 0.5 s while a step is open.
  - The background movie blurs: a 3 × 3 box of offset copies with a radius
    that grows from 0 to 2 units at 4 units per second.

### §9.3 The car page gauges

Three ring gauges (`handling_gauge`, `speed_gauge`, `acceleration_gauge`)
ride with the car's art:

- **Background ring:** 22° to 238° in dark gray (0.2), half the radius thick.
- **Value arc:** runs from 230° clockwise, 0.3 of the radius thick. For a
  value v it ends at 230° − 200° × v. It is orange up to 0.8 of full (the arc
  down to 70°) and red beyond.
- **On each new car** the gauges sweep to full (+0.04 per frame, full by frame
  25), then settle back at −0.02 per frame to the car's value. They fade in
  with the art.

The values (`arcade_gauges` in `src/game/gt_mode_menu/arcade.cpp`):

- **Handling and acceleration** are 0.7 for every car. The car data's bytes
  for them are always 0, and the game shows 0.7 for those.
- **Speed** is the showroom top speed in km/h (truncated) divided by 350, at
  most 1.

The **showroom top speed** (`sim::showroom_top_speed`) is found by a sweep:

- It runs from 0 km/h up to the speed at the rev limit in top gear (which is
  also the result if no step qualifies), in steps of
  ⌊(that speed in km/h) / 500⌋ + 1 km/h.
- It starts in first gear and shifts up whenever the engine passes the upshift
  rpm (or the rev limit − 500 if there is none).
- The top speed is the last speed where the full-throttle drive force is more
  than the resistance:

  F_drive > rolling(v) · c_rr · (m g + c_down v²) + c_drag v²

  Here rolling(v) is the speed-dependent rolling factor and c_rr = 0.1 the
  tarmac rolling coefficient. c_down is the sum of front and rear
  downforce, c_drag the drag coefficient as built (air density 1.2), m the
  car's mass and g = 9.8 m/s².
- The gears are the car's stored gear words, the ones the race uses
  (`sim::build_car_spec`). For a computed gearbox these are the words its
  gear slider generated; they are not generated again here.

On the disc this gives 297 km/h for `nsx_s_zero_97`, 283 for `rx7_rz_00`
and 249 for `xsara_wrc_99`. These are within 2 km/h of the game's
gauges (test `car_spec_showroom_top_speed`).

### §9.4 The time-trial page

`ar/select_compulsory` is a picker over the time-trial challenges. It moves
like the course picker, with the same strip and cross.

- **Art.** Each challenge shows its full-screen picture, its records (first,
  second, third), and the power and weight of the challenge car, with the car
  in 3D (§9.5).
- **Confirm** requests the race for the chosen challenge's course: on the disc
  the second challenge is `tt02`.

### §9.5 Showrooms

Every 3D car in the menus is a **showroom** with a camera and lights read from
`menu/showroom_<kind>.fbi` (`render::ShowroomCamera`). The record is 0x140
bytes. Key k is a 32-bit float at byte +0x10 + 4k, and the word at +0x04
enables lights (bit i for light i). The keys the engine uses:

| Keys | Meaning |
|---|---|
| 0–3 | global ambient |
| 4 + 16 i … | light i (0..2): ambient, diffuse, specular, direction (in eye space) |
| 0x34–0x36 | look-at start (x, y, z) |
| 0x38–0x3A | look-at target |
| 0x3B, 0x3C | look-at spring k, damping |
| 0x3D | elevation (negative = above) |
| 0x3E | yaw start (degrees; 180 = in front of the car) |
| 0x3F | distance start |
| 0x41, 0x42 | elevation and yaw acceleration |
| 0x43 | angle damping |
| 0x44 | distance target |
| 0x45, 0x46 | distance spring k, damping |
| 0x47, 0x48 | view-space shift |
| 0x49, 0x4A | view-space pitch, yaw |
| 0x4B | horizontal field of view |

The kinds are:

- `single`: the car page;
- `compulsory`: the time trial;
- `split`: the 2 Player car page, mirrored for player 2 (look-at x, yaw, yaw
  acceleration, shift x and view yaw negated);
- `dealer`, `buycar`, `garage`, `wheel`, `wheelshop`, `carwash`, `oilchange`:
  the GT Mode pages.

**Motion** (`MenuModelRenderer::showroom` in `src/app/screen_gtmode.cpp`).
Each frame, the distance and the look-at spring towards their targets with
the integrator of §5.2 and the file's k and damping. The yaw and elevation
speeds gain their acceleration, damped by key 0x43:

v ← v + a / 60;  v ← v − c v / 60;  angle ← angle + v / 60

With the `single` file the camera looks at 0.25 → 0.5 m from 10° above. The
yaw starts at 190°, turning with acceleration −10 and damping 0.5. The
distance springs from 2 m to 6 m, with a 53.13° field of view. Counting
the load delay below, the car is head-on (yaw 180°) about 2 s after the
page opens and side-on (yaw 90°) after about 7.4 s.

**Appearance.**

- The car is first held back for about 25 frames, as if loading from the
  disc. This delay is approximate.
- It then fades in: first visible 5 frames later, full after 88 frames on the
  arcade pages, or 38 frames on the GT Mode pages.
- A mirrored floor reflection is added at 0.65 × the fade × (1 − the car's
  dirt). The `buycar` popup has no floor reflection.
- The car is drawn in 4 jittered passes that are averaged (2 for `split` and
  `compulsory`) to smooth its edges. GT3 Revisited draws one pass, and one
  reflection pass, on phones and tablets (Android, iOS:
  `platform::is_mobile`), where the 3D renderer's MSAA smooths the edges
  instead.
- On the arcade car pages (`ar/select_car`, `ar/select_car_2p`) and the
  time-trial page the car and its reflection are drawn at the depth of the
  layout's `car` slot (z 5): over the background, the movie and the spec
  ticker, under the paint name, the cross band, the icon strip and the rest
  of the page. The intro steps with the menu frames, so it is exact frame by
  frame.
- **Before gameplay.** When the menu screen hands on a request (§7.5, §15),
  it frees the menus' 3D cars, their reflection maps, the prize model and
  their render targets (`gt_mode_widget_release`, `MenuModelRenderer::release`);
  each page loads its car again the next time it is drawn.

**Online lobby pickers.** The online lobby's car picker
(`src/app/online_pickers.cpp`) imitates this motion more simply:

- **Car.** The yaw starts at key 0x3E of `showroom_single.fbi` and turns with
  keys 0x42 / 0x43. The distance is a fraction of the framing distance: it
  starts at 0.4 and springs to 1 with k = 6, damping 6, standing in for the
  2 → 6 m spring. It fades in from frame 5 to frame 45, with the floor
  reflection at 0.3. The model loads once the choice has rested 8 frames (3
  if a car is already shown).
- **Icon strip** (`strip_target`). If the row fits in 600 units it is centered
  on x = 320 and stays still. Otherwise the focused icon is centered, with the
  row's ends held at 20 and 620. The focused icon's pulse is
  1 − (frames mod 12) / 24.
- **Paint column.** It shows the current paint with up to two slots above
  and four below (the outermost of each drawn faded), easing 30 % per frame.
  Paint names starting with `-` show nothing.

### §9.6 The 2 Player car page

`ar/select_car_2p` has no separate class page. Both players choose at the
same time, each on their own pad (`GameFlow::two_player_step`). Each player
goes through these states:

| State | Keys |
|---|---|
| class row: C, B, A, S, Dirt, Home Garage, Guest Garage (wraps) | Left / Right move (`select`). Confirm opens that class's cars (`next`), or plays `disable` if the class cannot be used. Cancel by **either** player leaves the page, back to the course |
| car and paint | Left / Right choose the car (clamped), Up / Down the paint (`cursor`). Confirm moves on, Cancel goes back to the class row |
| AUTOMATIC / MANUAL | Left / Right, Confirm, Cancel |
| RACING / DRIFT | skipped on a dirt course or when player 1's class is a garage class (the game reads player 1's class for both players) |
| ready | when both players are ready the race starts; Cancel steps back |

Entering the class row moves a road class to Dirt on a dirt course, and Dirt
to C on a road course. Road classes are crossed out on dirt and Dirt on road.

**Layout.** Each player has their own strip, logo and name plate (at 0.75
scale), gauges, paint column (without a name) and 3D car (the `split`
showroom). Player 2's elements are the layout's `_2p` elements, and player 2
is mirrored.

### §9.7 GT Mode screens

The menus need these GT Mode screens; their content is GT Mode's.

- **Map** (`map`). The directions move a pointer between the buildings
  instead of stepping through the list (`map_input`). On a press, the pointer
  aims at the building with the highest cos²(θ) / distance. θ is the angle
  between the pressed direction and the line from the current building to the
  candidate; buildings behind (cos θ < 0) are ignored. The press plays SE
  `cursor`. The pointer eases 30 % per frame, and the building takes the focus
  when the pointer is within 12 units of it.
- **Status bar** (`00cmn/status`, imported by most GT Mode pages). It shows
  the credits and "day N". The day text ends 10 units before its box's right
  edge.
- **Lists and rows.** Game code draws these over the page (`extra_draws`),
  placed as in §4.6, with its own keys and hit tests (`on_frame`,
  `garage_hover`, `garage_tap`):

  | Page | List | Spacing |
  |---|---|---|
  | `hm/hm_gr`, `hm/hm_gr_trade` (garage, trade) | vertical, 24-unit rows (`car1`, `car_a1` on the cursor) in the 304-unit `menu_garage` / `menu_garage_trade` window | 28 |
  | `00cmn/car_reguration` (Cars Permitted) | vertical, 20-unit rows (`link_menu`, `active_menu` on the cursor) in the 232-unit `menu_regulated_car` window | 24 |
  | `records_free_tt` (records, §12.4) | vertical, 56-unit bars in the 356-unit `menu_records` window | 60 |
  | `at/ws/ws_...` (wheel shop brand pages) | a row of 52 × 52 tiles in `menu_wheel_shop` | 56 |
  | `hm/hm_wheel` (the garage's wheel list, below) | a row of 60 × 60 buttons in the 620-unit `menu_wheel` window | 64 |

  - Up / Down move a vertical list's cursor, held at the ends: SE `cursor`
    (the records: `select`). Changing the garage's sort order puts the
    cursor on the first car and places the list at once.
  - A hovering pointer moves the cursor to the row or button under it (SE
    `select`), and the list or row then holds still until a key moves the
    cursor, so the row stays under the pointer.
  - A tap on another row or button moves the cursor there; a tap on the
    cursor's row or button acts as Confirm. On Cars Permitted a tap only
    moves the cursor.
  - A wheel shop brand page opens on the wheel the ride car wears (§4.2).
- **The garage's wheel list** (`hm/hm_wheel`, Garage > car > Wheel). One
  button per wheel the car can wear: the stock-wheel button first (the car's
  own wheels, the hidden tile `default_wheel`), then the bought
  wheels in the order they were bought, each with its own hidden tile. Each
  button is the page's `wheel` frame with the 52 × 52 tile in its middle; the
  cursor's button shows the frame's focused look (`wheel1`). The row is
  centered while it fits (up to 9 buttons) and scrolls with the cursor
  beyond that, with its scroll bar (§4.6).
  - The hidden orange `mark` frame is drawn over the button of the wheel the
    car wears, and the cursor starts there: on the stock-wheel button for a
    car on its own wheels. A car wearing a wheel the inventory does not hold
    has no button marked, and the cursor starts on the first button.
  - Left / Right move the cursor, held at the ends (SE `cursor` when it
    moves). The car on the page shows the wheel under the cursor.
  - Confirm asks "Equip this wheel?" (`hm/hm_change_wheel_sure`, a popup
    over which the list is not drawn). Yes puts the wheel on the car (the
    stock-wheel button: its own wheels) and shows `hm/hm_wheel_changed`.
    What a car may wear is GT Mode's (tests
    `gtmode_menu_disc_garage_wheel_list`,
    `gtmode_synth_garage_wheel_list`).

## §10 Options

The title Options menu (`menu/option.mbl`) is a horizontal list of eleven
pages (`flow::game_options`, `GameFlow::handle_option`). Each option is a
multi item. Its value is stored in `config.ini` section `[options]` as
`key = value`, and the change is saved and applied when the choice changes:

| Page | Option key | Values (first = default unless noted) |
|---|---|---|
| View | `wide_mode` | off (4:3), on (16:9) |
| Sound | `menu_bgm`, `menu_se`, `race_bgm`, `race_se`, `replay_bgm`, `replay_se` | on, off |
| Vibration | `vib1`, `vib2` | on, off |
| Race | `gear_auto` (transmission), `assist_asm`, `assist_tcs` | on, off |
| 2P Battle | `split_tire_damage` | on, off (default **off**) |
| 2P Battle | `split_boost` | none, low, high (default **low**) |
| Ghost | `ghost` | on, off (whether a better run replaces the ghost at once or after "Update Ghost?"; it never hides the ghost, presentation §G11) |
| GT Force 1P / 2P | `assist_1p` / `assist_2p` | type1 … type4 |
| GT Force 1P / 2P | `force_feedback_1p` / `force_feedback_2p` | on, off |

**Defaults.** The defaults where the game's own new-game defaults are not
known are approximate. The known ones are 4:3, sound on and vibration on.
Unknown or invalid stored values read as the default.

**Default page.** "Default settings" (`set_default`) puts every option back to
its default.

**2P Battle sliders.** Two values are sliders, not multi items:

- laps: 1–99, default 2, `split_laps`;
- handicap: −150 to +150 m in steps of 5, default 0, `split_handicap`; a
  negative value gives player 1 the head start.

Left / Right on a slider change it (SE `cursor`). The slider's yellow marker
sits at x + (laps − 1)/98 × w, or at the center + handicap/150 × w/2. The
handicap shows as a yellow line of length |h|/150 × w on the leading player's
side, with that player's car icon at its end.

**Previews.** The top page previews each page with widgets named after their
option. Only the art of the current values is drawn
(`option_preview_visible`):

- `<key>_<on|off>` is drawn when the on/off option is in that state;
- `<base>_<value>_<on|off>` (e.g. `assist_type1_on_1p`) is drawn "on" when
  the option has that value, and "off" otherwise.

**Fixed pages.**

- The Key Configuration page (`key/key`) and its view on the top page (layer
  `m5`) show a **Controller Settings** button instead of the DualShock 2
  table. The action rows, their labels and the 1P / 2P headings are not
  drawn; the panel (`bace_7`, 64, 28, 512 × 324) and its title bar stay. The
  button is a 260 × 26 box in the middle of the panel (x 190–450, y 177–203)
  with "Controller Settings" in white. It has the color of the title bar:
  orange on the page, gray in the view.
- On the page, Confirm or a tap on the button chooses it (SE `ok`). A tap up
  to 6 units above or below the box also counts; taps elsewhere on the page
  do nothing. The menu screen then opens Settings on its Controls page over
  the menus, which wait on the page (§15). Cancel leaves the page as before.
- The music page is the music list (§10.1).

**Music.** No page of the option set plays menu music (§7.3). The music list
page previews race songs (§10.1).

**When options take effect.**

- Menu BGM and Menu SE apply at once (`Gt3MenuScreen::connect_options`).
- The transmission option sets the arcade car pages' starting choice (§8).
- The race reads its own options when it starts, including Menu BGM for its
  quick menus' music (§11) and the music list's playlist.

**Leaving.** Cancel on the top page returns to the title with the focus on
Options.

### §10.1 The music list

Page `music/music` ("Favorite Music List", `flow::MusicListPage`, drawn in
its `song_edit` widget) shows and edits the **race playlist**, which the race
plays its songs from (music §2.3). The playlist is stored in `config.ini` as
`[options] music_playlist`. When none is stored, or the stored one is
invalid, a new one is made and stored at once: every race song of
`bgm/ads.inf` (25 on the disc) in the disc's order, all listed, Shuffle on.
The top page's music preview (widget `song_list`) shows the same list without
a cursor or previews.

**Rows.** One 24-unit row per song in the playlist's order: its check mark
and "title / artist" (text 10 units in). Then the Order / Shuffle row, 32
units high. The parts come from the page's hidden art (`label_check_box`,
`check_box`, `label_box`, `mode_select_box`, `button_straight`,
`button_shuffle`, `pop_*`). A song that is not on the disc shows the first
race song.

**Cursor.** It opens on the first song's check mark. On a song row it is on
the check mark or on the title.

- One key acts per frame, Confirm first, then Up, Down, Left, Right.
  Directions repeat as in §2.
- **Up / Down** move between rows without wrapping (SE `cursor`, `disable`
  at an end). Between song rows the cursor keeps its column; coming back
  from the last row, a song row has the column it last had.
- **Left / Right** switch between the check mark and the title, or choose
  Order / Shuffle on the last row (SE `cursor`, `disable` at an end).
- **Confirm on a check mark** takes the song into or out of the list (SE
  `ok`).
- **Confirm on a title lifts the song** (SE `ok`). Up / Down then carry it
  past its neighbors, never onto the last row (SE `cursor`, `disable` at
  the first or last song). Left / Right do nothing, and Confirm puts it down
  (SE `select`).
- Confirm on the Order / Shuffle row does nothing.

**Scrolling.** While the cursor moves (also carrying a song), the list
scrolls so that the cursor's row comes to the middle of the window, towards
the move only and never past the list's ends. It gets there in 4 frames, a
quarter of the way each frame (test `music_list_page_scroll`).

**Look.** The cursor's row, check mark or title is drawn with its focused
art, its brightness pulsing from 1.5 down to 1 over 30 frames. A lifted song
is drawn as a bar (`pop_label_check_box`) centered on its row, its brightness
pulsing from 1.5 down to 0.5.

**Previews.** The focused song's intro plays, looping, at level 1.0, whatever
the Menu BGM option (`GameFlow::take_preview`). The first song's intro starts
when the page opens. On an unlisted song or the last row the music stops. A
lifted song keeps playing; putting it down starts its intro again.

**Leaving.** Cancel returns to the top page and stops the music (back action
`stop_music`). It keeps the edit: the songs in the page's order, with their
marks and the Order / Shuffle choice, become the playlist, which starts a new
pass. The options are saved. With a song lifted, the edit is dropped.

**Pointer.** Hovering a row moves the cursor there, to the column under the
pointer, without scrolling (SE `cursor`); with a song lifted, hovering does
nothing. A tap on the cursor's place acts as Confirm; a tap elsewhere moves
the cursor there (SE `cursor`), and a tap on Order or Shuffle chooses it.
With a song lifted, a tap on another row carries the song there (as far as
the last song) and a tap on it puts it down (tests `music_list_page_*`,
`gtmode_synth_options_music_list`).

## §11 Race quick menus

Around a run the race screen shows GT3's **quick menu**, a row of icons
(`race::QuickMenu`). It runs on the same page manager (§4–§6) over the set
named after the layout: a horizontal scrolling row in the `menu` window, the
focus pulse, the focused item's help layer, the blink feedback, popups and
the menu SEs.

**Which layout** (`RaceScreen::quick_menu_layout`):

| Run | Before the run | After the run |
|---|---|---|
| license test | `qm_gt_license1` | `qm_gt_license1` |
| 2 Player Battle | — | `qm_arc_2p_result1` |
| machine test (GT Mode) | `qm_gt_mt_10001` (code `M1000`), `qm_gt_mt_max1` (`MMAX`), else `qm_gt_mt_4001` | the same |
| a run with no opponents | `qm_arc_tt_ltd1` with a lap count (the arcade time trial), else `qm_arc_tt_free1` | the same |
| GT race | `qm_gt_event1` | `qm_gt_result1` |
| championship round | `qm_gt_event_champ1` | `qm_gt_result_champ1` |
| arcade race | — | `qm_arc_result1` |

The menu comes **before** the first run for GT races, machine tests, license
tests, GT free runs, time trials and arcade free runs. The arcade race and 2
Player Battle start driving at once.

- **Before a race**, the menu lists the entry list with no times
  ("--'--.---", drawn with the font's long dash: presentation §D10) and the
  lap count.
- **Time trials, free runs and machine tests** show the ranking's Top 3
  (`RaceScreen::fill_top3`), read again before every quick menu and once more
  when a run's name entry closes (below). A free run started from the menus
  (the arcade time attack, GT Mode's free run;
  `RaceScreen::course_time_trial`) shows its course's time-trial ranking
  (`ct:<course>`, §12.4):
  - always three rows; a place the ranking has no entry for shows no name,
    no time (`--'--.---` in the font's long dashes, presentation §D10) and the
    speed ` 0.0 km/h` (or `mph`);
  - the names as stored with the records, times as `m'ss.mmm` (the menus'
    format, presentation §D10);
  - the top-speed test (MMAX) shows speeds as `%4.1f %s`, rounded to tenths,
    for example "218.8 mph", in the game data's speed unit (mph on the
    NTSC-U data) unless the `race` / `units` setting (Settings → Display →
    Speed units) picks one;
  - the time-trial list's car column (`qm_list_car_name<n>`) is the entry's
    car name, or `---` for an entry without a car or an empty place (a free
    run's list: the car each lap was driven in).
- **License tests** show the test's name and target times.

**Records after a run** (`RaceScreen::record_run`). When a live run started
from the menus (not a Quick race practice, a replay or a `qm_demo` run)
ends (its exit transition is over, §11.1), the run goes into its GT Mode
ranking at once, once per run:

| Run | Recorded when | Value | Ranking |
|---|---|---|---|
| license test | it finished with a medal | its time | `lic:<test>` (`game::add_license_record`) |
| machine test | it finished (the top-speed test also by a pause EXIT that counts as its finish, §11.2) | the time (M400, M1000) or the top speed (MMAX) | `mt:<code>` (`game::add_machine_test_record`) |
| arcade time-trial challenge | it finished (its one lap, `race::kTimeTrialLaps`), not failed, with a best lap | its best lap | `at:<code>` (`game::add_time_trial_record`) |
| free run (arcade time attack, GT Mode free run) | it has a best lap, however it ended | its best lap in whole ms, with the run's car | `ct:<course>` (`game::add_course_time_trial_record`) |

A free run's lap goes in only on a course of the game data's `TIME_ATTACK`
course list (36 courses on the disc); other courses have no ranking. A lap
over 0x3FFFFF ms is not entered. A new value ranks ahead of an equal one
already stored. A run that makes the
ranking opens the record name entry (§12.2, §7.6) before its replay (§11.1),
so the quick menu that opens after the replay shows the name just entered; a
license test has no replay, its name entry opens over the quick menu and the
Top 3 is read again when it closes. Every finished run is recorded this way, not
only the session's last one, and only this way: the menus never record from
the result posted when the race screen is left (time trial, free run,
license test, machine test, GT free run), so a time trial left by EXIT before
its lap is done records nothing. `GameResult::records_done` (a run of this
visit was recorded) only makes the arcade result save the game when the last
run did not finish.

**Music.** The quick menus play their music (the result menus one track, the
others another; music §3.3) at the menu level, looping, while the Menu BGM
option is on; the Race BGM option does not apply to them. Closing a quick
menu stops it. The race reads the option when it starts.

**Keys** (`QuickMenu::tick`):

- Keys held when the menu opens count only after they are released.
- Confirming a disabled item (below) plays SE `disable` and nothing else; the
  focus stays on it. A scripted `qm=` choice of a disabled item is refused
  the same way.
- Confirming an item (kind 2) reports the choice. The root page then rebuilds
  with the same item focused, so the menu is still there behind any panel the
  item opens. Confirming a root item other than options, records or the
  Analyzer stops the race behind the menu (§11.3); back on the menu (after
  saving, loading a ghost or a dialog) it starts again from the run's start.
- Cancel (Triangle / Square) on a root page with the cancel flag 0x100
  (every race quick menu but the championship ones) ends the menu at once as
  that page's EXIT item (`qm_exit`), with SE `return` (test
  `race_quick_menu_cancel`). The championship menus and the dialog popups
  (`exit_sure`, `ghost_dlg`) have no cancel flag and ignore it.
- An item that links to a dialog page opens it as a popup: "Retire from
  championship?" (`exit_sure`). "Update ghost?" (`ghost_dlg`) opens by itself after a
  run better than the ghost while the Options menu's Ghost is off (presentation
  §G11). The dialog's yes / no is reported.
- At most 8 frames run per screen update.
- **Touch controls.** While a quick menu is up, before or after a run, the
  on-screen touch controls show their menu layout (D-pad and face buttons,
  `docs/INPUT.md`), as on the pause menu and during replays; the driving
  layout shows only while a live run is driven (`RaceScreen::touch_layout`).

**Which items can be chosen** (`race::QuickMenuItems`, kept for the visit to
the race screen: every run, retry and menu until it is left; tests
`race_quick_menu_items`, `race_quick_menu_disabled_item`,
`race_quick_menu_disable_disc`). The rules go by the root page's item name;
dialog pages have none.

| Item | Enabled |
|---|---|
| `replay`, `save_replay` | after a live run (finished, failed or left with pause EXIT), except a license test left with pause EXIT; off again after `demonstration`, until the next run |
| `save_ghost`, `data_logger` | once a run of the visit set a session record: a time trial's lap, a 0-400 m / 0-1000 m time or a judged license run (a medal or TIME EXPIRED; not a FAIL) at or under the visit's best so far, or a free run's lap under it (any lap when there is none yet) |
| `setting`, `save_setting`, `load_setting` | not in a time trial; not in an arcade race or free run with a car that is not a garage car |
| `free_session` | not before a GT race on a dirt course (a championship round keeps it) |
| `exit`, `save_game` | not once a championship's last round has been run |
| any other | always |

A disabled item stays focusable and the focus moves past it as usual. It is
drawn under the layout's `disable` shade (every race quick menu's root page
has one: black at alpha 0xB1), laid over its normal look in the item's own
box; focused, its highlight is drawn at half brightness under the shade
(§5.3). The switches are
handed to the menu again when they change while it is up (DEMONSTRATION).

The item actions:

| Action | Item |
|---|---|
| `qm_start`, `qm_try_again` | run (again) |
| `qm_free_run` | the free practice before a GT race: the course alone, the player's car starting in its pit box (below) |
| `qm_replay` | watch the replay (§11.1) |
| `qm_demo` | the disc's demonstration of the event plays as a recorded race; without one, the computer drives the player's car |
| `qm_save_replay` | save the replay |
| `qm_save_ghost` | save the visit's best run (a free run: the run of its best lap), presentation §G11 |
| `qm_load_ghost` | load a saved ghost at once, from the saved replays that can be played here (§13), presentation §G11 |
| `qm_records` | records (§12.4, or a panel, §11.4) |
| `qm_logger` | the Analyzer over the session (§11.4) |
| `qm_option` | options (§11.4) |
| `qm_setting` | the car settings (§11.4) |
| `qm_next_session` | the next championship round |
| `qm_save_game` | Save game |
| `qm_exit` | leave |
| `qm_yes`, `qm_no` | dialog answers |

Every action in the disc's 12 quick-menu layouts (97 items, 17 actions) has a
meaning in this table (test `race_quick_menu_actions_disc`).

**Free practice needs a pit box** (`RaceScreen::start_free_run`). The free
practice starts the player's car in its pit box on the course's pit stop
line. On a course whose stop line has fewer than 4 points it has no pit box
(`race::exact_has_pit_box`): FREE RUN is then refused with the message "Free
run is not available on this course." and the pre-race menu stays up. A
course that cannot be loaded is refused the same way. GT Mode's courses without
a pit box are its dirt courses (no pit stop line), where `free_session` is
already off, so its own races never show the message.

**The result is taken when the live run ends** (`race::RaceResultLatch`,
`RaceScreen::latch_result`), for every run. The
game records the place and the prize right after the race, and the replay runs
after that. So the engine latches the live run's result once, at its end:

- The replays and the race behind the menu (§11.3) re-run the run, but the
  latched result does not change.
- Every way out reports the latched run: EXIT on the quick menu, EXIT on the
  replay's pause menu, or leaving from a replay.
- Flags decided later still come from the moment of leaving: retiring from a
  championship, the prize page or records already shown, and the car's dirt.
- A new live run (Try again) drops the latched result.
- The quick menu after a replay still shows the live race's rows.

### §11.1 After a run

When a live run's exit transition (§6) is over (`RaceScreen::after_run`;
the rules in `race::post_run`, test `race_post_run_sequence`), these follow
in order, then the quick menu:

| Run | First | Replay |
|---|---|---|
| arcade race | the difficulty's win cup (`arcade_mini`), for a win only | yes |
| 2 Player Battle | — | yes |
| GT race, championship round | the prize page (cup or consolation prize, §7.6) if it finished | yes, also after a pause EXIT |
| time trial, free run, machine test | the name entry if it ranks | yes |
| license test | the name entry if it ranks (over the quick menu) | no |
| GT free run (free practice) | — | no; back to the race's pre-race menu |

- The replay plays by itself after every run in the table that has one,
  finished, failed or left with pause EXIT alike. It plays once, the whole
  run from its start to the frame the live run ended (or the pause EXIT
  that counted as its finish, §11.2), with no result screens.
- The run's music stops; the replay starts its own songs under the replay
  BGM switch (REPLAY OPTIONS).
- Only Start opens the replay's pause; no other button skips it (the 2
  Player Battle's replay too, which is not tinted).
- At its end, or on EXIT from its pause, the exit transition (§6) runs over
  its last frame and the quick menu cuts in. The REPLAY item's replay ends
  the same way, back to the same menu.
- The prizes (arcade cup, GT race and championship prize pages) are queued
  by the race screen, only for runs started from the menus that are not a
  Quick race practice, and posted as `GameResult::prize_shown`.

A license test left with pause EXIT: its menu's preview box shows the disc's
demonstration instead of the run, and its REPLAY item is disabled (§11).

### §11.2 Pause EXIT as a finish

Pause EXIT ends a live run at once (§6), except
(`race::pause_exit_finishes`, test `race_pause_exit_finish`):

- in a free run (arcade time attack, GT Mode free run) with a completed lap;
- in the top-speed machine test once its car has moved (over 1 m/s) and
  has not reached its goal.

There the press is the run's finish: nothing is timed after it, the
race's recordings close at the press (the replay ends there), the record
overlay shows over the running race (5 s after the press; the top-speed
test's 0.1 s). The race ends the car's run at the press and the end-of-run
driver takes the car: a free run's car stops with the wheels straight on
the pit surface, else drives on without slowing; the top-speed test's car
drives on as at its goal (presentation §E5, `RaceScreen::exit_as_finish`,
test `exact_game_exit_as_finish`). A second EXIT ends the run.

### §11.3 What is drawn behind the menu

`race::menu_backdrop`, `RaceScreen::start_menu_backdrop` (tests
`race_menu_backdrop`, `race_quick_menu_backdrop_disc`):

| Menu | Behind it |
|---|---|
| time trial, free run, machine test (before and after a run) | the race, full frame |
| GT race, championship round, before the race (also after a free practice) | the race, full frame |
| result menus: arcade race, 2 Player Battle, GT race, championship round | no race: the layout's own opaque checker background over the whole frame |
| license test | the 280 × 210 preview box whose top-left corner is 32 units in and 58 down from the frame's top-left: the run, or the demonstration |

- The race behind the menu replays the last run endlessly: from the run's
  start, records off, the view mode picked at random as for the attract
  demo, its sound effects off. Once its recording runs out the cars get no input
  (no steering, throttle or brake) and the race goes on.
- Before any run the recording is empty: the car stands at its start; a
  field stands on its grid.

### §11.4 The quick menu's own panels

Some quick-menu items open a panel of the engine's own
(`RaceScreen::update_panel`, `draw_panel`). It takes the menu's place in the
4:3 frame, over black, with nothing behind it:

| Item | Panel |
|---|---|
| OPTION (`qm_option`) | OPTIONS: Transmission (AT / MT), Camera, Music, Vibration, Display (the HUD), and for 2 Player Battle the split (horizontal / vertical) |
| SETTING (`qm_setting`) | in GT Mode with a garage car (not a Quick race practice), the game's car settings menu (control `setting`), which closes back to the quick menu: before the first run with the car on its start as now set up, after a run with the last run kept (REPLAY plays it) and the next START taking the new set-up; otherwise a SETTINGS panel with Transmission only |
| RECORDS (`qm_records`), for runs without a GT Mode ranking (§12.4) | the best laps of the saved replays on this course |
| DATA LOGGER (`qm_logger`) | the Analyzer's graph over the session's laps, with its own keys |

On OPTIONS and SETTINGS, Up / Down choose a row (clamped), and Left / Right or
Cross change it. Circle or Triangle closes a panel (Cross too on the records
panel), back to the quick menu. Changes made here are not written to the
title Options (§10).

## §12 The save page and name entry

### §12.1 The save page

Saves and car setting files are plain files in the engine's storage
(`flow::SavePage`). The engine draws them on the game's file list page
(`menu/memorycard.mbl` page `file_operation`). The page has no memory-card
slots, no "working" progress windows and no capacity (its window art only
waits for a file dialog and says where an export went). It has two icon
panels:

- **Left panel** (SAVED GAMES or SETTING FILES): a 4 × 3 grid of 64-unit icons
  at a 70-unit pitch, scrolling by rows.
- **Right panel** (FROM A FILE): appears in Load game, Load Garage / Trade
  and Files when the platform has file dialogs. It holds one entry, "Import a save", or "Open a file" when loading a guest
  garage, which opens the platform's file dialog.

The grid spacing comes from screenshots and is approximate.

| Mode | Opened from | Confirm on a file |
|---|---|---|
| Load game | title Load Game | operations: Load, Edit file name, Copy, Delete, Export (Export only with file dialogs, as everywhere); "New game" asks for a name, then starts a new game |
| Save game | title Save Game, home Save, a championship's quick menu | operations: Overwrite ("Save" on the save being played), Edit file name, Copy, Delete, Export; "New Save" asks for a name, then saves |
| Load Garage / Trade | arcade Load Garage, GT Mode Trade | loads that save as the guest garage at once |
| Files | title File Operation | operations: Edit file name, Copy, Delete, Export; Import on the right |
| Load / Save settings | car settings | operations: Load (or Overwrite on the car's own files), Edit file name, Copy, Delete; Save on "New file" |

**Rules.**

- The save being played is never offered for deletion.
- An unreadable file offers only Delete.
- Setting files of other cars are dimmed (brightness 0.5) and cannot be
  overwritten.

**Keys.**

- The directions move over the grid (SE `cursor`). Right past a row's last
  file, or past the panel's edge, goes to the right panel, and Left comes
  back.
- Confirm opens the file's operation menu, or acts at once where the table
  says so.
- In the operation menu, Up / Down choose and Confirm runs the operation.
- Cancel goes back one level, and from the list it leaves the page.
- Mouse and touch (`SavePage::tap`, `hover`): a hovering pointer moves the
  cursor to the icon, button or OK / CANCEL under it (SE `cursor`); a tap does
  the same, and on what is already focused it is Confirm. A tap outside the
  open operation, format or question window is Cancel; a tap anywhere closes
  the error and "Exported" windows (test `gtmode_menu_disc_save_page_dialogs`).

**Export to PS2** (the same on every platform). The operation opens a format
window in the operation menu's place, drawn the same way, with three buttons,
top first:

| Button | Writes |
|---|---|
| PS2 save (.psu) | the save as a `.psu` |
| PS2 card (.ps2) | a new PCSX2 memory card image holding only this save |
| Raw save (.raw) | the save's data file alone, which PS2 save editors open |

- It starts on the first. Up / Down choose, held at the ends (SE `cursor`);
  Cancel goes back to the operation menu (SE `cancel`).
- Confirm (SE `ok`) opens the platform's save dialog, which only picks the
  place. It starts on the file's name (`GT3` without one) with the format's
  extension. The chosen format is written whatever the file is called; a
  name typed without an extension gets the format's. In the browser the file
  is downloaded under that name. On macOS the dialog starts in the folder of
  the last export, or in Documents.
- Cancelling the dialog returns to the format window (test
  `save_page_flow`).
- A written file shows "Exported as" and its name in the "working" window
  with OK; either key or a tap closes it, back to the list.

**While a file dialog is open** (export or import) the page shows the
original's "working" window with "Waiting for the file window..." and one
CANCEL button. Either key, or a tap on CANCEL, stops waiting (SE `cancel`):
back to the format window for an export, to the list for an import. The
dialog is closed where the platform allows it (macOS), and an answer it still
sends is ignored (test `save_page_dialog_cancel`). On macOS the dialog is a
sheet on the game's window: a controller's buttons reach the page while it is
up, and when it closes the window takes the keyboard and the controller again
(macOS leaves no window with the keyboard after such a sheet).

**Import.** "Import a save" (and "Open a file") accepts the engine's `.sav`
saves and PS2 saves: a `.psu`, a memory card image (`.ps2`, `.mcd`), or the
save's data file alone under any name, such as a `.raw` from a save editor,
recognized by its header (tests `gtmode_disc_ps2_save_containers`,
`gtmode_disc_ps2_raw_save_edited`). An imported save without its own file
comment takes the picked file's name without its extension (on Android
the name the system's picker shows), or "PS2 GAME".

**One question at most.** Delete and overwrite each ask one question. Delete
starts on CANCEL and overwrite starts on OK. Overwriting the save being played
asks nothing.

**Errors.** A failed operation shows the write-error window, or the
read-error window (file damaged or wrong version). Either key closes it.

**Where the cursor goes.**

- The page opens on the save being played (Load / Save), otherwise on the
  first file that is not "New".
- After a copy, an import or a rename of a save, the cursor goes to the new
  (newest) file.

**After a load, save or new game,** the page ends and the flow goes on:

- title modes return to the title on the same item;
- Load Garage returns to `ar/top`;
- Trade goes to the guest garage's car list;
- a settings mode returns to the car settings, which may then show a popup
  (`load_warning`).

Cancel goes back where the page was opened from.

**Look** (`SavePage::draw`).

- The panel without the cursor is drawn at 0.3 brightness.
- The cursor is a white quad added over the focused icon at alpha
  0.25 × (1 − (f mod 31)/30).
- The bottom bar shows the focused file's name, with "(playing)" for the
  current save, or "New game" / "New save" / "New file".
- The info panel shows the focused file's details.
- **Operation menu.** It is a window 256 units wide and
  max(140, 44 + 32 (n − 1)) high for n operations. It is centered on the
  focused icon and kept inside the panel. Buttons sit 116 units in, the first
  36 units below the top, 32 apart.

### §12.2 Name entry

The name entry page (`name_entry`, used for a new save, a rename and the
record names after a run) is a grid of key rows plus an OK row. The page has
a `menu` window, so the engine places the rows itself, ignoring the layout
positions (`GameFlow::draw_list_impl`):

- **Rows.** Seven rows (six key rows and the OK row) stack down from the
  window's top, 32 units high with 4 units between them.
- **Keys.** In each key row the keys are 24 wide at a 28-unit pitch, centered
  on the window. The OK key is left-aligned in its row.

**The text box** (`flow::TextBox`). Both uses, the save page's name entry
(`GameFlow`) and the record name entry (`GtModeSession::name_box`), edit the
name the same way. The text has a cursor, an insertion point between
characters:

- The entry starts with a name and the cursor after it. A save or a rename
  starts with the name the save page offers (a rename: the file's name). A
  record starts with the last name entered (`GameState::player_name`, also
  read from and written to imported and exported PS2 saves).
- Confirm on a key inserts its character at the cursor and moves the cursor
  past it (SE `cursor`), while the text is shorter than its limit; on a full
  text nothing happens. The character is the key's own text (the save page:
  the key element's text; the record entry: GT Mode's key table from
  `name_entry1.pmb`). A key without text types a space.
- Triangle / Square are **Back Space**: the cursor steps one left (it stays
  at the start) and the character there is deleted, so at the start the
  first character goes. SE `cursor` while the text was not empty. They never
  leave the page and do not repeat.
- L1 / R1 move the cursor one character left / right over the text,
  auto-repeating as in §2 (SE `cursor` when it moves; silent at the ends).

**Other keys.**

- Start jumps to the OK row.
- Only OK ends the page.
- Up / Down keep the column (§4.3).

**Limits.** A save name holds at most 16 characters (`SavePage::kNameMax`),
and trailing spaces are dropped. An empty save name changes nothing. A record
name holds at most 15 characters. Every key and Back Space of the record
entry stores the name with the new record and as the last name entered.

### §12.3 Caret

The text box shows the name with the 12-unit text inset (§5.7). The page's
hidden red bar `cursor` is the caret. It sits at the text box's cursor, right
after the text before it: box x + 12 + width of that text + 1, with the
width measured with the game font (`GameFlow::draw_list`; the record entry's
cursor comes from `GtModeHooks::name_cursor`). With the cursor at the start
it is at box x + 13. The box is lighter at the top and the caret brightest at
its bottom (the layout's corner colors run from the bottom-left).

### §12.4 The records page

The quick menu's Records item (§11) of a license test, a machine test, an
arcade time trial or a free run (the arcade time attack, GT Mode's free run)
started from the menus opens the GT Mode records page (control `records`,
page `records_free_tt`; `make_records_screen`, `game::begin_records`) on that
run's ranking; a free run's is its course's time-trial ranking
(`ct:<course>`). Other runs' Records item shows the best laps of the saved
replays instead.

**Course time trials.** There is one ranking of 12 places per course of the
game data's `TIME_ATTACK` course list (36 on the disc;
`GtMode::course_time_trial_index`), filled only by free runs' best laps (§11).
A new game has them empty: unlike the arcade challenges they have no built-in
records.

The page draws every place of the ranking (`draw_records` in
`src/game/gt_mode_menu/extra_draws.cpp`): 12 places, 6 for a license test.
Each place is a 56-unit `record_bar` with its texts, in a vertical list in
the 356-unit `menu_records` window, 60 units apart from its top (§4.6, §9.7):

- **Cursor.** The page opens on the first place, its bar drawn with the
  focused look (`record_bar1`). Up / Down move the cursor through the
  places, held at the ends (SE `select`); a hovering pointer or a tap moves
  it as in §9.7, and a tap on the cursor's place acts as Confirm.
- **Scrolling.** Six places show at once. The 12 places scroll with the
  cursor, held at the window's top and bottom near the ends; a license
  test's 6 fill the window and do not scroll. The page has no scroll bar
  (test `gtmode_menu_disc_vertical_lists`).

The texts of each place:

| Text | A stored entry | An empty place |
|---|---|---|
| `record_rank` | the place, 1 … | the place |
| `record_name` | the entry's name (`GtMode::ranking_name`; none for an entry stored without one) | nothing |
| `record_data` | the time `m'ss.mmm` (the menus' format, presentation §D10); the top-speed test: the speed (`race::record_speed_text`, whole units and tenths, truncated, in the game data's unit, "194.1mph") | `--'--.---` (the top-speed test: `---.-` and the unit), in the font's long dashes |
| `record_date` | the date the record was set, `DD/MM/YY` | `--/--/--`, in the font's long dashes |
| `record_car_pict`, `record_car` | machine tests and course time trials only: the car icon, then the car's two names, "<name> <second name>" | the icon, no name |

**Dates** (`game::record_date_text`). Each record carries a clock stamp, the
local time when it was set (`RankEntry::stamp`). The date is written day,
month, year, the order of the NTSC-U data. Records without a stamp (from a
save older than the stamps) show `--/--/--` (the font's long dashes,
character 0x7F, presentation §D10). Only the year 2000 has a
February 29: in later leap years February 28 is followed by March 1 and the
year's last day shows as December 32 (test `gtmode_record_dates`). The
arcade challenges' built-in records have no stamp in a new game made by the
engine; imported from a PS2 save they keep the save's stamps (a reference new
game's show 16/10/26). PS2 saves carry the arcade records' stamps both ways.

**PS2 saves** also carry the course time trials and the three machine-test
rankings both ways, with names, cars and stamps: 44 course slots, a
course's slot being its place in the `TIME_ATTACK` list (the slots past the
list stay empty), then 0-400 m, 0-1000 m and top speed. An imported
machine-test ranking's first entry is also that test's best. Each record's
32-byte entry block in a PS2 save is kept from an import and written back
unchanged; the engine's own records write it as 30 `-`. An imported arcade
challenge record takes the challenge's car.

## §13 Replay Theater and Analyzer

Page `replay_theater` (from the title) offers three items:

- **Load Replay:** the saved replays, newest first.
- **Demonstration:** the disc's demonstration runs, each played as a
  recorded race.
- **Analyzer.**

**File list** (`flow::TheaterPage`, drawn on the game's DEMONSTRATION page
`demo_loader`):

- **Grid.** 64-unit icons in rows of 8, three rows visible (the top of a
  fourth shows, clipped), rows scrolling with the cursor. The info panel below
  shows the focused file's course, car, time, laps, power, mode, date and
  size.
- **Confirm** plays the file at once. With no files it plays `disable`.
- **Older replays.** A saved replay of a format before 11 is still listed,
  but its info panel shows only "-", its date and size, with the comment
  "Recorded with an older version". Confirm on it plays `disable`; Start can
  delete it (`game::replay_theater_entries`, test `replay_library_files`).
- **Release and data.** A replay plays only on the game release and with the
  game data it was recorded with. Otherwise it does not play and a message
  says why.
- **Start** deletes a saved replay after one "Delete OK?" question, which
  starts on CANCEL. Demonstrations cannot be deleted.
- **Cancel** returns to `replay_theater`, with the focus on the item that
  opened the list.
- **After playing,** the replay or demonstration runs once, then
  `replay_theater` reopens with the focus on its first item. No result is
  processed.
- **Cursor look.** The cursor is an additive white quad at alpha
  0.3 + 0.25 × (1 − (f mod 31)/30). The level is approximate.

**Analyzer** (`menu/logger.mbl` page `top`, `GameFlow::logger_tick`). It has
five buttons: REPLAY, GRAPH, LOAD A, LOAD B, EXIT. The focus starts on REPLAY.

- Left / Right move the focus, clamped (SE `cursor`).
- LOAD A / LOAD B open the file list as "Load track" and return with that
  track's file and info.
- REPLAY and GRAPH need a loaded track; without one they play `disable`. A
  second track is used only if it is on the same course as the first.
- EXIT or Cancel returns to `replay_theater` with the focus on Analyzer.
- **Button look.** REPLAY and GRAPH are dimmed to 0.35 while no track is
  loaded, other buttons are at 0.75, and the focused one is full. The focused
  button gets an additive pulse 0.25 + 0.15 sin(0.1 f), which is approximate.

## §14 Menu sound effects

The menus play named effects of `sound/se.inf` (bank 0, `sound/gt3sys.ins`)
through the voice mixer (`flow::MenuSounds::play`):

- **Volume.** Each effect plays at volume 1.0 × its region's level, clamped
  to 0x3FFF.
- **One-region effects** are started twice, panned hard left and hard right,
  so they sound centered at full level.
- **Region-pair effects** (`next` 2–3, `return` 7–8) play only their first
  region, centered with the sine pan law.
- **Overlap.** Earlier sounds are not stopped.
- **Menu SE off.** With the Menu SE option off, nothing plays.

| SE | When |
|---|---|
| `select` | the focus moves; a multi item's choice changes; an arcade picker moves; a tap focuses an item; a hovering pointer moves a code-drawn list's cursor (§9.7); the records page's cursor moves (§12.4); a lifted song is put down (§10.1) |
| `ok` | confirm on a page without feedback; game code opens a popup; save and Theater page confirms; a music list check mark or lift (§10.1); the Key Configuration page's button (§10) |
| `next` | confirm on a page with feedback; arcade picker confirm; arcade car page step |
| `cancel` | cancel to a back page; arcade step back; save and Theater page back |
| `return` | a popup closes; cancel ends the menu (no back page) |
| `disable` | a refused confirm (disabled item, intercept refusal, disabled picker entry); the end of a clamped picker; the ends of the music list (§10.1) |
| `cursor` | save and Theater grid moves; option sliders; music list moves (§10.1); paint and step choices; name entry key, Back Space and cursor moves (§12.2); the GT Mode map pointer; Analyzer focus; GT Mode's code-drawn lists; the prize counter's beep |
| `param` | a GT Mode car settings slider changes |

**GT Mode's per-frame code** (`on_tick` and `on_frame` in
`src/game/gt_mode_menu/lists.cpp`) can add effects every frame:

- **Running animations** (`on_tick`) step on every frame, also while the menu
  is busy, and play their effects on the frame they reach them. The car wash
  plays `car_wash` 31 frames after its action. The oil change plays `oil`
  twice, as its gauge starts to drain and as it starts to refill: 62 and 224
  frames after its action (test `gtmode_menu_disc_wash_and_oil`). An oil
  change whose gauge is no longer on screen plays nothing more.
- **The page's code** (`on_frame`) runs on every frame the menu is not busy
  and plays at most one effect per frame. The page's own code runs first and
  its effect wins the frame: the settings sliders (`param`, or `disable` at
  an end or when refused), the code-drawn lists (the dealer's paint, the
  garage list, the Cars Permitted list, the garage's wheel list: `cursor`;
  the records page: `select`), and
  the prize counter's beep (`cursor`), which plays on the frame of its step.
  A queued map pointer move (`cursor`) plays on the next frame on which the
  page's code plays none.

The race quick menus play the same set through their own `MenuSounds`.

## §15 The menu screens in the app

**`gt3r --screen gt3menu`** (`Gt3MenuScreen`) is the screen that runs the
game's menus.

- **Picture.** It draws each 640 × 448 frame into a texture shown at 4:3 in
  the middle of the window.
- **Frames.** It steps the flow in 60 Hz frames, plays the flow's SEs, music
  and movies, and starts transitions over the last frame.
- **Gameplay.** It hands requests to the registered gameplay handler, and
  hands its last frame with the running transition to the race screen for
  the loading picture (§6), after freeing the menus' 3D cars (§9.5). A movie
  request plays the movie screen. With no handler it opens the "race" screen,
  or returns at once.
- **Leaving.** The "GT3R menu" button in the bar beside the title menu, Select
  on the title, or Cancel on the title returns to the engine's own main menu.
  While the on-screen touch controls are shown, the button moves up until it
  is 10 UI units clear of every control's touch area (round buttons count out
  to 1.2 × their radius), so it sits above the bottom-left cluster (the D-pad,
  or the face buttons when left-handed) and a tap reaches it
  (`TouchControls::clear_above`, test
  `touch_clear_above_moves_buttons_off_controls`).
- **Controller Settings.** When the Key Configuration page's button is
  chosen (§10), the screen opens the engine's Settings on its Controls page
  over the menus, which stay on that page. Settings' back button then reads
  "< Back to game", and its key hints end in "Triangle/Esc: back to game".
  That button, Triangle or Esc closes Settings, and the menus go on from the
  Key Configuration page as they were, from anywhere on the page. Settings
  opens with player 1's Controller row selected; the bindings of both players
  are on the same page below (docs/INPUT.md, Remapping in the app). The screen
  Settings opens there, **Customize layout...** (Touch), also leads straight
  back: Triangle or Esc (Android back) closes it and Settings together,
  keeping the changes made on it. The layout editor's **Done** still returns
  to Settings. Player 1's buttons still held when Settings closes are ignored
  by the menus until released, so the Triangle that closed it does not also
  leave the page. If the menu language was
  changed in Settings meanwhile, the menus start again from the title in the
  new language.
- **Language.** The menus use the language folder `[gt3menu] lang`
  (default US), or the first language on the disc if that one is missing.
- **Debug overlay** (`[gt3menu] debug = on` or `GT3R_GT3MENU_DEBUG=1`). It
  shows the language, the page name and the flow's log lines, and Select then
  cycles the menu language among the folders on the disc.

Development aids:

| Variable | Effect |
|---|---|
| `GT3R_LANG` | the menu language folder |
| `GT3R_GT3MENU_OPEN=<set>:<page>` | opens a page directly |
| `GT3R_GT3MENU_SCRIPT` | runs keys at start-up, one frame each plus a release frame, after 30 frames to let the first page settle. Example: `down,cross,wait:40` |
| `GT3R_GT3MENU_FREEZE` | stops the menu clock after the script |

**`gt3r --screen menu`** (`src/app/screen_menu.cpp`) is a static viewer. It
composes pmb pages with their default layers, optionally with a focused item,
and browses IMGS sets. It does not run the flow.

---

## Verification

Checked on the disc image (PBPX-955.03) with `build/gt3r_tests` and with
scratch programs over the engine library:

- Tests `menu_flow_*` (navigation, auto-repeat, popups, return / import,
  the initial focus rule, disabled items, fades, disc targets, the disc's
  race page hooks and dealer strips, the highlights on their items,
  title → time trial → back, the Key Configuration button, the horizontal
  rows' scroll bars and easing, the vertical list layout, every horizontal
  selector on the disc), `gtmode_menu_disc_vertical_lists`,
  `gtmode_synth_garage_list_scroll`, `gtmode_menu_disc_garage_wheel_list`,
  `gtmode_synth_garage_wheel_list`, `save_page_flow`,
  `gtmode_disc_ps2_save_containers`, `gtmode_disc_ps2_raw_save_edited`,
  `gtmode_synth_title_focus_on_return`, `gtmode_synth_race_page_focus`,
  `gtmode_synth_attract_mode`, `gtmode_synth_options_music_list`,
  `music_list_*`, `gtmode_menu_disc_wash_and_oil`,
  `gtmode_menu_disc_wheel_shop_highlights`, `race_exit_wash`,
  `race_loading_picture_frames`,
  `race_quick_menu_actions_disc`, `race_quick_menu_cancel`, `menu_flow_text_box`, `gtmode_record_dates`,
  `gtmode_synth_machine_test_runs`, `gtmode_synth_course_time_trials`,
  `gtmode_disc_course_time_trials_and_save`, `car_spec_showroom_top_speed`,
  `replay_library_files`,
  `touch_clear_above_moves_buttons_off_controls` and `sound_menu_effects` pass.
- Read with `gt3tool menu-pmb`: the corner colors of the arcade car pages'
  `bg2` and `mpeg` and of the name entry's `text_box` and `cursor` (§5.1,
  §5.6, §12.3), and the `car` slot's z of 5 (§9.5).
- Measured with a scratch program:
  - 10 settle frames after a build;
  - 30 feedback frames on the title (flags 0x0A2);
  - a Fadeout control switch of 40 frames;
  - `ar/top` plays `arcade01` and `stream/arcade_menu_full.pss`;
  - Hard + L1 + R1 gives race code `APAT0001`;
  - a new game's picker lists as in §9.1;
  - the attract idle limit is 1,200 frames, and an interrupted demo returns
    to the title.
