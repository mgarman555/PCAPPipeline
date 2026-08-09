# Take Recorder reconciliation — PCAPTool ▸ Mocap Manager take records

**Date:** 2026-08-09
**Status:** Landed (deferral + record write-back); session-state probe pending Windows confirmation

Closes follow-up **#4** of the [2026-06-29 Mocap Manager integration design](2026-06-29-ue58-mocap-manager-integration-design.md) — *"decide whether `PCAPTakeRecorderSubsystem` defers to the Mocap Manager's recorder when the official session is active."*

## Why

UE 5.8's Mocap Manager ships its own recorder, and it drives **the same single Take Recorder transport** PCAPTool drives. Two controllers on one transport is not a cosmetic conflict — it means a double-record, or a take stopped halfway through a performance. On a shoot day that is an unrepeatable loss.

Separately, takes recorded through PCAPTool were invisible to the Mocap Manager's own Review tab, because nothing ever wrote an `FPCapTakeRecord` row.

## The deferral policy

There is one transport, and whoever started the take owns it.

| Mode | Behaviour |
|---|---|
| `Auto` *(default)* | Defer whenever an official session is detected. PCAPTool stops driving and **observes** instead. |
| `AlwaysDefer` | Never drive. For a shoot run entirely from the Mocap Manager, with PCAPTool present for its databases, HMC/VCam and take records. |
| `NeverDefer` | PCAPTool always drives. Escape hatch for a false positive — it removes the guard, so it logs a warning when set. |

**Observe mode** is the interesting half. `TakeRecorderStarted` fires for *every* take on the transport, including the Mocap Manager's. When a take starts that PCAPTool did not start, it is adopted read-only: stamped with the active shot's identity, harvested into an `FTake` exactly as if PCAPTool had started it, and published — but `StopRecord()` refuses to touch it. The Mocap Manager operator keeps control of their own transport.

If there is no active shot to attribute a foreign take to, PCAPTool stays out entirely rather than inventing a shot to hang it on.

### Detection

Two signals, in order:

1. **The session-state probe** — reflection against the Mocap Manager's live session state. **Currently returns "unresolvable."** The PCap data-asset and record types were verified against the real 5.8 headers; the runtime object that *holds* live session state was not. Rather than guess a class path and silently return a wrong answer, the probe reports that it could not resolve and logs once.
2. **Transport ownership** *(the working fallback)* — a recording is in flight that PCAPTool did not start. Directly observable, and correct regardless of who owns it.

The fallback is sufficient on its own: it is precisely the condition we must not add a second controller to. Confirming the session holder upgrades detection from reactive to predictive — PCAPTool could then grey out RECORD *before* the Mocap Manager starts, rather than refusing at the moment of the press.

## The mapping — `FTake` → `FPCapTakeRecord`

Field names verified against the real `PCapDatabase.h`.

| PCAPTool | Epic | Note |
|---|---|---|
| `MasterSequence` | `RecordedTake` | `TSoftObjectPtr<ULevelSequence>` |
| `RecordedAt` | `DateTimeCreated` | |
| `DurationSeconds` | `TakeDurationSeconds` | |
| `Label` | `TakeStatus` | Best→ThumbsUp, Burn→ThumbsDown, else Neutral |
| `Notes` | `SlateNote` on the matching `FPCapSlateRecord` | |
| `TakeID` | row name | find-or-create, so a re-publish updates in place |

Two traps worth recording, both caught by reading the headers rather than inferring:

- **`EPCapTakeStatus::Neutral` is `2`, not `0`.** The enumerators are `{ ThumbsUp=0, ThumbsDown=1, Neutral=2 }`. Writing the enum as a zero-defaulted raw byte would mark every take ThumbsUp.
- **The takes DataTable is per-session, not global.** It hangs off `FPCapSessionRecord::TakesDataTable` (and slates off `SessionSlateTable`). Resolving "the takes table" by scanning the Asset Registry for a table whose row structure is `PCapTakeRecord` picks an arbitrary session's table and silently writes takes into the wrong session.

## What landed

- `EPCAPRecorderDeferral` + the deferral policy, wired into `StartRecordForActiveShot` (refuses with an operator-facing reason), `StopRecord` (never stops a foreign take), and `HandleTakeStarted` (enters observe mode).
- `UPCAPTakeRecordWriter` — find-or-create `FPCapTakeRecord` / `FPCapSlateRecord` rows by take ID, with the label→status mapping, all through reflection against the private Workflow module.
- Publish on harvest, and **re-publish on `FinishReview()`** — the row written at harvest still carries the default `Captured`, so the label the operator sets afterwards has to reach `TakeStatus` separately.
- `GetTransportOwner()` for operator-facing display: `PCAPTool`, `Mocap Manager`, or empty.

A take that fails to publish is still recorded in `UMocapDatabase` — the database remains the record of truth, so a publish failure is logged, never fatal.

## Decisions

- **Conservative by default.** `Auto` defers rather than competing. A missed PCAPTool-driven take is recoverable; a double-record or a cut take is not.
- **Observe rather than ignore.** Deferring does not mean going blind — the take is still harvested and published, so the database stays complete no matter who pressed record.
- **No guessed reflection paths.** An unresolvable probe reports itself as unresolvable. A wrong class path that silently returns `false` would look identical to "no session active," which is the failure mode that causes a double-record.

## Not yet done

1. **Confirm the session-state holder** on the Windows install and wire signal 1.
2. **Per-stream asset refs.** `BodyAnimAssets` / `FaceAnimAssets` / `AudioAssets` are bindings inside the recorded sequence; only `MasterSequence` is resolved at harvest.
3. **Audio source arming** (`TakeRecorderMicrophoneAudioSource`) — still the next reflection pass, unchanged by this work.
4. **Operator Console surface** for the deferral mode and transport owner — the API is there, nothing displays it yet.

## Verify on Windows

Reflection names are only as good as their first run. Confirm, in order:

- [ ] `/Script/PerformanceCaptureWorkflow.PCapTakeRecord` and `.PCapSlateRecord` resolve as `UScriptStruct`s.
- [ ] `FPCapSessionRecord::TakesDataTable` / `SessionSlateTable` resolve, and the session binding finds the *active* session's tables.
- [ ] The resolved table's row struct matches before any write (a mismatched row write corrupts the asset).
- [ ] `EPCapTakeStatus` resolves by name and Neutral maps to `2`.
- [ ] Record a take from PCAPTool → an `FPCapTakeRecord` row appears in the Mocap Manager's Review tab.
- [ ] Label it Best → re-publish updates `TakeStatus` in place rather than adding a second row.
- [ ] Start a take from the **Mocap Manager** → PCAPTool shows `Capturing`, transport owner reads `Mocap Manager`, RECORD refuses with the deferral message, and STOP in PCAPTool does nothing.
- [ ] That observed take is harvested into `UMocapDatabase` with the correct take ID and manifest.
- [ ] Harvest a take, switch session, then label it — the re-publish still finds it (this is why the harvested session ID is tracked rather than read from the current selection).
