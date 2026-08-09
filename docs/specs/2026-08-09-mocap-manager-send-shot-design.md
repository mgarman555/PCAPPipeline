# Operator Console — "Send shot to Mocap Manager"

**Date:** 2026-08-09
**Status:** built, not yet compiled (Windows is the gate — see *Verify on Windows*)
**Closes:** follow-up #1 ("Call site / UI") of
[2026-06-29 UE 5.8 Mocap Manager integration](2026-06-29-ue58-mocap-manager-integration-design.md)

## Why

`UPCAPMocapBridge` has been able to project a called shot onto Epic's Performance
Capture actors since the 5.8 bump, but nothing called it — `SpawnShotToStage` was
a library function with no button. The operator still had to hand-place an
`ACapturePerformer` per performer and hand-type each Live Link subject, which is
exactly the manual step the bridge exists to remove.

This adds the missing call site: one action in the Operator Console that takes
the shot the operator is *already* looking at and stages it in the level.

## What landed

All in `SPCAPOperatorConsole` (`.h` / `.cpp`) — no other file changed.

- **The button.** `RebuildContext()` grows a row under the RECORD / Next-take
  row: `Send shot to Mocap Manager`, at natural width, with a dim
  `N talent · M props` read-out beside it (what will land, counted off the shot).
  Deliberately its own row and not full width — RECORD stays the loudest thing
  in the pane; this is prep, not capture.
- **Resolution.** `UMocapDatabase::GetActiveShot()` for the shot (the click
  `PushSelectionToDB()`s first, so Active\* matches the visible selection),
  `GEditor->GetEditorWorldContext().World()` for the world — the same editor-world
  access the Call Sheet's Volume-Visualizer spawn uses — and `GatherPropRoster()`
  for `TArray<UPropRosterEntry*>`, a straight copy of the Call Sheet's
  `GatherProps()` Asset Registry enumeration
  (`GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), …)`).
- **Disabled with a reason.** `CanSendShotToMocapManager(FText& OutReason)`
  returns false + operator-facing prose, which the button uses *verbatim* as its
  tooltip. Four blocks: no active shot · no editor world · nothing called to the
  shot · a take is recording. The handler re-checks and, if it somehow fires
  anyway, toasts the reason — the action never silently no-ops.
- **Undo.** The spawn (and the resulting selection) sit inside one
  `FScopedTransaction` — a mis-fire is a single Ctrl-Z for the whole shot, not
  one per actor.
- **Feedback.** An editor toast via `FNotificationInfo` /
  `FSlateNotificationManager` (the plugin's existing pattern, from
  `SPCAPCallSheetPanel::SaveDayConfiguration`) naming the shot and the actor count, plus the
  matching `UE_LOG(LogTemp, …, TEXT("[PCAP] …"))` line.
- **Re-entrancy.** `SentShotKeys` (Production|Day|Session|Shot) remembers what
  has been staged this editor session. A second press relabels the button to
  `Re-send shot to Mocap Manager`, warns in the tooltip, and — if pressed — says
  so in the toast ("this shot was already sent this session, so the level now
  holds duplicates; Ctrl-Z undoes").

## Decisions

- **Warn, don't block, on re-send.** Hard-blocking the second press strands the
  operator after a Ctrl-Z (the level is empty again but the button thinks it
  isn't). Relabel + tooltip + toast is predictable and always recoverable.
  The corollary: `SentShotKeys` is a wording hint, never a correctness gate, and
  it deliberately does not survive an editor restart.
- **Select what landed.** The spawned actors are left selected (`SelectNone` +
  `SelectActor`, matching `SPCAPCallSheetPanel::SpawnVolumeVisualizer`). The
  operator sees exactly what arrived in the outliner and can Delete instead of
  Undo.
- **Blocked while CAPTURING.** Spawning actors into the level mid-take would
  land them in the recording. Same gate the Next-take button already uses.
- **Count off `Shot.Subjects` / `Shot.Props`, not `bIsActive`.** The bridge
  places one performer per *listed* subject and one actor per *listed* prop; it
  does not filter on `FShotSubject::bIsActive`. The enable-gate and the
  `N talent · M props` read-out therefore use the same raw counts, so the label
  can never promise something different from what spawns. (Whether the bridge
  *should* filter is a bridge-side question — see below.)
- **Static enable-state, not an attribute lambda.** `IsEnabled` / `ToolTipText`
  are computed once per `RebuildContext()`, matching how the rest of this file
  works; `PollRecordState` already rebuilds the context on every record-state
  change, and shot selection rebuilds it directly.

## Not yet done

1. **`bIsActive` is ignored.** A subject listed on the shot but toggled inactive
   still spawns a performer, because `UPCAPMocapBridge::SpawnShotToStage` doesn't
   filter. If un-called talent should stay off the stage, that filter belongs in
   the bridge (one `if` in its subject loop), not in a second copy of the rule at
   the call site.
2. **No "clear the stage" counterpart.** Removing a staged shot is Ctrl-Z or a
   manual delete. A `Clear staged actors` action needs the bridge to hand back
   (or tag) what it spawned.
3. **Nothing is written back.** The staged actors aren't recorded anywhere in
   `FShot` / `FTake`, so a later session can't tell the level was staged from
   this shot. That's the same ground as follow-up 2b (adopting the PCap database
   records) and should land there, not here.
4. **Stage placement is the origin.** Every actor spawns at
   `FVector::ZeroVector` (bridge behaviour) — they need laying out by hand.
   Reconciling this with `UStageConfigAsset` is follow-up #3 (Stage alignment).
5. **Docs not updated** — `docs/README.md`'s spec index and
   `docs/tools/operator-console.md` should both mention the action. Left alone
   deliberately: parallel sessions own those files this pass.

## Verify on Windows

Build gate first (`Development Editor / Win64`), then restart the editor and open
**Window ▸ Tools ▸ PCAP Tools ▸ Operator Console**.

Compile-risk items, highest first:

1. **`#include "ScopedTransaction.h"` resolves.** First use of `FScopedTransaction`
   anywhere in this plugin. `UnrealEd` is already a private dep, so the header
   should be on the include path — if not, this is the one include to fix.
2. **`FSlateNotificationManager` / `FNotificationInfo` in this TU.** Same include
   pair as `SPCAPCallSheetPanel.cpp`, but new to this file, and this file is in a
   different unity blob.
3. **`GEditor->SelectActor` / `SelectNone` arities** — copied from
   `SPCAPCallSheetPanel::SpawnVolumeVisualizer`, so a mismatch would break that
   file too, but confirm.
4. **`FText::Format` + `FText::AsNumber(int32)`** in the toast strings.

Runtime checks:

5. Select a shot with talent **and** props → the button reads
   `Send shot to Mocap Manager` and the count matches the Talent list.
6. Press it → `ACapturePerformer` per subject + a mesh actor per prop appear in
   the outliner, **selected**; the toast names the shot and the count.
7. Each performer's Live Link subject is the body subject (face when there's no
   body), and a `USkeletalMesh` `DrivenTarget` came through as the mocap mesh.
8. Tracked props carry a `UPCapPropComponent` bound to the prop's subject;
   untracked props place transform-only.
9. **Ctrl-Z once** removes *all* of them, and the transaction reads
   "Send Shot to Mocap Manager" in the undo history.
10. Press again → the button now reads `Re-send shot to Mocap Manager` and the
    toast carries the duplicate warning.
11. Disabled states each show their reason on hover: no shot selected · a shot
    with no talent/props · during CAPTURING · with no level open.
