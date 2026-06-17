# Operator Console — comfort + the Realtime-Operator merge (coordination note)

**Date:** 2026-06-12 · branch `claude/operator-comfort-design` (off current `main`). Nothing here is on `main`.

## Why this is a coordination note, not a new roadmap
Asked to "design a more comfortable UI / what needs more complexity," I re-fetched and found that
territory is **already an active workstream**: `2026-06-10-ui-comfort-and-depth-notes.md` (commit
`e96bed2`) is a prioritized comfort+depth roadmap, and `8e41995` (active-day readiness band) shipped
from it minutes ago. Writing a second redesign would duplicate it. So this note instead does the one
thing that roadmap is *waiting on* — it flags (depth #4) "Realtime-Operator territory — **coordinate
before building**." This is that coordination.

## The comfort proposal (visual, in chat)
An interactive mockup of a calmer Operator Console was produced in the session chat. It demonstrates,
in three toggleable states:
- **Dominant state bar** — Ready / Capturing / Reviewing reads at a glance (vs. today's small corner
  text); Capturing shows a live elapsed timer + take id.
- **Always-visible health strip** — Body · Face · Audio · VCam rollup with status dots, so readiness
  is glanceable *before* you select a shot. (= roadmap depth #2, "ready to shoot".)
- **Gated record** — Record disables and names the actor + stream when something's down ("kevinDorman ·
  face not connected"), instead of today's silent log-only block.
- **Post-take prompt** — the Reviewing state becomes a real label (Best default) + director/commentator
  notes card. (= roadmap depth #3.)

## What already exists (and shouldn't be rebuilt)
My Realtime Operator branch `claude/gallant-cerf-fe61d0` (commit `7f208af`, reviewed, not compiled)
already implements, as self-contained Slate:
| Roadmap item | Built on the branch as |
|---|---|
| depth #3 — take review / label + notes | the post-take overlay (label chips default Best + Director + Commentator notes → writes `FTake.Label/DirectorNotes/CommentatorNotes`) |
| depth #4 — per-shot assembly | drag-in roster authoring (+ click-add) + per-actor `DrivenTarget` asset search (`SObjectPropertyEntryBox`) |
| depth #2 — ready-to-shoot | a live stream-status resolver (body via Live Link, face via HMC subsystem, audio stored) + the operator health strip |

These were written against the older "Realtime Operator" framing, but the *logic* is current and
matches the Console's needs.

## The one decision (yours)
The locked vision is **one unified Operator Console** ([[pcap-operator-tool-vision]]). So the
recommendation is **fold these three features into `SPCAPOperatorConsole`** — keep the Console's nav
spine + the real `UPCAPTakeRecorderSubsystem` wiring; graft on the review prompt, the per-shot assembly,
and the health strip from the branch. Not a second tab.

Pick one and the build is fast and non-colliding:
1. **Fold-in (recommended).** I rebase onto current `main` and port the three features into the Console
   panel, file-by-file, coordinating with whoever owns `SPCAPOperatorConsole.cpp`.
2. **I prep drop-in patches** for each of the three (smallest first: the take-review prompt) and hand
   them over for someone else to apply.
3. **Hold** — you review the mockup + the branch first, then direct.

## Smallest first step (depth #3 — take-review prompt into the Console)
Self-contained, ~60 lines in `SPCAPOperatorConsole::RebuildContext()`:
- When `GetRecordState() == Reviewing`, render the label chips (Captured/Best/Alt/Burn, Best
  preselected) + two notes boxes in the context pane instead of the talent list.
- **Done** writes `Label` + `DirectorNotes` + `CommentatorNotes` onto the just-recorded take, then calls
  `UPCAPTakeRecorderSubsystem::FinishReview()` to return to Ready.
- **One API gap to close first:** the subsystem harvests the take into the active shot but doesn't
  expose *which* take it was (PendingTakeID is private). Either label the active shot's last take (the
  harvest appends it) or add a 1-line `FString GetLastTakeID() const` getter to the subsystem. The
  getter is cleaner — recommend that.

## Deliberately NOT done
I did **not** edit `SPCAPOperatorConsole.cpp`, the DB panels, or anything on `main` — they're under
active parallel development and the roadmap itself asks to coordinate first. Blind edits there are how
the earlier duplication happened ([[pcap-fetch-before-build]]).
