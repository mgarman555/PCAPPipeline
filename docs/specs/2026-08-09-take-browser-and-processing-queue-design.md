# Take Browser + processing queue — Layer 5, post-take management

**Date:** 2026-08-09
**Status:** Landed; unverified on Windows (no engine on the authoring host)

Builds **Layer 5** of the locked UI architecture — the only layer with no implementation at all. Consumes the `FTake` hierarchy from the [2026-06-09 Phase 1 data model](2026-06-09-pcap-phase1-data-model-migration-design.md) and republishes labels through the writer from the [2026-08-09 take-record reconciliation](2026-08-09-pcap-take-record-reconciliation-design.md).

## Why

Three things were true before this, and each one is worse than it sounds.

**`FTakeProcessingState` was a dead shape.** It has been a field on `FTake` since Phase 1 — five `FProcessingStep` members, four apply flags, queue and completion stamps. Nothing ever wrote a single field of it. The only read in the entire plugin was one `== Pending` test.

**Nothing could set a take label.** Every take is born `Captured`, and the only writer of `FTake::Label` was a `BlueprintCallable` helper with no callers and no Blueprint implementing it. So `Best`, `Alt` and `Burn` were unreachable in the shipped tool — which means the "only Best and Alt are processed" rule had never once been exercised.

Those two compound into the third: **the queue was empty by construction.** `UMocapDatabase::GetUnprocessedQueuedTakes()` correctly implements the locked rule (`Best` or `Alt`, and not yet processed) and has zero callers. Wire a queue to it and it returns nothing, forever, because no take is ever `Best` or `Alt`.

Labelling and queueing are therefore the same feature. Layer 5 could not be built as a viewer over existing state, because there was no existing state.

## What landed

Two files, plus tab registration.

| File | Role |
|---|---|
| `Public/PCAPTakeProcessingQueue.h` · `Private/PCAPTakeProcessingQueue.cpp` | `UPCAPTakeProcessingQueue` — eligibility, step applicability, the state machine, persistence. The first and only writer of `FTakeProcessingState`. |
| `Public/SPCAPTakeBrowser.h` · `Private/SPCAPTakeBrowser.cpp` | `SPCAPTakeBrowser` — the tab. Filters, take list, manifest detail, label chips, per-step controls, the batch bar. |

`UPCAPTakeProcessingQueue` is an engine subsystem with a static working surface that takes the `UMocapDatabase` explicitly, and a Blueprint face over the project's assigned database. The panel decides none of the policy — applicability, legality and the wording that says where a step happens all come from the subsystem, so the UI cannot drift from the rules.

**No change to `PCAPToolTypes.h`.** The existing shape already expresses the whole model, including the asymmetry that looks like a bug until you know the pipeline: there are five steps but only four apply flags, because the Sequencer merge is unconditional.

Also landed: the **Take Browser tab** in the PCAP Tools group (`PCAPTool_TakeBrowser`), registered in `Private/PCAPTool.cpp` with a file-local tab name and a `CreateStatic` spawner rather than the usual `FPCAPToolModule` statics — `Public/PCAPToolModule.h` is owned by another workstream this phase. Behaviour is identical; the spawner carries no module state.

## The state machine

Two levels. Steps are driven; the take-level status is derived and never assigned.

### Admission

A take is **eligible** when it is labelled `Best` or `Alt`, has a real record timestamp, and has not already finished. Each rejection carries an operator-facing reason, so a skipped take is seen being skipped:

| Rejected because | Reason shown |
|---|---|
| Labelled `Burn` | archived raw, never processed |
| Labelled `Captured` | only Best and Alt enter the queue |
| No record timestamp | a seeded shot slot, not a recorded take |
| Already `Complete` | finished |

The record-timestamp test matters more than it looks: every shoot day is seeded with seven placeholder takes on the calibration / test / retarget slots. Label one of those and it would otherwise put an empty slot into the day's worklist.

**Admission computes the step plan from the take's own manifest** and stores it in the apply flags:

| Step | Applies when |
|---|---|
| Body Solve Cleanup | any subject carried a body stream |
| HMC Solve | any subject carried a face stream |
| Body Retarget | any subject carried a body stream |
| Audio Sync / Trim | any subject carries audio channels |
| Merge to Sequencer | always, once admitted |

### Steps

Applicable steps go to `Queued` at admission. That is what disambiguates step-level `Pending`: **post-admission, `Pending` means "does not apply."** There is no `Skipped` enumerator and none was added — see Decisions.

| From | Action | To | Writes |
|---|---|---|---|
| `Queued` | Mark started | `InProgress` | `bHasStarted`, `StartedAt`; clears any error |
| `InProgress` | Mark complete | `Complete` | `bHasCompleted`, `CompletedAt` |
| `Queued` | Mark complete | `Complete` | both stamps at the same instant — the one-click form |
| `Queued` · `InProgress` | Record a problem | `Failed` | `ErrorMessage` (**required**), `CompletedAt` |
| `Failed` · `Complete` | Reset | `Queued` | fully cleared, **and every dependent step with it** |

`Queued → Complete` is legal on purpose: an operator marking a finished step in a single action is two legal transitions applied at once, not a skip. `CompletedAt` means *the step ended* and is stamped on failure too — `FProcessingStep` has no `FailedAt`, and losing the time an overnight batch broke is worse than the slightly loose field name. `bHasCompleted` is the success bit.

Dependencies are direct-and-transitive: the retarget needs the body solve, and the merge needs all four. Body solve, HMC solve and audio sync are independent of each other — they happen at different stations, often in parallel, and ordering them would be a fiction.

### Rollup

`OverallStatus` is always derived, never assigned. Precedence: **`Failed` > `InProgress` > `Queued` > `Complete` > `Pending`.** A day's queue holding one failure reads `Failed`, never "mostly complete". `Complete` requires every applicable step complete; a take that was never admitted reads `Pending`.

**Remove from queue** is the only route back to `Pending` — it clears the stamps, the plan and all five steps. Without it an accidentally-queued take is stuck forever, because every other transition moves away from `Pending`. It deliberately leaves `OutputSequence` alone: that names a real asset, and this layer never invents or discards assets.

### Re-labelling after admission

Nothing stops a queued take being re-labelled, so the label is **re-checked at every transition** rather than trusted from admission. A take queued and then marked `Burn` is refused with a reason and surfaced as stranded — counted in the queue bar and called out in its own detail pane. Dropping it silently is how an operator loses a shot.

## What is in-engine vs operator-attested

This is the part that matters most, and it is stated in the code, in the API, in the tooltips and in the panel body.

**Nothing in this layer performs a solve, and nothing pretends to.**

| Step | Reality | Class |
|---|---|---|
| **Body Solve Cleanup** | Happens in **Shogun Post / Motive**. | Unreal **cannot** do this — ever |
| **HMC Solve** | Happens in **MetaHuman Animator** or the head-cam vendor's tool. | Unreal **cannot** do this — ever |
| Body Retarget | IK Retargeter work, in-engine in principle | No automation wired; no input asset |
| Audio Sync / Trim | Sequence audio offset/trim, in-engine in principle | No automation wired; no audio ever reaches a take |
| Merge to Sequencer | Sequencer assembly, in-engine in principle | No automation wired |

Two of the five are not Unreal operations at all. The other three could be engine work one day, but none of them can run today either: the recorder resolves only `FTake::MasterSequence` and leaves `BodyAnimAssets` / `FaceAnimAssets` / `AudioAssets` empty, so there is no per-stream input for a retarget, a trim or a merge to act on. That is a real distinction — *"external forever"* and *"automation not wired yet"* are different promises — and the UI keeps them apart rather than flattening both into a grey "not supported".

So all five ship as **operator attestation**. The timestamps are testimony about work done elsewhere. The single source for that wording lives on the subsystem and is rendered verbatim beside each step's controls, so no panel can quietly imply the tool did the work.

If in-engine automation is ever wired for the last three, it belongs behind a new entry point that marks the step complete on success. The attestation transitions stay regardless, because they are the only honest model for the two steps that can only ever happen outside the engine.

**"Process All Queued" therefore queues; it does not process.** The locked architecture names that button, so it keeps its name — but it states its full payload before the click, and its result says *"N take(s) queued for processing … Nothing has been processed — each step is now outstanding for the operator."*

## Decisions

- **Attestation over automation.** Modelling an external solve as anything the engine "runs" would be a fabricated result on unrepeatable shoot data. Marking is honest and immediately useful; a fake solve is worse than no tool.
- **Keep the locked button name, move the honesty into the copy.** Renaming a locked architecture item is not ours to do unilaterally. Making the button explain itself is.
- **Labelling landed in Layer 5.** The locked architecture puts it in Layer 4 (Shot List), which does not exist as a tab. Without label chips here the entire layer ships dead. This is a deliberate deviation, recorded here rather than made quietly; it moves out when Layer 4 is built.
- **Did not build on `GetUnprocessedQueuedTakes()`.** Its name says `Queued`; its body tests `Pending` — the never-written default. It is an *admission-candidate* finder, and it is correct at that job, so it is left alone. Queue membership is a separate enumeration over takes that are actually admitted, which also makes retry reachable: a `Failed` take must never route back through `Pending`.
- **Everything is keyed on the five-part coordinate** (production / day / session / shot / take), re-resolved immediately before each read or write. The database hands out raw pointers into `TArray` storage four deep, and the recorder appends to `FShot::Takes` from a Take Recorder callback that can land mid-batch. A cached `FTake*` is a use-after-free that will not reproduce on a quiet desk and will reproduce on a shoot day.
- **`UMocapDatabase::GetTake()` is never used.** Its signature drops `SessionID` and returns the first match across the whole day. Shot slots are 3-digit and reused across sessions, so it resolves the wrong take exactly when a day has two sessions — the case Layer 5's session filter exists to surface. Resolution goes through the session-qualified shot lookup instead.
- **The step plan is computed once, at admission, and never recomputed.** The apply flags are `EditAnywhere`; an operator may have un-ticked a retarget for a delivery. A re-planning second press would silently overwrite that.
- **Writes save, they don't just dirty.** `MarkPackageDirty` alone never reaches disk — one editor crash or one "don't save" and a full day of labels and attestation is gone. That is the single most likely silent failure for this panel, so every mutator flushes by default.
- **No new fields, no new enumerators.** `FTakeProcessingState` is persisted inside a `UDataAsset`; adding a `Skipped` status or a per-subject step is a save-format change that cannot be compile-verified here. The four apply flags already carry "does not apply", and per-subject detail goes in the step's error message.
- **A step enum in pipeline order.** `FTakeProcessingState` declares the merge *before* audio sync, but the merge consumes what the others produce. Anything that walks the steps walks the enum; walking the struct's declaration order would run them wrong.

## Not yet done

1. **Automation tests.** The pure logic — applicability from a manifest, the rollup precedence, each guarded transition, and `Burn` never entering the queue — is all stack-constructible and belongs beside the existing 29 tests in `Private/Tests/`. None are written yet.
2. **No engine execution for anything.** Body retarget, audio sync/trim and the Sequencer merge stay attested until the recorder resolves per-stream asset refs (`BodyAnimAssets` / `FaceAnimAssets` / `AudioAssets`) and audio arming lands — both are open items on the take-record reconciliation, in files owned by another workstream.
3. **Take duration is never written.** The panel shows `—` and says why, rather than a misleading `0.0s`.
4. **Per-subject step granularity.** The HMC solve and the retarget are really per-subject; the struct carries one step each per take. Partial failure across a two-hander lives in the error message. Fixing it properly is a persisted-struct change and a deliberate later decision.
5. **No archive mechanism for `Burn`.** The locked rule is "archived raw and never processed"; the model has no archive field, path or attestation. Exclusion from the queue is implemented; *archived* is not represented, and inventing a path that touches recorded data was out of scope.
6. **Tab statics.** The tab name and spawner are file-local in `PCAPTool.cpp` rather than `FPCAPToolModule` members, to avoid a concurrent edit to `PCAPToolModule.h`. Worth folding back for consistency.
7. **Doc index rows.** The root README tool table, plugin README, and `docs/README.md` per-tool list each need a Take Browser row.

## Verify on Windows

Nothing here has been compiled — there is no engine on the authoring host. In order:

- [ ] **The module compiles.** Both new translation units join the unity blob; confirm no `C4459` shadow (warnings are errors, and this is exactly how the last 5.8 build broke).
- [ ] The panel and the subsystem agree on every call — names, arity, and which surface (static vs. instance) each call site uses.
- [ ] Restart the editor, then Window ▸ Tools ▸ PCAP Tools ▸ **Take Browser** appears and opens.
- [ ] Label a take `Best` → the chip updates, the queue bar payload changes, and the label survives an editor restart (this is the save path, not just the dirty flag).
- [ ] That label reaches the Mocap Manager's Review tab as `TakeStatus`, and a failure to republish logs without undoing the label.
- [ ] "Process All Queued" pre-states its payload, and the count and step breakdown match what actually gets queued.
- [ ] Press it twice — the second press queues nothing and leaves hand-edited apply flags intact.
- [ ] A take with **no face stream** never gets an HMC Solve step; its row reads "not applicable", not "outstanding".
- [ ] `Burn` takes never appear in the batch, and a take re-labelled `Burn` *after* queueing is refused at every step control and counted as stranded in the queue bar.
- [ ] Mark a step failed with an empty message → refused. With a message → the step reads `Failed` and the take rolls up to `Failed`, not "mostly complete".
- [ ] Reset the body solve on a take whose retarget is complete → the retarget and the merge reset with it.
- [ ] Open a day with **two sessions sharing a shot slot** and confirm every label and step write lands on the take you selected. This is the lookup trap; it is invisible in a one-session day.
- [ ] Record a take while the Browser is open — the list picks it up on the poll and nothing dangles.
