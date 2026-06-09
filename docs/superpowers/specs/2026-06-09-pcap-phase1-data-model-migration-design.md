# PCAP Tool — Phase 1: Data Model Migration

**Date:** 2026-06-09
**Status:** Design — awaiting review
**Engine:** UE 5.7 · Substrate · plugin module type `Editor`
**Scope owner:** Madi Garman

---

## 1. Context

The June 2026 "Capture Directory Handoff" specifies a complete capture-session data model, roster system, content structure, and UI. On inspection, the `PCAPTool` plugin **already implements** most of that model (the locked May-29 architecture, faithfully built) plus a large, actively-developed HMC telemetry layer that the handoff does not mention.

Three sources of truth were reconciled:

1. **Locked May-29 architecture** (in the `ue-pcap-pipeline` skill) — `FStageConfig` as a struct, `ActorName` as a free string, no roster, 5-layer operator UI.
2. **June handoff** (this migration's target) — `UStageConfigAsset`/roster DataAssets, `ActorID` canonical key, single `WBP_PCAPCapture` UI, `CommentatorNotes`, `_Database`/`_Roster`/`Productions` folders.
3. **The repo** — locked model implemented + a 733-line HMC subsystem, a near-duplicate `UHMCMonitorComponent`, and Slate operator panels. Last commits are all HMC integration (the Technoprops ORION HMC has arrived and is the active sprint).

**Decision (confirmed):** The handoff is canonical. Migrate the existing code to match it, in **phases, data model first**.

This document covers **Phase 1 only**. Take Recorder integration is Phase 2; the `WBP_PCAPCapture` UI is Phase 3.

---

## 2. Locked decisions

| # | Decision | Rationale |
|---|---|---|
| D1 | Handoff is canonical; migrate existing code to it. | User direction. |
| D2 | Phased; **data model first**. | Take Recorder and UI both depend on correct types. |
| D3 | **`ShotID` = slot-only 3-digit** (e.g. `"003"`). `TakeID = DayID + ShotID + "_" + TakeNumber` → `"001003_004"`. Folder = `Shot_003`. | Matches the existing `UPCAPToolStatics::GenerateTakeID` (`Day(3)+Slot(3)_Take(3)`) and the original locked model's 3-digit shot IDs (`001`, `901`). |
| D4 | Names are **exactly the handoff names**, no project prefix → `MocapDatabase`. The `U`/`F` prefixes remain (UHT-mandatory) but are stripped in all editor UI ("Mocap Database"). | User direction; engine constraint. |
| D5 | **`ActorID` is the displayed actor name** across the entire project (e.g. `kevinDorman`). No display-name resolver. `FirstName`/`LastName` are roster metadata only. | User direction; simplifies Phase 1 and Phase 3. |
| D6 | **No asset redirectors needed** — there is no saved `Content/`, no `.uasset`, no persisted `HMCConfig.json` yet. All renames are free source refactors. | Verified by filesystem scan. |
| D7 | Keep the `bHas*` + `meta=(EditCondition=...)` gate pattern for optional struct members. | UE `USTRUCT`s cannot be null; this is the correct idiom and is already in use. |

---

## 3. Scope

### In scope (Phase 1)
- Three new `UDataAsset` classes: `UActorRosterEntry`, `UPropRosterEntry`, `UStageConfigAsset`.
- Field/type changes across `PCAPToolTypes.h` (`ActorID`, `PropID`, `CommentatorNotes`, `StageConfig` → soft-ptr, `FShootDay` override).
- `UPCAPDatabase` → `UMocapDatabase` (class + file rename), roster arrays, active-session state, and helper methods.
- `ActorName` → `ActorID` rename across the HMC layer (both implementations) and the editor-widget API, so everything compiles and stays consistent.
- A pure helper `MakeShotSubjectFromRoster(const UActorRosterEntry*)` so the roster is immediately meaningful.
- Automation tests for the pure helpers.

### Out of scope (deferred)
- **Phase 2:** Take Recorder output-path wiring, per-stream source setup, record/stop hooks, `FTake` creation, RECORD-button gating, the `AudioCapture`/CineCamera Take Recorder source plugin dependencies.
- **Phase 3:** `WBP_PCAPCapture` UI; roster asset-creation tooling; the "call actor → copy roster defaults" workflow buttons; Edit-Roster-Default vs Override-For-Session UI; wiring HMC panel text.
- **Not doing:** de-duplicating `UPCAPToolSubsystem` vs `UHMCMonitorComponent` (unrelated refactor — see §8 Risks).

---

## 4. Detailed design

### 4.1 New DataAsset: `UActorRosterEntry`
New files `Public/ActorRosterEntry.h` / `Private/ActorRosterEntry.cpp`. Saved by the user to `Content/Mocap/_Roster/Actors/[actorID].uasset` (asset-creation tooling is Phase 3). `FirstName`/`LastName` are plain metadata fields — no `GetDisplayName()` helper, since `ActorID` is the displayed name everywhere (D5).

```cpp
UCLASS(BlueprintType)
class PCAPTOOL_API UActorRosterEntry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString ActorID;    // "kevinDorman" — canonical, permanent
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString FirstName;   // metadata only
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString LastName;    // metadata only
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") FBodyStreamEntry DefaultBodyStream;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") FFaceStreamEntry DefaultFaceStream;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") TArray<FAudioStreamEntry> DefaultAudioStreams;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="History")  TArray<FString> ProductionHistory;  // ["DA","TLOU"]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")    FString Notes;
};
```

### 4.2 New DataAsset: `UPropRosterEntry`
New files `Public/PropRosterEntry.h` / `Private/PropRosterEntry.cpp`.

```cpp
UCLASS(BlueprintType)
class PCAPTOOL_API UPropRosterEntry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PropID;             // "lightsaberHiltA"
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;        // "Lightsaber Hilt A"
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsTracked = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bIsTracked"))
    FName DefaultLiveLinkName;   // FName — see field-type convention in §4.4
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> ProductionHistory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Notes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};
```

### 4.3 New DataAsset: `UStageConfigAsset` (was `FStageConfig`)
New files `Public/StageConfigAsset.h` / `Private/StageConfigAsset.cpp`. The `FStageConfig` struct is **deleted** from `PCAPToolTypes.h`; `FRetargetConfig` **stays** a struct (referenced here).

```cpp
UCLASS(BlueprintType)
class PCAPTOOL_API UStageConfigAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ConfigName;          // "Home_Xsens"
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EBodySystem  BodySystem  = EBodySystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EFaceSystem  FaceSystem  = EFaceSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EAudioSystem AudioSystem = EAudioSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EVCamSystem  VCamSystem  = EVCamSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString LiveLinkPresetPath;  // path to .llp
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ETimecodeSource TimecodeSource = ETimecodeSource::Software;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRetargetConfig RetargetChain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Notes;
};
```

### 4.4 Struct changes in `PCAPToolTypes.h`

| Type | Change |
|---|---|
| `FShotSubject` | `ActorName` → **`ActorID`**; `IsActive` → **`bIsActive` default `false`**; keep `bHasBodyStream`/`bHasFaceStream` gates; standardize bool names to `b`-prefix |
| `FPropEntry` | `PropName` → **`PropID`** (ref → `UPropRosterEntry.PropID`); `IsTracked` → **`bIsTracked`** (update its `EditCondition` string and the `LiveLinkSubjectName` gate); **drop** the `bHasStreamStatus` gate so `StreamStatus` is always present (default `Disconnected`, per handoff); `LiveLinkSubjectName` stays `FName` |
| `FTakeSubjectSnapshot` | `ActorName` → **`ActorID`**; `HadBodyStream`/`HadFaceStream` → `bHadBodyStream`/`bHadFaceStream` |
| `FTakePropSnapshot` | `PropName` → **`PropID`**; `WasTracked` → `bWasTracked` |
| `FTake` | **add** `UPROPERTY(...) FString CommentatorNotes;` (after `DirectorNotes`) |
| `FProduction` | `ActiveStageConfig`: `FStageConfig` → **`TSoftObjectPtr<UStageConfigAsset>`** |
| `FShootDay` | **remove** `bOverridesStageConfig`; `ActiveStageConfig`: `FStageConfig` → **`TSoftObjectPtr<UStageConfigAsset>`** (null ⇒ inherit production's) |
| `FStageConfig` | **deleted** (promoted to `UStageConfigAsset`) |

Forward-declare `class UStageConfigAsset;` in `PCAPToolTypes.h` (soft-ptr only — no include needed in the header).

**Field-type convention:** all Live Link subject-name fields stay **`FName`** (`FBodyStreamEntry`/`FFaceStreamEntry`/`FPropEntry`/`UPropRosterEntry.DefaultLiveLinkName`), matching the existing stream entries and Live Link's native key type. The handoff's `FString` notation for these is informal, not a type decision. File/preset *paths* (`LiveLinkPresetPath`) remain `FString`.

### 4.5 `UPCAPDatabase` → `UMocapDatabase`
Rename class and files: `PCAPDatabase.h/.cpp` → `MocapDatabase.h/.cpp`; `.generated.h`; ~46 references including `#include` lines.

```cpp
UCLASS(BlueprintType)
class PCAPTOOL_API UMocapDatabase : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP") TArray<FProduction> Productions;

    // Roster — permanent records (soft refs to the DataAssets in _Roster/)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UActorRosterEntry>> ActorRoster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UPropRosterEntry>>  PropRoster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UStageConfigAsset>> StageConfigs;

    // Active session state (set at session start, cleared at end)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveProductionCode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveDayID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveSessionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveShotID;

    // Existing accessors (retained, return raw ptrs — C++ only)
    FProduction* GetProductionByCode(const FString& ProjectCode);
    FShootDay*   GetDay(const FString& ProjectCode, const FString& DayID);
    FSession*    GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID);
    FShot*       GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID);
    FTake*       GetTake(const FString& ProjectCode, const FString& DayID, const FString& ShotID, const FString& TakeID);
    TArray<FTake*> GetTakesByLabel(ETakeLabel Label);
    TArray<FTake*> GetUnprocessedQueuedTakes();

    // New active-selection accessors (resolve against Active* fields)
    FProduction* GetActiveProduction();
    FShootDay*   GetActiveDay();
    FSession*    GetActiveSession();
    FShot*       GetActiveShot();
    UStageConfigAsset* GetActiveStageConfig() const;   // day override else production

    // New helpers
    FString BuildNextTakeID() const;   // active shot's next take, via UPCAPToolStatics
    FString BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const;
};
```

### 4.6 ID / path rules (the crux — get these exact)

- **TakeID** = `DayID(3) + ShotID(3) + "_" + TakeNumber(3)` → `001` + `003` + `_` + `004` = `001003_004`. (Delegates to existing `UPCAPToolStatics::GenerateTakeID(DayID, ShotID, TakeNumber)`.)
- **`BuildNextTakeID()`** = `GenerateTakeID(ActiveDayID, ActiveShotID, GenerateNextTakeNumber(*GetActiveShot()))`. Returns empty string if no active shot.
- **`BuildTakeAssetPath(TakeID, ActorID, StreamSuffix)`** =
  `/Game/Mocap/Productions/{ActiveProductionCode}/Day_{ActiveDayID}/Session_{ActiveSessionID}/Shot_{ActiveShotID}/{TakeID}/{TakeID}_{ActorID}_{StreamSuffix}`
  - When `ActorID` is **empty**, omit the actor segment → `{TakeID}_{StreamSuffix}` (for `VCam`, `Master`).
  - Example (body): `/Game/Mocap/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_kevinDorman_mocap`
  - Example (VCam): `/Game/Mocap/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_VCam`

### 4.7 HMC layer `ActorName` → `ActorID`
Rename the key field and all uses in **both** implementations (they are near-identical and must stay in lockstep):

- Structs: `FHMCDeviceConfig.ActorName` → `ActorID`, `FHMCDeviceStatus.ActorName` → `ActorID`, `FHMCCameraFeed.ActorName` → `ActorID`.
- `UPCAPToolSubsystem` and `UHMCMonitorComponent`: `CameraFeeds` map keyed by `ActorID`; `AssignActor(DeviceName, NewActorID)`; `GetFeedsForActor(ActorID)`; JSON field `"actorName"` → `"actorID"` in `SaveConfig`/`LoadConfig` (no migration needed — no saved file).
- `UPCAPToolEditorWidget`: `ActorName` parameters/locals that refer to the canonical key → `ActorID`.
- The `FHMCDeviceConfig::WebSocketEndpoint` DEPRECATED field: leave as-is (its comment about save-format compat is now moot but harmless; removing it is out of scope).

### 4.8 Pure roster helper
On `UPCAPToolStatics` (or as a `static` on `UActorRosterEntry`):

```cpp
// In-memory copy of roster defaults into a shot subject. No asset I/O.
static FShotSubject MakeShotSubjectFromRoster(const UActorRosterEntry* Entry);
```
Copies `ActorID`, `DefaultBodyStream`→`BodyStream` (+ sets `bHasBodyStream` if the entry's body subject name is non-empty), `DefaultFaceStream`→`FaceStream` (+gate), `DefaultAudioStreams`→`AudioStreams`. `CharacterName` left blank (shot-level), `bIsActive` = false. This is the data half of "call actor to shot"; the UI button is Phase 3.

---

## 5. Files

**New (6):** `ActorRosterEntry.{h,cpp}`, `PropRosterEntry.{h,cpp}`, `StageConfigAsset.{h,cpp}`.
**Renamed (2):** `PCAPDatabase.{h,cpp}` → `MocapDatabase.{h,cpp}`.
**Modified:** `PCAPToolTypes.h` (structs/enums), `MocapDatabase.cpp` (new accessors/helpers), `PCAPToolStatics.{h,cpp}` (roster helper), `PCAPToolSubsystem.{h,cpp}`, `HMCMonitorComponent.{h,cpp}`, `SHMCSetupPanel.{h,cpp}`, `SHMCPreviewPanel.cpp`, `PCAPToolEditorWidget.{h,cpp}` (ActorID rename + `UMocapDatabase` references).
**No change:** `.uplugin` (no new module deps in Phase 1), `PCAPToolSettings.*`, `PCAPToolModule.h`, `SPCAPToolPanel.*`.

---

## 6. Compatibility / migration
- **No redirectors** — verified no saved assets or config exist (D6). Class and property renames are pure source refactors.
- `U`/`F` prefixes are UHT-mandatory and invisible in editor UI (D4).
- Enum member ordering is unchanged (already `None`-first where the handoff expects it), so no implicit value shifts.

---

## 7. Verification plan
- **Gate 1 — Build:** compiles under UE 5.7 with clean UHT generation. *(Confirmed: this machine has only UE 5.4; the project targets 5.7.4. I cannot self-verify compilation — Madi builds in her 5.7.4 environment and I fix from the error log. Execution proceeds in compile-sized batches to keep that loop tight.)*
- **Gate 2 — Automation tests** (`Spec`/`SIMPLE_AUTOMATION_TEST`) for the deterministic logic:
  - `BuildTakeAssetPath` with and without `ActorID` (actor segment present/omitted).
  - `BuildNextTakeID` against a seeded `FShot` with N existing takes.
  - `GenerateTakeID` slot-3 composition (`"001","003","004"` → `"001003_004"`).
  - `MakeShotSubjectFromRoster` copies defaults and sets gates correctly.
- **Gate 3 — In-editor smoke:** create a `UMocapDatabase` + one `UActorRosterEntry`; confirm new fields render; confirm a `StageConfigs` soft-ptr resolves; confirm `BuildTakeAssetPath` output matches §4.6.

---

## 8. Risks & tech debt
- **HMC duplication.** `UPCAPToolSubsystem` and `UHMCMonitorComponent` are near-identical (register/connect/poll/feeds/AssignActor/Save+Load). The rename must be applied to both, doubling that part of the work and the risk of drift. **Not** consolidating them in Phase 1 (unrelated refactor) — flagged for a separate task.
- **HMC is live, working code.** The `ActorID` rename touches the active sprint. Mitigation: rename is mechanical; Gate 1 + Gate 3 (connect a device, see feeds grouped by ActorID) catch regressions.
- **No local 5.7 build.** This machine has only UE 5.4; the project targets 5.7.4. Gates 1/3 run on Madi's machine. Mitigation: I sequence edits in compile-sized batches and keep changes mechanical so the build-fix loop is short.

---

## 9. Downstream (for context, not this phase)
- **Phase 2:** Take Recorder — uses `BuildTakeAssetPath`/`BuildNextTakeID`; adds `AudioCapture` + CineCamera Take Recorder source plugin deps; record/stop hooks build `FTake` and write to the active `FShot.Takes`.
- **Phase 3:** `WBP_PCAPCapture` on an extended `UPCAPToolEditorWidget`; roster asset tooling; Edit-Default vs Override-Session; keep HMC Slate panels as the operator deep-dive (do not regress to the thin operator strip).
