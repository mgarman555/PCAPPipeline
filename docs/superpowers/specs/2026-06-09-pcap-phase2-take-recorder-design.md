# PCAP Tool — Phase 2: Take Recorder Integration

**Date:** 2026-06-09
**Status:** Design — verified against engine source; build pending
**Engine:** UE 5.7.4 (API verified against local UE 5.4 source — core Take Recorder API is stable across 5.x; flag any 5.7 deltas at build)
**Depends on:** Phase 1 (data model) + the driven-target amendment below.

---

## 1. Handoff corrections (verified against engine source)

The handoff's Take Recorder section is partly wrong about the API. Verified facts:

- **Entry point:** `UTakeRecorderBlueprintLibrary::StartRecording(ULevelSequence* Base, UTakeRecorderSources* Sources, UTakeMetaData* MetaData, const FTakeRecorderParameters& Params)` → returns `UTakeRecorder*`. Plus `IsRecording()`, `GetActiveRecorder()`, `StopRecording()`, `CancelRecording()`, `GetDefaultParameters()`.
- **Output path is NOT `UTakeRecorderParameters::OutputDirectory`** (doesn't exist). It's `Params.Project` (`FTakeRecorderProjectParameters`):
  - `RootTakeSaveDir` (`FDirectoryPath`, content dir) + `TakeSaveDir` (`FString`, subfolder template supporting `{slate}`/`{take}`/`{year}`…). `GetTakeAssetPath()` composes them.
  - `DefaultSlate`, `bStartAtCurrentTimecode`, `bRecordTimecode`, `RecordingClockSource`.
- **Slate + take number** live on `UTakeMetaData` (`GetSlate()`/`GetTakeNumber()`, with setters) — created via `UTakeMetaData::CreateFromDefaults(Outer, Name)`.
- **Record events** (`StartRecording`/`Stopped`/`Finished`) on the BP library are **deprecated in 5.4** → use **`UTakeRecorderSubsystem`** delegates (`TakeRecorderStarted`/`Stopped`/`Finished`). `Finished` provides the resulting `ULevelSequence*`.
- **Source classes are engine-`Private/`** (un-exported): `UTakeRecorderLiveLinkSource` (`SubjectName` FName), `UTakeRecorderMicrophoneAudioSource`, `UTakeRecorderActorSource`. `UTakeRecorderSources::AddSource(TSubclassOf<UTakeRecorderSource>)` **is** public/Blueprintable, so sources are added by `UClass` (resolved via `/Script/<Module>.<Class>` path) and configured by **`FProperty` reflection** (e.g. set `SubjectName`).

**Build deps to add** — `.uplugin`: enable `"Takes"`. `PCAPTool.Build.cs` PrivateDependencyModuleNames: `"TakeRecorder"`, `"TakesCore"`, `"TakeMovieScene"`, `"LevelSequence"`, `"MovieScene"`, `"MovieSceneTracks"`. (LiveLink source class ships with the LiveLink plugin already enabled; audio/actor sources ship with `Takes`.)

---

## 2. Decisions

| # | Decision | Source |
|---|---|---|
| P2-D1 | **Hybrid source arming**: a per-stage Take Recorder preset (`UTakePreset`) provides the baseline source set; the plugin tailors the armed tracks to the active shot's called subjects each record (reflection). | Madi |
| P2-D2 | **Driven-target** mapping added to the model — each shot subject references "what it drives" (anything, by search). | Madi |
| P2-D3 | **One-tap Next Take** = `BuildNextTakeID` + record with the current armed set, no reconfiguration. | Madi |
| P2-D4 | Harvest via `UTakeRecorderSubsystem::Finished` → build `FTake`, set `MasterSequence`, resolve per-stream asset refs by the Phase-1 naming, append to active `FShot.Takes`, save DB. | Spec |

---

## 3. Driven-target amendment (do first — small, additive)

Each performance drives a target in the scene, swappable by search; "anything" = a soft ref to any asset or placed actor.

`FShotSubject` gains:
```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite)
TSoftObjectPtr<UObject> DrivenTarget;   // mesh / MetaHuman / skeletal / level actor — assigned by search
```
`FTakeSubjectSnapshot` gains the same `DrivenTarget` (record-time provenance). `MakeShotSubjectFromRoster` leaves it empty (driven target is shot/scene-specific, not a roster default). No new include (`TSoftObjectPtr`/`UObject` via `CoreMinimal`).

---

## 4. Record pipeline (new file: `PCAPTakeRecorderController`)

A small editor object (or methods on `UMocapDatabase`/a new `UPCAPTakeController`) that owns the record flow:

1. **Prepare** (on RECORD press, for the active shot):
   - Validate every active subject's streams are `Connected` (green). Block if any `Disconnected`.
   - `Params = GetDefaultParameters()`; set `Params.Project.RootTakeSaveDir.Path = "/Game/Mocap/Productions/{Code}/Day_{Day}/Session_{Sess}/Shot_{Shot}"`, `Params.Project.TakeSaveDir = BuildNextTakeID()` (the take folder).
   - `MetaData = CreateFromDefaults(...)`; set slate = `ShotID`, take number = next take int.
   - `Sources`: start from the stage preset's sources; for each active `FShotSubject`, ensure a LiveLink source exists for `BodyStream`/`FaceStream` subject names (reflection: `AddSource(LiveLinkClass)` + set `SubjectName`); audio sources per `AudioStreams`; actor source for VCam.
2. **Record:** `StartRecording(BaseSequence, Sources, MetaData, Params)`.
3. **Stop:** `StopRecording()`.
4. **Harvest** (`UTakeRecorderSubsystem::Finished(ULevelSequence* Out)`):
   - Build `FTake`: `TakeID = BuildNextTakeID()` snapshot, `DayID/ShotID/TakeNumber/SessionID`, `RecordedAt`, `Label = Captured`.
   - `MasterSequence = Out`. Resolve `BodyAnimAssets`/`FaceAnimAssets`/`AudioAssets`/`VCamAsset` by the Phase-1 asset paths (`BuildTakeAssetPath(TakeID, ActorID, suffix)`).
   - Populate `SubjectManifest`/`PropManifest` from the active shot (incl. `DrivenTarget`).
   - Append to active `FShot.Takes`; `SaveDatabase()`.

**Reflection helpers** (isolate the fragile part in one place, e.g. `UPCAPToolStatics`):
```cpp
static UTakeRecorderSource* AddLiveLinkSource(UTakeRecorderSources* Sources, FName SubjectName);
// finds /Script/LiveLinkSequencer.TakeRecorderLiveLinkSource, AddSource, sets SubjectName via FProperty
```
Audio/actor source class paths + property names to confirm from engine source at build time:
`/Script/TakeRecorderSources.TakeRecorderMicrophoneAudioSource`, `/Script/TakeRecorderSources.TakeRecorderActorSource`.

---

## 5. Verification
- **Gate 1 — Build** (Madi, 5.7.4): compiles with the new module deps. Confirm the `/Script/...` source class paths resolve at runtime (log if `FindObject<UClass>` returns null — that's the 5.7-vs-5.4 risk point).
- **Gate 2 — Smoke:** with a stage preset + a green LiveLink subject, press RECORD → a take records to the correct `/Game/Mocap/Productions/...` folder, named `001003_NNN`, and an `FTake` appears in the active shot with `MasterSequence` set.
- **Gate 3 — Next Take:** one-tap records the next take number to the same shot folder.

---

## 6. Risks
- **Reflection vs private classes** (P2-D1) — the source-arming code can't be compile-verified here; the `/Script` class paths and property names are the likely break points (verify at build, log null lookups).
- **5.7 vs 5.4 API drift** — verified against 5.4; if 5.7 moved a field, the build surfaces it.
- **Take Recorder requires an open Sequencer/level context** for some operations — confirm the editor-utility (no-PIE) path works; fall back to requiring a level loaded.
