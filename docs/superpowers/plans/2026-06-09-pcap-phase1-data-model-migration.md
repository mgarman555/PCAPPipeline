# PCAP Tool — Phase 1: Data Model Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the `PCAPTool` plugin's data model to the June 2026 canonical handoff — roster DataAssets, `ActorID` key, `StageConfig`-as-DataAsset, `UMocapDatabase`, and take-path helpers — without regressing the live HMC layer.

**Architecture:** Pure C++ source refactor (no saved assets exist, so no redirectors). Work proceeds in compile-sized batches; each task leaves the module compiling. The renames lean on the compiler as the completeness check. New logic ships with UE automation tests.

**Tech Stack:** UE 5.7.4, Substrate, plugin module type `Editor`. C++ `USTRUCT`/`UDataAsset`/`UEngineSubsystem`/`UEditorUtilityWidget`. Automation via `Misc/AutomationTest.h`.

**Build reality:** This machine has only UE 5.4; the project targets 5.7.4. **Madi runs every build/test checkpoint in her 5.7.4 environment**; the agent writes code and fixes from the error log. Keep batches small so the loop stays tight.

**Spec:** `docs/superpowers/specs/2026-06-09-pcap-phase1-data-model-migration-design.md`

**Branch:** `session6/phase1-data-model`

---

## File Structure

**New files (6):**
- `Plugins/PCAPTool/Source/PCAPTool/Public/StageConfigAsset.h` — `UStageConfigAsset` (was `FStageConfig`)
- `Plugins/PCAPTool/Source/PCAPTool/Private/StageConfigAsset.cpp`
- `Plugins/PCAPTool/Source/PCAPTool/Public/ActorRosterEntry.h` — `UActorRosterEntry`
- `Plugins/PCAPTool/Source/PCAPTool/Private/ActorRosterEntry.cpp`
- `Plugins/PCAPTool/Source/PCAPTool/Public/PropRosterEntry.h` — `UPropRosterEntry`
- `Plugins/PCAPTool/Source/PCAPTool/Private/PropRosterEntry.cpp`

**Renamed (2):**
- `Public/PCAPDatabase.h` → `Public/MocapDatabase.h` (`UPCAPDatabase` → `UMocapDatabase`)
- `Private/PCAPDatabase.cpp` → `Private/MocapDatabase.cpp`

**Modified:**
- `Public/PCAPToolTypes.h` — session structs (Task 3) + HMC structs (Task 4)
- `Public/PCAPToolStatics.h` / `Private/PCAPToolStatics.cpp` — `MakeShotSubjectFromRoster` (Task 7)
- `Public/PCAPToolEditorWidget.h` / `Private/PCAPToolEditorWidget.cpp` — `ActorID` rename + `UMocapDatabase` refs
- `Public/PCAPToolSubsystem.h` / `Private/PCAPToolSubsystem.cpp` — HMC `ActorID` rename
- `Public/HMCMonitorComponent.h` / `Private/HMCMonitorComponent.cpp` — HMC `ActorID` rename
- `Public/SHMCSetupPanel.h` / `Private/SHMCSetupPanel.cpp`, `Private/SHMCPreviewPanel.cpp` — HMC `ActorID` rename

**New test file (1):**
- `Private/Tests/PCAPDataModelTests.cpp` — automation tests (Task 8)

---

## Task 1: New DataAsset — `UStageConfigAsset`

Creates the DataAsset that replaces the `FStageConfig` struct. Compiles alongside the still-present `FStageConfig` (they coexist until Task 3 deletes the struct).

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/StageConfigAsset.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/StageConfigAsset.cpp`

- [ ] **Step 1: Create the header**

`Public/StageConfigAsset.h`:
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // enums + FRetargetConfig
#include "StageConfigAsset.generated.h"

// Stage hardware configuration. One DataAsset per stage setup
// (saved to Content/Mocap/_Roster/StageConfigs/[configName].uasset).
UCLASS(BlueprintType)
class PCAPTOOL_API UStageConfigAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FString ConfigName;          // "Home_Xsens"
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") EBodySystem  BodySystem  = EBodySystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") EFaceSystem  FaceSystem  = EFaceSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") EAudioSystem AudioSystem = EAudioSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") EVCamSystem  VCamSystem  = EVCamSystem::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FString LiveLinkPresetPath;  // path to .llp
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") ETimecodeSource TimecodeSource = ETimecodeSource::Software;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FRetargetConfig RetargetChain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FString Notes;
};
```

- [ ] **Step 2: Create the cpp**

`Private/StageConfigAsset.cpp`:
```cpp
#include "StageConfigAsset.h"
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build the project in UE 5.7.4. Expected: compiles clean; `UStageConfigAsset` appears in the editor's "Miscellaneous → Data Asset" picker.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/StageConfigAsset.h Plugins/PCAPTool/Source/PCAPTool/Private/StageConfigAsset.cpp
git commit -m "feat(pcap): add UStageConfigAsset DataAsset (replaces FStageConfig struct)"
```

---

## Task 2: New DataAssets — `UActorRosterEntry` and `UPropRosterEntry`

Additive; both reference only existing types from `PCAPToolTypes.h`.

**Files:**
- Create: `Public/ActorRosterEntry.h`, `Private/ActorRosterEntry.cpp`
- Create: `Public/PropRosterEntry.h`, `Private/PropRosterEntry.cpp`

- [ ] **Step 1: Create `Public/ActorRosterEntry.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // FBodyStreamEntry / FFaceStreamEntry / FAudioStreamEntry
#include "ActorRosterEntry.generated.h"

// Permanent per-performer record. Saved to Content/Mocap/_Roster/Actors/[actorID].uasset.
// FirstName/LastName are metadata only — ActorID is the displayed name everywhere.
UCLASS(BlueprintType)
class PCAPTOOL_API UActorRosterEntry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString ActorID;   // "kevinDorman" — canonical, permanent
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString FirstName;  // metadata
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity") FString LastName;   // metadata
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") FBodyStreamEntry DefaultBodyStream;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") FFaceStreamEntry DefaultFaceStream;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults") TArray<FAudioStreamEntry> DefaultAudioStreams;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="History")  TArray<FString> ProductionHistory;  // ["DA","TLOU"]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")    FString Notes;
};
```

- [ ] **Step 2: Create `Private/ActorRosterEntry.cpp`**

```cpp
#include "ActorRosterEntry.h"
```

- [ ] **Step 3: Create `Public/PropRosterEntry.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // EStreamStatus
#include "PropRosterEntry.generated.h"

// Permanent per-prop record. Saved to Content/Mocap/_Roster/Props/[propID].uasset.
UCLASS(BlueprintType)
class PCAPTOOL_API UPropRosterEntry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PropID;        // "lightsaberHiltA"
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;   // "Lightsaber Hilt A"
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsTracked = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bIsTracked"))
    FName DefaultLiveLinkName;   // FName — Live Link's native key type
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> ProductionHistory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Notes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};
```

- [ ] **Step 4: Create `Private/PropRosterEntry.cpp`**

```cpp
#include "PropRosterEntry.h"
```

- [ ] **Step 5: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean; both DataAssets appear in the Data Asset picker.

- [ ] **Step 6: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/ActorRosterEntry.h Plugins/PCAPTool/Source/PCAPTool/Private/ActorRosterEntry.cpp Plugins/PCAPTool/Source/PCAPTool/Public/PropRosterEntry.h Plugins/PCAPTool/Source/PCAPTool/Private/PropRosterEntry.cpp
git commit -m "feat(pcap): add UActorRosterEntry and UPropRosterEntry DataAssets"
```

---

## Task 3: Session-layer type migration (`PCAPToolTypes.h` + editor-widget consumers)

Renames the session structs and deletes `FStageConfig`. The HMC structs (`FHMC*`) are **left untouched** in this task — they migrate in Task 4. The compiler verifies completeness: any missed reference in `PCAPToolEditorWidget.cpp` is a build error.

**Files:**
- Modify: `Public/PCAPToolTypes.h` (session structs only)
- Modify: `Public/PCAPToolEditorWidget.h`, `Private/PCAPToolEditorWidget.cpp`

- [ ] **Step 1: Add the forward declaration in `PCAPToolTypes.h`**

After the existing `class UIKRetargeter;` forward declaration (near line 14), add:
```cpp
class UStageConfigAsset;   // soft-ref'd by FProduction / FShootDay
```

- [ ] **Step 2: Replace `FShotSubject` in `PCAPToolTypes.h`**

Replace the whole struct with (note `ActorName`→`ActorID`, `IsActive`→`bIsActive` default `false`):
```cpp
USTRUCT(BlueprintType)
struct PCAPTOOL_API FShotSubject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorID;            // ref → UActorRosterEntry.ActorID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsActive = false;     // called to this shot

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasBodyStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bHasBodyStream"))
    FBodyStreamEntry BodyStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasFaceStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bHasFaceStream"))
    FFaceStreamEntry FaceStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAudioStreamEntry> AudioStreams;
};
```

- [ ] **Step 3: Replace `FPropEntry` in `PCAPToolTypes.h`**

`PropName`→`PropID`, `IsTracked`→`bIsTracked`, drop the `bHasStreamStatus` gate:
```cpp
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPropEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropID;             // ref → UPropRosterEntry.PropID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsTracked = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bIsTracked"))
    FName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};
```

- [ ] **Step 4: Replace `FTakeSubjectSnapshot` and `FTakePropSnapshot`**

```cpp
USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakeSubjectSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ActorID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CharacterName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHadBodyStream = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHadFaceStream = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> AudioChannels;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakePropSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PropID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWasTracked = false;
};
```

- [ ] **Step 5: Add `CommentatorNotes` to `FTake`**

In `FTake`, immediately after the `DirectorNotes` `UPROPERTY`, add:
```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString CommentatorNotes;   // added June 2026
```

- [ ] **Step 6: Convert `FProduction.ActiveStageConfig` to a soft-ptr**

In `FProduction`, replace:
```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FStageConfig ActiveStageConfig;
```
with:
```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    TSoftObjectPtr<UStageConfigAsset> ActiveStageConfig;
```

- [ ] **Step 7: Convert `FShootDay` stage config to a soft-ptr (drop the override bool)**

In `FShootDay`, replace these two properties:
```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridesStageConfig = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverridesStageConfig"))
    FStageConfig ActiveStageConfig;
```
with (null ⇒ inherit production's config):
```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UStageConfigAsset> ActiveStageConfig;   // null = inherit production
```

- [ ] **Step 8: Delete the `FStageConfig` struct**

Remove the entire `USTRUCT(...) struct PCAPTOOL_API FStageConfig { ... };` block (the `FRetargetConfig` struct directly above it **stays**).

- [ ] **Step 9: Update `PCAPToolEditorWidget.h` / `.cpp`**

Rename every session-subject identifier the compiler now rejects:
- Parameter and local `ActorName` → `ActorID` in `SetSubjectActive`, `RemoveSubjectFromShot` (and any other subject function).
- Member access `Subject.ActorName` → `Subject.ActorID`, `Subject.IsActive` → `Subject.bIsActive`.
- Any `Prop.PropName` → `Prop.PropID`, `Prop.IsTracked` → `Prop.bIsTracked` (in `RemovePropFromShot` / prop helpers).

Do not invent new behavior — only rename to match the new field names.

- [ ] **Step 10: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean. If the compiler flags a missed `ActorName`/`IsActive`/`PropName`/`IsTracked`/`FStageConfig` reference anywhere, rename it and rebuild. **The clean build is the test for this task.**

- [ ] **Step 11: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolEditorWidget.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPToolEditorWidget.cpp
git commit -m "refactor(pcap): migrate session structs to ActorID/PropID + StageConfig soft-ptr; add CommentatorNotes"
```

---

## Task 4: HMC-layer `ActorName` → `ActorID` rename

The risky one — it touches the live HMC code, in **two near-identical implementations** that must stay in lockstep. The compiler enforces completeness.

**Files:**
- Modify: `Public/PCAPToolTypes.h` (HMC structs only: `FHMCDeviceConfig`, `FHMCDeviceStatus`, `FHMCCameraFeed`)
- Modify: `Public/PCAPToolSubsystem.h`, `Private/PCAPToolSubsystem.cpp`
- Modify: `Public/HMCMonitorComponent.h`, `Private/HMCMonitorComponent.cpp`
- Modify: `Public/SHMCSetupPanel.h`, `Private/SHMCSetupPanel.cpp`, `Private/SHMCPreviewPanel.cpp`

- [ ] **Step 1: Rename the field in the three HMC structs (`PCAPToolTypes.h`)**

In `FHMCDeviceConfig`, `FHMCDeviceStatus`, and `FHMCCameraFeed`, rename the member `ActorName` → `ActorID`. Update the trailing comments accordingly (e.g. `// FShotSubject.ActorName — set after connection` → `// FShotSubject.ActorID — set after connection`).

- [ ] **Step 2: Rename in `PCAPToolSubsystem.{h,cpp}`**

Apply consistently (18 occurrences in the cpp, 4 in the header):
- Member access `Config.ActorName`/`Status->ActorName`/`Feed.ActorName` → `.ActorID`.
- `AssignActor(const FString& DeviceName, const FString& NewActorName)` → `NewActorID`; rename the local `OldActorName`/`NewActorName` → `OldActorID`/`NewActorID`.
- `GetFeedsForActor(const FString& ActorName)` → `ActorID`.
- `CameraFeeds` comment "keyed by ActorName" → "keyed by ActorID".
- **JSON key:** in `SaveConfig`/`LoadConfig`, `SetStringField(TEXT("actorName"), ...)` and `TryGetStringField(TEXT("actorName"), ...)` → `TEXT("actorID")`. (No saved file exists, so no migration needed.)

- [ ] **Step 3: Rename in `HMCMonitorComponent.{h,cpp}`**

Identical changes to Step 2 (this class mirrors the subsystem — 18 cpp occurrences, 4 header). Same member, parameter, `CameraFeeds` key comment, and JSON `"actorName"`→`"actorID"`.

- [ ] **Step 4: Rename in the Slate panels**

- `SHMCSetupPanel.h` (1) / `SHMCSetupPanel.cpp` (8): every `ActorName` referring to the HMC device's actor → `ActorID`.
- `SHMCPreviewPanel.cpp` (1): same.

Display text that previously showed the actor name now shows the `ActorID` string (e.g. `kevinDorman`) — this is intended (D5); do not add a resolver.

- [ ] **Step 5: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean. Smoke: with a device registered and assigned to an `ActorID`, confirm its camera feeds still group under that actor (the `CameraFeeds` map now keyed by `ActorID`). **Clean build + feeds grouping correctly = the test.**

- [ ] **Step 6: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolSubsystem.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPToolSubsystem.cpp Plugins/PCAPTool/Source/PCAPTool/Public/HMCMonitorComponent.h Plugins/PCAPTool/Source/PCAPTool/Private/HMCMonitorComponent.cpp Plugins/PCAPTool/Source/PCAPTool/Public/SHMCSetupPanel.h Plugins/PCAPTool/Source/PCAPTool/Private/SHMCSetupPanel.cpp Plugins/PCAPTool/Source/PCAPTool/Private/SHMCPreviewPanel.cpp
git commit -m "refactor(pcap): rename HMC ActorName -> ActorID across subsystem, component, and Slate panels"
```

---

## Task 5: Rename `UPCAPDatabase` → `UMocapDatabase`

Mechanical class + file rename. The compiler enforces completeness across the ~46 references.

**Files:**
- Rename: `Public/PCAPDatabase.h` → `Public/MocapDatabase.h`; `Private/PCAPDatabase.cpp` → `Private/MocapDatabase.cpp`
- Modify: every file with a `PCAPDatabase` / `UPCAPDatabase` reference (notably `PCAPToolEditorWidget.{h,cpp}`)

- [ ] **Step 1: Rename the files (preserve history)**

```bash
cd Plugins/PCAPTool/Source/PCAPTool
git mv Public/PCAPDatabase.h Public/MocapDatabase.h
git mv Private/PCAPDatabase.cpp Private/MocapDatabase.cpp
```

- [ ] **Step 2: Edit `Public/MocapDatabase.h`**

- Class `UPCAPDatabase` → `UMocapDatabase`.
- Generated include `#include "PCAPDatabase.generated.h"` → `#include "MocapDatabase.generated.h"`.

- [ ] **Step 3: Edit `Private/MocapDatabase.cpp`**

- `#include "PCAPDatabase.h"` → `#include "MocapDatabase.h"`.
- Every method scope `UPCAPDatabase::` → `UMocapDatabase::`.

- [ ] **Step 4: Update all other references**

In every remaining file (primarily `PCAPToolEditorWidget.h`/`.cpp`): `#include "PCAPDatabase.h"` → `#include "MocapDatabase.h"`; type `UPCAPDatabase` → `UMocapDatabase` (forward declarations, `GetDatabase()` return type, locals, casts). Search the whole module for `PCAPDatabase` to catch every site.

- [ ] **Step 5: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean. Any lingering `PCAPDatabase`/`UPCAPDatabase` reference is a build error — fix and rebuild. **Clean build = the test.**

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(pcap): rename UPCAPDatabase -> UMocapDatabase (class + files)"
```

---

## Task 6: `UMocapDatabase` — roster arrays, active state, accessors, helpers

Adds the new members and the path/ID helpers. `BuildTakeAssetPath`/`BuildNextTakeID` delegate to the existing `UPCAPToolStatics`.

**Files:**
- Modify: `Public/MocapDatabase.h`, `Private/MocapDatabase.cpp`

- [ ] **Step 1: Add includes + members + declarations to `MocapDatabase.h`**

At the top, add includes:
```cpp
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"
```
Inside the class (after `Productions`), add:
```cpp
    // Roster — permanent records (soft refs to the DataAssets in _Roster/)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UActorRosterEntry>> ActorRoster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UPropRosterEntry>>  PropRoster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster") TArray<TSoftObjectPtr<UStageConfigAsset>> StageConfigs;

    // Active session state (set at session start, cleared at end)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveProductionCode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveDayID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveSessionID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active") FString ActiveShotID;

    // Active-selection accessors (resolve against the Active* fields above)
    FProduction* GetActiveProduction();
    FShootDay*   GetActiveDay();
    FSession*    GetActiveSession();
    FShot*       GetActiveShot();
    UStageConfigAsset* GetActiveStageConfig() const;   // day override else production

    // Take-id / asset-path helpers
    FString BuildNextTakeID() const;
    FString BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const;
```

- [ ] **Step 2: Add include to `MocapDatabase.cpp`**

At the top, add:
```cpp
#include "PCAPToolStatics.h"
```

- [ ] **Step 3: Implement the accessors + helpers in `MocapDatabase.cpp`**

Append:
```cpp
FProduction* UMocapDatabase::GetActiveProduction()
{
    return GetProductionByCode(ActiveProductionCode);
}

FShootDay* UMocapDatabase::GetActiveDay()
{
    return GetDay(ActiveProductionCode, ActiveDayID);
}

FSession* UMocapDatabase::GetActiveSession()
{
    return GetSession(ActiveProductionCode, ActiveDayID, ActiveSessionID);
}

FShot* UMocapDatabase::GetActiveShot()
{
    return GetShot(ActiveProductionCode, ActiveDayID, ActiveSessionID, ActiveShotID);
}

UStageConfigAsset* UMocapDatabase::GetActiveStageConfig() const
{
    UMocapDatabase* Self = const_cast<UMocapDatabase*>(this);
    if (FShootDay* Day = Self->GetActiveDay())
    {
        if (!Day->ActiveStageConfig.IsNull())
        {
            return Day->ActiveStageConfig.LoadSynchronous();
        }
    }
    if (FProduction* Prod = Self->GetActiveProduction())
    {
        if (!Prod->ActiveStageConfig.IsNull())
        {
            return Prod->ActiveStageConfig.LoadSynchronous();
        }
    }
    return nullptr;
}

FString UMocapDatabase::BuildNextTakeID() const
{
    const FShot* Shot = const_cast<UMocapDatabase*>(this)->GetActiveShot();
    if (!Shot)
    {
        return FString();
    }
    const FString TakeNumber = UPCAPToolStatics::GenerateNextTakeNumber(*Shot);
    return UPCAPToolStatics::GenerateTakeID(ActiveDayID, ActiveShotID, TakeNumber);
}

FString UMocapDatabase::BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const
{
    const FString Base = FString::Printf(
        TEXT("/Game/Mocap/Productions/%s/Day_%s/Session_%s/Shot_%s/%s/"),
        *ActiveProductionCode, *ActiveDayID, *ActiveSessionID, *ActiveShotID, *TakeID);

    const FString AssetName = ActorID.IsEmpty()
        ? FString::Printf(TEXT("%s_%s"), *TakeID, *StreamSuffix)
        : FString::Printf(TEXT("%s_%s_%s"), *TakeID, *ActorID, *StreamSuffix);

    return Base + AssetName;
}
```

- [ ] **Step 4: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean; a `UMocapDatabase` asset shows the new Roster/Active fields.

- [ ] **Step 5: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/MocapDatabase.h Plugins/PCAPTool/Source/PCAPTool/Private/MocapDatabase.cpp
git commit -m "feat(pcap): UMocapDatabase roster arrays, active-session state, take-path helpers"
```

---

## Task 7: `UPCAPToolStatics::MakeShotSubjectFromRoster`

Pure in-memory copy of roster defaults into a shot subject — the data half of "call actor to shot." No asset I/O.

**Files:**
- Modify: `Public/PCAPToolStatics.h`, `Private/PCAPToolStatics.cpp`

- [ ] **Step 1: Declare it in `PCAPToolStatics.h`**

Add inside the class:
```cpp
    // In-memory copy of an actor's roster defaults into a shot subject.
    // Sets bHasBodyStream/bHasFaceStream from whether the default subject names are set.
    // CharacterName is left blank (shot-level) and bIsActive defaults to false.
    UFUNCTION(BlueprintCallable, Category="PCAP|Roster")
    static FShotSubject MakeShotSubjectFromRoster(const UActorRosterEntry* Entry);
```
Add a forward declaration above the class:
```cpp
class UActorRosterEntry;
```

- [ ] **Step 2: Implement it in `PCAPToolStatics.cpp`**

Add the include at the top:
```cpp
#include "ActorRosterEntry.h"
```
Append the body:
```cpp
FShotSubject UPCAPToolStatics::MakeShotSubjectFromRoster(const UActorRosterEntry* Entry)
{
    FShotSubject Subject;
    if (!Entry)
    {
        return Subject;
    }

    Subject.ActorID       = Entry->ActorID;
    Subject.CharacterName = FString();
    Subject.bIsActive     = false;

    Subject.BodyStream     = Entry->DefaultBodyStream;
    Subject.bHasBodyStream = !Entry->DefaultBodyStream.LiveLinkSubjectName.IsNone();

    Subject.FaceStream     = Entry->DefaultFaceStream;
    Subject.bHasFaceStream = !Entry->DefaultFaceStream.LiveLinkSubjectName.IsNone();

    Subject.AudioStreams = Entry->DefaultAudioStreams;
    return Subject;
}
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolStatics.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPToolStatics.cpp
git commit -m "feat(pcap): MakeShotSubjectFromRoster — copy roster defaults into a shot subject"
```

---

## Task 8: Automation tests for the pure helpers

Verifies the deterministic logic (Gate 2 in the spec). Tests live in the editor module under a dev-automation guard.

**Files:**
- Create: `Private/Tests/PCAPDataModelTests.cpp`

- [ ] **Step 1: Create the test file**

`Private/Tests/PCAPDataModelTests.cpp`:
```cpp
#include "Misc/AutomationTest.h"
#include "MocapDatabase.h"
#include "ActorRosterEntry.h"
#include "PCAPToolStatics.h"
#include "PCAPToolTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPGenerateTakeIDTest,
    "PCAP.DataModel.GenerateTakeID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPGenerateTakeIDTest::RunTest(const FString&)
{
    TestEqual(TEXT("slot-3 composition"),
        UPCAPToolStatics::GenerateTakeID(TEXT("001"), TEXT("003"), TEXT("004")),
        TEXT("001003_004"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBuildTakeAssetPathTest,
    "PCAP.DataModel.BuildTakeAssetPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBuildTakeAssetPathTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");
    DB->ActiveSessionID      = TEXT("S01");
    DB->ActiveShotID         = TEXT("003");

    TestEqual(TEXT("body path"),
        DB->BuildTakeAssetPath(TEXT("001003_004"), TEXT("kevinDorman"), TEXT("mocap")),
        TEXT("/Game/Mocap/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_kevinDorman_mocap"));

    TestEqual(TEXT("vcam path (no actor segment)"),
        DB->BuildTakeAssetPath(TEXT("001003_004"), FString(), TEXT("VCam")),
        TEXT("/Game/Mocap/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_VCam"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBuildNextTakeIDTest,
    "PCAP.DataModel.BuildNextTakeID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBuildNextTakeIDTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction P; P.ProjectCode = TEXT("DA");
    FShootDay   D; D.DayID       = TEXT("001");
    FSession    S; S.SessionID   = TEXT("S01");
    FShot    Shot; Shot.ShotID   = TEXT("003");
    FTake T1; T1.TakeNumber = TEXT("001");
    FTake T2; T2.TakeNumber = TEXT("002");
    Shot.Takes.Add(T1); Shot.Takes.Add(T2);
    S.Shots.Add(Shot); D.Sessions.Add(S); P.Days.Add(D); DB->Productions.Add(P);

    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");
    DB->ActiveSessionID      = TEXT("S01");
    DB->ActiveShotID         = TEXT("003");

    TestEqual(TEXT("next take id after 2 takes"), DB->BuildNextTakeID(), TEXT("001003_003"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPMakeShotSubjectFromRosterTest,
    "PCAP.DataModel.MakeShotSubjectFromRoster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPMakeShotSubjectFromRosterTest::RunTest(const FString&)
{
    UActorRosterEntry* E = NewObject<UActorRosterEntry>();
    E->ActorID = TEXT("kevinDorman");
    E->DefaultBodyStream.LiveLinkSubjectName = FName(TEXT("kevinDorman_mocap"));
    // DefaultFaceStream left unset → bHasFaceStream should be false

    const FShotSubject Subject = UPCAPToolStatics::MakeShotSubjectFromRoster(E);
    TestEqual(TEXT("actor id copied"),  Subject.ActorID, FString(TEXT("kevinDorman")));
    TestTrue (TEXT("has body stream"),  Subject.bHasBodyStream);
    TestFalse(TEXT("no face stream"),   Subject.bHasFaceStream);
    TestFalse(TEXT("inactive by default"), Subject.bIsActive);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: Build + run automation (Madi)**

Build in 5.7.4. Then run the tests: **Tools → Session Frontend → Automation**, filter `PCAP.DataModel`, Start Tests. (Or commandline: `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests PCAP.DataModel; Quit" -unattended -nopause -testexit="Automation Test Queue Empty"`.)
Expected: all four `PCAP.DataModel.*` tests **PASS**.

- [ ] **Step 3: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPDataModelTests.cpp
git commit -m "test(pcap): automation tests for take-path/id helpers and roster copy"
```

---

## Self-Review

**1. Spec coverage** — every spec §4 item maps to a task:
- §4.1 `UActorRosterEntry` → Task 2 · §4.2 `UPropRosterEntry` → Task 2 · §4.3 `UStageConfigAsset` → Task 1
- §4.4 struct changes → Task 3 (session) + Task 4 (HMC structs)
- §4.5 `UMocapDatabase` rename → Task 5; members/accessors/helpers → Task 6
- §4.6 ID/path rules → Task 6 (impl) + Task 8 (tests)
- §4.7 HMC `ActorID` rename → Task 4
- §4.8 `MakeShotSubjectFromRoster` → Task 7
- §7 verification Gates 1 (build checkpoints, every task) / 2 (Task 8) / 3 (smoke in Tasks 1,2,4,6)

**2. Placeholder scan** — no TBD/TODO; all code blocks complete; rename steps give exact old→new identifiers with the compiler as completeness check.

**3. Type consistency** — `ActorID` (FString), `PropID` (FString), `bIsActive`/`bHasBodyStream`/`bHasFaceStream` (bool), `LiveLinkSubjectName`/`DefaultLiveLinkName` (FName) used identically across Tasks 2–8. `UMocapDatabase`, `UStageConfigAsset`, `UActorRosterEntry`, `UPropRosterEntry` names match between definition and use. `BuildTakeAssetPath`/`BuildNextTakeID`/`MakeShotSubjectFromRoster`/`GetActiveStageConfig` signatures consistent between `.h` declaration and `.cpp` definition and the Task 8 tests.

**Open dependency:** Task 8's `BuildNextTakeID` test assumes `GenerateNextTakeNumber` returns `"003"` for two existing takes (`001`,`002`) — true for both count+1 and max+1. If the existing implementation differs, the test surfaces it (correct behavior for a test).

---

## Notes for the executor
- **No local build.** Every "build checkpoint" runs on Madi's 5.7.4 machine. Do not mark a task complete until she confirms the build (and, for Task 8, the tests) pass.
- **Compiler is the test for renames** (Tasks 3–5). A clean build is the pass condition.
- **Stop-and-fix loop:** if a build fails, read the error log, fix in place, ask for a rebuild — do not push past a red build.
