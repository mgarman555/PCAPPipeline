# PCAP Tool — UI comfort & depth notes

Written during an autonomous block (2026-06-10). Records what I shipped, plus a prioritized menu of
comfort polish and depth/complexity gaps for Madi to pick from. Nothing below is built unless marked
**DONE**.

## Shipped this block
- **DONE — Call Sheet Overview** (`18ea671`): a default dashboard of the active day — context band
  (project · stage · day + counts), called-actor cards (headshot) + called-prop cards (mesh), the
  stage's systems summary, empty states. The "what the operator looks for" view.
- **DONE — Detail popups close on click-outside** (`b20c0bd`): scrim click dismisses; the card
  consumes its own clicks. (X still works.)

## Comfort polish (UI) — prioritized

**Quick wins (each ~minutes, low risk):**
1. **Selected-card emphasis** — the open grid tile should get a green border/highlight (right now it
   relies on the default selection tint). Makes "which card is open" obvious.
2. **Esc closes the detail popup** — pairs with click-outside; standard muscle memory.
3. **Grid empty-states** — when a database has no entries yet, show "No actors yet — type an ID above
   to create one" instead of a blank grid (the Overview already does this; the grids don't).
4. **Hover affordance on cards** — pointer cursor + a subtle highlight so cards read as clickable.
5. **Tooltips** — on the icon/letter buttons (the `X` close, Save/Open/Delete).

**Medium (worth a design pass):**
6. **Centralize the look** — move padding / corner radius / label color / section spacing into
   `SPCAPPanelStyle.h` as named tokens, and apply across every panel, so spacing/typography is
   identical everywhere. Currently each panel hardcodes its own margins.
7. **Quick-call from the card** — a small "Call" corner toggle on actor/prop cards (in the Call Sheet
   context) so you call talent to the day without opening the detail. Faster day-callout.
8. **Detail popup → optional side-drawer** — instead of a center modal that covers the grid, slide a
   panel in on the right so the gallery stays visible while you edit. Calmer for rapid edits.
9. **Rounded / consistent thumbnails** — soften the card thumbnails; unify sizes across Overview and
   the grids.

## Depth / complexity gaps (where it's thin) — prioritized

1. **Operator ↔ Call Sheet wiring (the §6 contract)** — *highest value.* The Operator Console's
   per-shot actor/prop pickers should default to the day's **called** pool (with the full DB as
   fallback). The data exists (`FShootDay.CalledActorIDs/PropIDs`); it's just not read by the Operator
   yet. This is what makes the call sheet actually drive the shoot.
2. **"Ready to shoot" feedback** — surface what's missing before a take: no stage set, no actors
   called, a called actor with no Live Link subject, streams not connected. A small status strip in
   the Call Sheet Overview and/or the Operator Console.
3. **Take review / labelling flow** — after a record, the `Reviewing` state is minimal. A clean
   "label this take (Best/Alt/Burn) + notes" prompt would close the capture loop.
4. **Per-shot assembly** — building the shot list and assigning called talent/props per shot. (Note:
   this leans into Realtime-Operator-session territory — coordinate before building.)
5. **Calibration tool** — its own tool (you flagged it as separate). Needs a design pass.
6. **`UMocapDatabase` → `UPCAPDatabase` class rename** — the asset is `MasterPCAPDatabase` but the C++
   class is still `UMocapDatabase`; a full rename (deferred) would remove the "mocap" naming
   inconsistency. Mechanical but touches many files — best done when the parallel HMC session is quiet.

## My recommendation for what's next
If you want **comfort**: do quick wins 1–5 in one pass (they're cheap and make everything feel
finished). If you want **depth**: #1 (Operator reads the call sheet) is the single most valuable thing
left — it's what turns the call sheet from a list into the thing that drives capture.

I can mock any of these in the chat before building.
