# HMC Rig Library — Implementation Plan (Sub-project 1)

> **For agentic workers:** authored on macOS with **UE 5.7.4 installed** — verification is a **local `Build.sh` compile** after each chunk (`Result: Succeeded`), plus Madi's Windows build for MSVC-only warnings + runtime. Steps use checkbox syntax.

**Goal:** Make HMC a database/library like Actor/Prop/Stage — a catalog of named physical rigs (`UHMCRigEntry`: Name + Type + IP), browsable in its own panel and selectable in HMC Setup.

**Architecture:** Matches the existing pattern exactly — a `UDataAsset` entry discovered via `AssetRegistry`, a database panel cloned from `SPCAPStageDatabasePanel`, a nomad tab registered in `PCAPTool.cpp`. The rig's **Type** *is* the capture configuration (`ECaptureConfiguration`, extended with `PhoneHeadMount`), so it drives the watcher directly. Pipeline/actor/tuning stay in the Capture tool.

**Tech Stack:** UE 5.7 C++ (`Plugins/PCAPTool`), Slate, AssetRegistry.

---

## File structure
- `Public/PCAPToolTypes.h` — extend `ECaptureConfiguration`; forward-declare `UHMCRigEntry`; add `FHMCDeviceConfig.SourceRig`.
- **New** `Public/HMCRigEntry.h` — `UHMCRigEntry` DataAsset.
- `Private/PCAPToolStatics.cpp` — `GetDefinition` PhoneHeadMount branch.
- **New** `Public/SPCAPHMCDatabasePanel.h` + `Private/SPCAPHMCDatabasePanel.cpp` — clone of the Stage panel for `UHMCRigEntry`.
- `Private/PCAPTool.cpp` — register the HMC Database nomad tab.
- `Private/PCAPToolSubsystem.cpp` — persist `SourceRig` in Save/LoadConfig.
- `Public/SHMCSetupPanel.h` + `Private/SHMCSetupPanel.cpp` — "Add from library" in the Add-Device modal.

---

## Task 1: Extend ECaptureConfiguration to the 4 rig types

**Files:** `Public/PCAPToolTypes.h`

- [ ] **Step 1:** Add `PhoneHeadMount` and a "Tripod" display label:
```cpp
UENUM(BlueprintType)
enum class ECaptureConfiguration : uint8
{
    MonoTripod      UMETA(DisplayName = "Tripod"),
    MonoHeadMount   UMETA(DisplayName = "Mono - Head Mount"),
    StereoHeadMount UMETA(DisplayName = "Stereo - Head Mount"),
    PhoneHeadMount  UMETA(DisplayName = "Phone - Head Mount")
};
```
- [ ] **Step 2:** Local compile (`Build.sh`); expect `Result: Succeeded`. Commit.

---

## Task 2: UHMCRigEntry data asset

**Files:** **Create** `Public/HMCRigEntry.h`

- [ ] **Step 1:**
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // ECaptureConfiguration
#include "HMCRigEntry.generated.h"

// One physical HMC rig. Saved to Content/Mocap/_Roster/HMCRigs/[rigName].uasset.
// Reusable across days/productions; the rig's Type IS its capture configuration.
UCLASS(BlueprintType)
class PCAPTOOL_API UHMCRigEntry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString RigName;            // "Orion"

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    ECaptureConfiguration Type = ECaptureConfiguration::StereoHeadMount;  // mount form-factor = capture config

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString IPAddress;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    TSoftObjectPtr<UTexture2D> Thumbnail;   // optional, for the database card

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString Notes;
};
```
- [ ] **Step 2:** Compile, commit.

---

## Task 3: GetDefinition — PhoneHeadMount branch

**Files:** `Private/PCAPToolStatics.cpp` (`GetDefinition`)

- [ ] **Step 1:** Add a case alongside the others (phone = a mono head-mount with TrueDepth; for now mirror MonoHeadMount defaults — tight face, no board):
```cpp
        case ECaptureConfiguration::PhoneHeadMount:
        case ECaptureConfiguration::MonoHeadMount:
            break;   // tight-face head-mount defaults (no board); refine phone depth on rig
        case ECaptureConfiguration::StereoHeadMount:
            P.bCheckBoard = true;
            break;
        case ECaptureConfiguration::MonoTripod:
        default:
            P.FramingSizeMin = 0.20f; P.FramingSizeMax = 0.60f; P.FramingDriftTol = 0.12f;
            break;
```
(Replace the existing switch body so all four cases are explicit; MonoTripod keeps the looser bands it already had.)
- [ ] **Step 2:** Compile, commit.

---

## Task 4: FHMCDeviceConfig → source-rig link + persistence

**Files:** `Public/PCAPToolTypes.h`, `Private/PCAPToolSubsystem.cpp`

- [ ] **Step 1:** Forward-declare near the top of `PCAPToolTypes.h`: `class UHMCRigEntry;`
- [ ] **Step 2:** In `FHMCDeviceConfig` add:
```cpp
    // The library rig this device was created from (soft — survives rename).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UHMCRigEntry> SourceRig;
```
- [ ] **Step 3:** Persist in `SaveConfig`: `Obj->SetStringField(TEXT("sourceRig"), C.SourceRig.ToSoftObjectPath().ToString());` and `LoadConfig`: `FString SR; if (Obj->TryGetStringField(TEXT("sourceRig"), SR)) Config.SourceRig = TSoftObjectPtr<UHMCRigEntry>(FSoftObjectPath(SR));`
- [ ] **Step 4:** Compile, commit.

---

## Task 5: SPCAPHMCDatabasePanel (clone of the Stage panel)

**Files:** **Create** `Public/SPCAPHMCDatabasePanel.h` + `Private/SPCAPHMCDatabasePanel.cpp`

- [ ] **Step 1:** Clone `SPCAPStageDatabasePanel.{h,cpp}` verbatim, then substitute `UStageConfigAsset → UHMCRigEntry`, `StageConfigs/ → HMCRigs/`, and the detail editors. The browse uses `AssetRegistry.GetAssetsByClass(UHMCRigEntry::StaticClass()->GetClassPathName())`; create uses `CreatePackage` + `NewObject<UHMCRigEntry>(...)` + `FAssetRegistryModule::AssetCreated` (identical to `CreateStageAsset`).
- [ ] **Step 2:** `BuildDetailFor(UHMCRigEntry*)` edits: **RigName** (SEditableTextBox), **Type** (SComboBox over the 4 `ECaptureConfiguration` values, labelled via `StaticEnum`), **IPAddress** (SEditableTextBox), **Thumbnail** (object picker, same as Stage's mesh picker but `UTexture2D`), **Notes** (multiline). The card summary shows `RigName · Type · IP`.
- [ ] **Step 3:** Compile, commit.

---

## Task 6: Register the HMC Database tab

**Files:** `Private/PCAPTool.cpp` (+ its module header for the TabName/Spawner decls)

- [ ] **Step 1:** Mirror the Stage DB tab: `#include "SPCAPHMCDatabasePanel.h"`; `const FName FPCAPToolModule::HMCDBTabName = TEXT("PCAPTool_HMCDB");`; in `StartupModule` `RegisterTabSpawner(HMCDBTabName, FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnHMCDBTab)).SetDisplayName(LOCTEXT("HMCDBTabTitle","HMC Database")).SetTooltipText(LOCTEXT("HMCDBTabTooltip","PCAP — the HMC rig library"))`; `UnregisterNomadTabSpawner(HMCDBTabName)` in shutdown; `TSharedRef<SDockTab> SpawnHMCDBTab(const FSpawnTabArgs&) { return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPHMCDatabasePanel) ]; }` + declare `HMCDBTabName`/`SpawnHMCDBTab` in the module header.
- [ ] **Step 2:** Compile, commit.

---

## Task 7: HMC Setup — "Add from library"

**Files:** `Public/SHMCSetupPanel.h`, `Private/SHMCSetupPanel.cpp`

- [ ] **Step 1:** In the Add-Device modal, add a **rig picker** (SComboBox over `AssetRegistry.GetAssetsByClass(UHMCRigEntry)`), above the manual Name/IP fields. Selecting a rig fills Name + IP and remembers the chosen `UHMCRigEntry*`.
- [ ] **Step 2:** On Connect, when a rig was picked: build the `FHMCDeviceConfig` with `DeviceName = rig->RigName`, `IPAddress = rig->IPAddress`, `CaptureConfig = rig->Type`, `SourceRig = rig`, then `RegisterDevice`. Manual entry still works (no rig → SourceRig empty, CaptureConfig default).
- [ ] **Step 3:** Compile, commit.

---

## Self-review
- **Spec coverage:** HMC library entry Name/Type/IP/thumbnail/notes (T2) · Type=capture-config w/ 4 values (T1) · GetDefinition phone branch (T3) · device→rig link (T4) · database panel (T5) · tab (T6) · Setup "add from library" (T7). MocapDatabase roster is intentionally **not** added — browse is AssetRegistry-based, matching `SPCAPStageDatabasePanel` (no roster dependency for create/browse). Call-sheet flag + cross-DB presets = separate sub-projects.
- **Type consistency:** `UHMCRigEntry`, `ECaptureConfiguration::PhoneHeadMount`, `SourceRig`, `HMCDBTabName`/`SpawnHMCDBTab` consistent across tasks.
- **No placeholders:** new code shown; the panel is an explicit clone-and-substitute of a named existing file.
