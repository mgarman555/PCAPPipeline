# PCAPTool — Realtime Operator Tool — Handoff

**For a fresh Claude Code (or developer) session · UE 5.7 C++ plugin · June 2026**

This document is **self-contained** — you should be able to build the Realtime Operator
without the conversation that produced it. It assumes **Phase 1 (data model)** is already
in the codebase (it is — on `main`) and that **Phase 2 (Take Recorder backend)** is being
built in parallel (the RECORD path depends on it; the UI can be built first with record stubbed).

---

## 1. What this tool is

The **Realtime Operator** is the **live capture control console** — the surface the operator
drives *during a shoot* to control everything: see every stream's status, set who is called
and what each performer drives, arm record tracks, and **RECORD / STOP / Next Take**.

It is **one of three separate tools** in the PCAPTool plugin, each its own dockable tab:

| Tool | Class | Role |
|---|---|---|
| **HMC Monitor** | `SPCAPToolPanel` (exists) | HMC device operator — setup + live feeds |
| **Mocap Database** | `SPCAPDatabasePanel` (exists) | Read-only browser — "where things exist" |
| **Realtime Operator** | `SPCAPOperatorPanel` (**build this**) | Live capture control — the console |

Do **not** fold capture controls into the Database UI — that split is deliberate. The Database
UI browses; the Operator controls.

This is "Layer 1 — Realtime Operator" in the pipeline UI architecture.

---

## 2. How it ties into the rest of the system

- **Reads/writes** the `UMocapDatabase` (the project's database DataAsset, found via
  `UPCAPToolSettings::Get()->GetDatabase()`). It drives the *active selection*
  (`ActiveProductionCode/DayID/SessionID/ShotID`) and the active shot's `FShotSubject`s.
- **Drives the Phase 2 Take Recorder backend** for RECORD / STOP / Next Take (see §6).
- **Reads HMC stream status** from `UPCAPToolSubsystem` (the existing HMC device monitor —
  `GetAllDeviceStatuses()`, `GetEffectiveIssueFlags()`), and Live Link subject status for
  body/face/audio.
- Built as **C++ Slate** (recommended — consistent with `SPCAPToolPanel`/`SPCAPDatabasePanel`,
  and fully deliverable as code). A styled WBP is an alternative but needs in-editor assembly.

---

## 3. Data model it uses (already in code — `PCAPToolTypes.h` / `MocapDatabase.h`)

Everything below already exists. **Do not redefine it.**

```cpp
// FShotSubject — one called performer on a shot
FString ActorID;                       // canonical id, e.g. "kevinDorman"
FString CharacterName;
bool    bIsActive;                     // called to this shot
bool    bHasBodyStream;  FBodyStreamEntry BodyStream;   // LiveLinkSubjectName (FName), SuitID, StreamStatus
bool    bHasFaceStream;  FFaceStreamEntry FaceStream;   // LiveLinkSubjectName (FName), DeviceID, StreamStatus
TArray<FAudioStreamEntry> AudioStreams;                  // ChannelID, DeviceLabel, InputLevel, StreamStatus
TSoftObjectPtr<UObject>   DrivenTarget; // ← what this performance drives (mesh/MetaHuman/actor), swappable by SEARCH

// FPropEntry — PropID, bIsTracked, LiveLinkSubjectName, Notes, StreamStatus
// EStreamStatus — Connected | Disconnected | Degraded
// FShot — ShotID, ShotType, Description, Subjects[], Props[], Takes[]
// FTake — TakeID, DayID, ShotID, TakeNumber, SessionID, Label, manifests, asset refs, ...

// UMocapDatabase helpers the Operator will call:
FShot*  GetActiveShot();                       // resolves ActiveProductionCode/DayID/SessionID/ShotID
UStageConfigAsset* GetActiveStageConfig();
FString BuildNextTakeID() const;               // "001003_004" — next take for the active shot
FString BuildTakeAssetPath(TakeID, ActorID, StreamSuffix) const;
```

The editor-widget base `UPCAPToolEditorWidget` already exposes BlueprintCallable CRUD for the
hierarchy + active selection (`SetActiveShot`, `GetShots`, `AddSubjectToShot`,
`SetSubjectActive`, `GetTakesForShot`, …) and `MakeShotSubjectFromRoster` lives on
`UPCAPToolStatics`. Reuse these; don't duplicate the access layer.

---

## 4. What it must do (requirements — from Madi)

1. **Drag-in authoring** — drag actors/props into the active shot (from the roster).
2. **Universal search** — *everything* assignable by search: roster actors/props, stage configs,
   and especially the **DrivenTarget** (search any mesh/MetaHuman/level actor). "Everything and
   every database available by search" for fast change-over.
3. **Reassign driven target fast** — pick/swap what an actor drives via a search box, live, in
   seconds (the "quick change-over" while audio/HMC stay bound to the actor).
4. **Live stream status** — body / face / audio / vcam, per called actor, green/amber/red.
5. **RECORD / STOP** — gated: RECORD disabled unless all active streams are `Connected`.
6. **One-tap Next Take** — a *separate* action that just records the next take number to the
   current shot with the current setup (no reconfiguration). Uses `BuildNextTakeID()`.
7. **State bar** — `READY` / `CAPTURING` / `REVIEWING`; navigation locks during the last two.
8. **Post-take prompt** — on stop, label (Captured/Best/Alt/Burn, default Best) + director/
   commentator notes, then back to READY.

---

## 5. UI layout (Slate)

A single `SPCAPOperatorPanel` (SCompoundWidget), registered as a **"Realtime Operator"** nomad
tab (mirror the tab registration in `PCAPTool.cpp` / `PCAPToolModule.h` — see how
`DatabaseTabName`/`SpawnDatabaseTab` were added).

```
┌──────────────────────────────────────────────────────────────┐
│ STATE BAR (56px)   READY / CAPTURING / REVIEWING               │  dominant
├──────────────────────────────────────────────────────────────┤
│ Active: DA · Day_001 · Session_S01 · Shot_003   [shot picker]  │
├──────────────────────────────────────────────────────────────┤
│ CALLED ACTORS (one row per active FShotSubject):               │
│   ⬤ kevinDorman   body●  face●  audio●   drives:[ search… ]    │  ← drag to add
│   ⬤ …                                                          │
│ PROPS:  lightsaberHiltA  tracked●                              │
├──────────────────────────────────────────────────────────────┤
│ [ RECORD ]  (disabled if any active stream not Connected)      │
│ [ Next Take ]   next: 001003_004                               │
├──────────────────────────────────────────────────────────────┤
│ OPERATOR STRIP (52px): Body | Face | Audio | VCam  (dots)      │
└──────────────────────────────────────────────────────────────┘
Overlays: RECORD overlay (during CAPTURING), Post-Take prompt (during REVIEWING).
```

Stream dot colors: green=Connected, amber=Degraded, red=Disconnected, grey=not active this shot.
Stream names use the actorID convention: `kevinDorman_mocap`, `kevinDorman_hmc`, `kevinDorman_audio_lav`.

For the **search pickers**, use Slate `SAssetSearchBox` / `SObjectPropertyEntryBox` (asset picker
with type filter) for `DrivenTarget`, and a filtered `SSearchBox` + list for roster lookups.

---

## 6. The record path (depends on Phase 2 — `docs/superpowers/specs/2026-06-09-pcap-phase2-take-recorder-design.md`)

Phase 2 provides a record controller (public-API parts: output path from `BuildTakeAssetPath`,
slate/take via `UTakeMetaData`, `UTakeRecorderBlueprintLibrary::StartRecording/StopRecording`,
harvest to `FTake` via `UTakeRecorderSubsystem::Finished`; **hybrid** source arming — a per-stage
Take Recorder preset baseline + per-shot reflection). The Operator calls:

- **RECORD**: validate streams green → controller `StartRecordForActiveShot()`.
- **STOP**: controller `StopRecord()` → REVIEWING.
- **Next Take**: controller `RecordNextTake()` (re-arms same set, increments take number).

Until Phase 2 lands, stub these (log + flip the state bar) so the UI is fully buildable and testable.

---

## 7. Visual design spec (from the original capture-directory handoff)

```
--bg #0D0F0E  --surface #161A18  --surface2 #1E2420  --border #2A3030
--text #E8EDE8  --text2 #7A8A80  --text3 #4A5850
--green #4AE080  --amber #E0A030  --red #E04040  --inactive #3A4440
State bar:  READY→surface2/text2 quiet · CAPTURING→green-dim, pulsing · REVIEWING→amber-dim, static
Fonts: JetBrains Mono for data (TakeIDs, stream names, actorIDs); Inter for labels/buttons.
```
(In Slate these map to `FSlateColor`/`FLinearColor` + `FCoreStyle`/`FAppStyle` text styles; an
exact font match needs a Slate style set — match the palette first, fonts second.)

---

## 8. Build / test / git

- **Build host:** the project compiles via **`origin/main`** (this Mac authors; the build host
  syncs `git reset --hard origin/main`). So **commit to `main` and push** — feature branches are
  invisible to the build. Push guarded: `git fetch`; confirm `origin/main` is an ancestor of HEAD;
  `git push origin HEAD:main`.
- Register the tab, add `SPCAPOperatorPanel.{h,cpp}` — no new `Build.cs` deps for the UI itself
  (Slate/UMG already present); the record path pulls `TakeRecorder`/`TakesCore` (already added).
- Verify in-editor: **Window → Tools → "Realtime Operator"** opens the console.

---

## 9. Suggested task order

1. `SPCAPOperatorPanel` skeleton + "Realtime Operator" tab + state bar (static READY).
2. Active-shot picker + called-actors list (read `GetActiveShot()` subjects) + stream dots.
3. `DrivenTarget` search picker (assign/swap) writing back to the `FShotSubject`.
4. Drag-in authoring (roster → shot) + props row.
5. RECORD gating + STOP + state transitions (record stubbed).
6. Post-take prompt (label + notes) → writes `FTake` label/notes.
7. One-tap Next Take.
8. Wire to the real Phase 2 record controller when it lands.

Keep each step a commit to `main`; build on the build host between steps.
