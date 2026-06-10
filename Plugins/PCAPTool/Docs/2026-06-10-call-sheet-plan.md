# Call Sheet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `Call Sheet` shoot-day prep tool — a left-rail workspace that hosts the Actor/Prop/Stage databases as sections and declares the day's called actors/props.

**Architecture:** One new Slate panel (`SPCAPCallSheetPanel`) registered as a nomad tab. Its left rail swaps the main pane between five sections; Stages/Actors/Props sections reuse the existing DB panels. Day-level callout is stored as ID-string arrays on `FShootDay` and toggled from the Actor/Prop sections. Built in 7 testable slices — slice 1 is a clickable tab.

**Tech Stack:** UE 5.7 C++ editor plugin, Slate UI, `UDataAsset` rosters, `FAutomationTest` for the data-model layer. Compiles on the Windows host (this Mac is 5.4) — each task ends with a build-and-click or `Automation RunTests` verification Madi runs.

**Convention note:** This project keeps docs in `Plugins/PCAPTool/Docs/` and works directly on `main` (Mac authors → main, Windows builds). Commit only your own files; do not touch the HMC structs in `PCAPToolTypes.h`.

**Open decision carried in:** roster folder = `/Game/Mocap/Database/{Actors,Props,Stages}/` (recommended; Task 6). Flagged, easy to change.

---

### Task 1: Call Sheet shell + tab (hosts existing DB panels)

Gives a clickable `Call Sheet` tab with the left rail; Stages/Actors/Props show the real existing panels, Project/Shoot day are placeholders. Old DB tabs stay for now.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/SPCAPCallSheetPanel.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPCallSheetPanel.cpp`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolModule.h`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTool.cpp`

- [ ] **Step 1: Create the panel header**

`SPCAPCallSheetPanel.h`:
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;

/**
 * Call Sheet — the shoot-day prep tool. Left-rail workspace: Context (Project, Stages,
 * Shoot day) + Called out (Actors, Props). Selecting a rail item swaps the main pane.
 * Stages/Actors/Props host the existing database panels; the section IS the database.
 */
class PCAPTOOL_API SPCAPCallSheetPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPCallSheetPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    enum class ESection : uint8 { Project, Stages, ShootDay, Actors, Props };
    ESection Current = ESection::Actors;

    TSharedPtr<SBox> RailBox;
    TSharedPtr<SBox> ContentBox;

    void SelectSection(ESection S);
    TSharedRef<SWidget> BuildRail();
    TSharedRef<SWidget> BuildRailItem(ESection S, const FText& Label);
    TSharedRef<SWidget> BuildSectionContent(ESection S);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
```

- [ ] **Step 2: Create the panel implementation**

`SPCAPCallSheetPanel.cpp`:
```cpp
#include "SPCAPCallSheetPanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PCAPCallSheet"

void SPCAPCallSheetPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            // Header
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SNew(STextBlock).Text(LOCTEXT("Title", "CALL SHEET")).ColorAndOpacity(FSlateColor(ColGreen)) ]
            ]

            // Body — rail + content
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(168.f)
                    [
                        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(6.f))
                        [ SAssignNew(RailBox, SBox) ]
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(6.f, 0.f, 0.f, 0.f))
                [ SAssignNew(ContentBox, SBox) ]
            ]
        ]
    ];

    SelectSection(Current);
}

void SPCAPCallSheetPanel::SelectSection(ESection S)
{
    Current = S;
    if (RailBox.IsValid())    RailBox->SetContent(BuildRail());
    if (ContentBox.IsValid()) ContentBox->SetContent(BuildSectionContent(S));
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildRail()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 2.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("Context", "Context")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Project,  LOCTEXT("Project",  "Project")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Stages,   LOCTEXT("Stages",   "Stages")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::ShootDay, LOCTEXT("ShootDay", "Shoot day")) ]
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 10.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("CalledOut", "Called out")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Actors, LOCTEXT("Actors", "Actors")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Props,  LOCTEXT("Props",  "Props")) ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildRailItem(ESection S, const FText& Label)
{
    const bool bActive = (S == Current);
    return SNew(SButton)
        .ButtonStyle(FAppStyle::Get(), "NoBorder")
        .ContentPadding(FMargin(8.f, 6.f))
        .HAlign(HAlign_Left)
        .OnClicked_Lambda([this, S]() { SelectSection(S); return FReply::Handled(); })
        [
            SNew(STextBlock)
            .Text(Label)
            .ColorAndOpacity(FSlateColor(bActive ? ColGreen : FLinearColor::White))
        ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildSectionContent(ESection S)
{
    switch (S)
    {
        case ESection::Stages: return SNew(SPCAPStageDatabasePanel);
        case ESection::Actors: return SNew(SPCAPActorDatabasePanel);
        case ESection::Props:  return SNew(SPCAPPropDatabasePanel);
        case ESection::Project:
            return SNew(STextBlock).Text(LOCTEXT("ProjStub", "Project picker — Task 4")).ColorAndOpacity(FSlateColor(ColText2));
        case ESection::ShootDay:
        default:
            return SNew(STextBlock).Text(LOCTEXT("DayStub", "Shoot day picker — Task 4")).ColorAndOpacity(FSlateColor(ColText2));
    }
}

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 3: Declare the tab in the module header**

In `PCAPToolModule.h`, add after `ConsoleTabName`:
```cpp
    static const FName CallSheetTabName;
```
and after `SpawnConsoleTab`:
```cpp
    TSharedRef<SDockTab> SpawnCallSheetTab(const FSpawnTabArgs& Args);
```

- [ ] **Step 4: Register the tab in `PCAPTool.cpp`**

Add the include near the other panel includes:
```cpp
#include "SPCAPCallSheetPanel.h"
```
Add the name definition near the others:
```cpp
const FName FPCAPToolModule::CallSheetTabName = TEXT("PCAPTool_CallSheet");
```
In `StartupModule()`, add a registration block (mirrors the Console one):
```cpp
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        CallSheetTabName,
        FOnSpawnTab::CreateRaw(this, &FPCAPToolModule::SpawnCallSheetTab))
        .SetDisplayName(LOCTEXT("CallSheetTabTitle", "Call Sheet"))
        .SetTooltipText(LOCTEXT("CallSheetTabTooltip", "PCAP Tool — shoot-day prep (project, stage, day, actors, props)"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
```
In `ShutdownModule()`, add:
```cpp
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CallSheetTabName);
```
Add the spawn method (mirrors `SpawnConsoleTab`):
```cpp
TSharedRef<SDockTab> FPCAPToolModule::SpawnCallSheetTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SPCAPCallSheetPanel) ];
}
```

- [ ] **Step 5: Verify (Madi, on host)**

Build, restart the editor (tab registration only re-runs on a full restart). Open Window → Tools → `Call Sheet`. Expect: the rail shows Context (Project/Stages/Shoot day) + Called out (Actors/Props); clicking Stages/Actors/Props shows the existing DB panels inside; Project/Shoot day show stub text; the active rail item is green.

- [ ] **Step 6: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/SPCAPCallSheetPanel.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPCallSheetPanel.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolModule.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTool.cpp
git commit -m "feat(pcap): Call Sheet — shell + tab hosting the DB panels"
```

---

### Task 2: Day-level callout data model + helpers + tests

Adds the storage the Operator reads, with automation tests (this layer is unit-testable).

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h` (struct `FShootDay` only — do NOT touch HMC structs)
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/MocapDatabase.h`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/MocapDatabase.cpp`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPDataModelTests.cpp`

- [ ] **Step 1: Add the callout arrays to `FShootDay`**

In `PCAPToolTypes.h`, inside `struct FShootDay`, after `ActiveStageConfig`:
```cpp
    // Day call sheet — what is called for the whole day (roster ID references).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
    TArray<FString> CalledActorIDs;   // → UActorRosterEntry.ActorID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
    TArray<FString> CalledPropIDs;    // → UPropRosterEntry.PropID
```

- [ ] **Step 2: Declare the helpers in `MocapDatabase.h`**

After `GetActiveStageConfig()`:
```cpp
    // Day call sheet — operate on the active day (ActiveProductionCode + ActiveDayID).
    bool IsActorCalled(const FString& ActorID) const;
    void SetActorCalled(const FString& ActorID, bool bCalled);
    bool IsPropCalled(const FString& PropID) const;
    void SetPropCalled(const FString& PropID, bool bCalled);
```

- [ ] **Step 3: Implement them in `MocapDatabase.cpp`**

```cpp
bool UMocapDatabase::IsActorCalled(const FString& ActorID) const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->CalledActorIDs.Contains(ActorID);
}

void UMocapDatabase::SetActorCalled(const FString& ActorID, bool bCalled)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
    {
        if (bCalled) Day->CalledActorIDs.AddUnique(ActorID);
        else         Day->CalledActorIDs.Remove(ActorID);
    }
}

bool UMocapDatabase::IsPropCalled(const FString& PropID) const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->CalledPropIDs.Contains(PropID);
}

void UMocapDatabase::SetPropCalled(const FString& PropID, bool bCalled)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
    {
        if (bCalled) Day->CalledPropIDs.AddUnique(PropID);
        else         Day->CalledPropIDs.Remove(PropID);
    }
}
```

- [ ] **Step 4: Add the automation test**

In `PCAPDataModelTests.cpp`, before `#endif`:
```cpp
// Day call sheet — set/clear/idempotent against the active day.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPDayCalloutTest,
    "PCAP.DataModel.DayCallout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPDayCalloutTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction P; P.ProjectCode = TEXT("DA");
    FShootDay   D; D.DayID       = TEXT("001");
    P.Days.Add(D);
    DB->Productions.Add(P);
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");

    TestFalse(TEXT("not called initially"), DB->IsActorCalled(TEXT("kevinDorman")));
    DB->SetActorCalled(TEXT("kevinDorman"), true);
    TestTrue(TEXT("called after set"), DB->IsActorCalled(TEXT("kevinDorman")));
    DB->SetActorCalled(TEXT("kevinDorman"), true);
    TestEqual(TEXT("no dupes"), DB->GetDay(TEXT("DA"), TEXT("001"))->CalledActorIDs.Num(), 1);
    DB->SetActorCalled(TEXT("kevinDorman"), false);
    TestFalse(TEXT("removed after clear"), DB->IsActorCalled(TEXT("kevinDorman")));
    return true;
}
```

- [ ] **Step 5: Verify (Madi, on host)**

Build, then run: `Automation RunTests PCAP.DataModel` (or the Session Frontend). Expect `PCAP.DataModel.DayCallout` PASS alongside the existing tests.

- [ ] **Step 6: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h \
        Plugins/PCAPTool/Source/PCAPTool/Public/MocapDatabase.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/MocapDatabase.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPDataModelTests.cpp
git commit -m "feat(pcap): day-level call sheet (CalledActorIDs/PropIDs) + helpers + test"
```

---

### Task 3: Call toggle in the Actors + Props sections

When a day is active, each actor/prop row in the DB panel shows a Call/Called toggle bound to the Task-2 helpers. Reuses the existing panels' `GetDatabase()` access pattern.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPActorDatabasePanel.cpp` (`OnGenerateRow`)
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPPropDatabasePanel.cpp` (`OnGenerateRow`)

- [ ] **Step 1: Add the settings include (both panels, if absent)**

At the top of each `.cpp`:
```cpp
#include "PCAPToolSettings.h"
#include "Widgets/Input/SCheckBox.h"
```
(`SCheckBox` is already included in the Actor panel; add to the Prop panel.)

- [ ] **Step 2: Add a Call toggle to the Actor row**

In `SPCAPActorDatabasePanel::OnGenerateRow`, add a right-aligned slot to the row's `SHorizontalBox` (after the name `SVerticalBox` slot):
```cpp
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 2.f, 0.f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Item]()
            {
                UPCAPToolSettings* S = UPCAPToolSettings::Get();
                UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
                const FString ID = Item.IsValid() ? Item->ActorID : FString();
                return (DB && DB->IsActorCalled(ID)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([Item](ECheckBoxState NewState)
            {
                UPCAPToolSettings* S = UPCAPToolSettings::Get();
                if (UMocapDatabase* DB = (S ? S->GetDatabase() : nullptr))
                {
                    if (Item.IsValid()) DB->SetActorCalled(Item->ActorID, NewState == ECheckBoxState::Checked);
                }
            })
            [ SNew(STextBlock).Text(LOCTEXT("Call", "Call")) ]
        ]
```
Add `#include "MocapDatabase.h"` if not already present (needed for `IsActorCalled`).

- [ ] **Step 3: Add the same toggle to the Prop row**

In `SPCAPPropDatabasePanel::OnGenerateRow`, the row is currently an `SVerticalBox`. Wrap it in an `SHorizontalBox` so the toggle sits to the right:
```cpp
    return SNew(STableRow<TWeakObjectPtr<UPropRosterEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 4.f, 2.f, 0.f)
            [ SNew(STextBlock).Text(FText::FromString(IDText)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 2.f, 4.f)
            [ SNew(STextBlock).Text(FText::FromString(NameText)).ColorAndOpacity(FSlateColor(ColText2)) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 2.f, 0.f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Item]()
            {
                UPCAPToolSettings* S = UPCAPToolSettings::Get();
                UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
                const FString ID = Item.IsValid() ? Item->PropID : FString();
                return (DB && DB->IsPropCalled(ID)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([Item](ECheckBoxState NewState)
            {
                UPCAPToolSettings* S = UPCAPToolSettings::Get();
                if (UMocapDatabase* DB = (S ? S->GetDatabase() : nullptr))
                {
                    if (Item.IsValid()) DB->SetPropCalled(Item->PropID, NewState == ECheckBoxState::Checked);
                }
            })
            [ SNew(STextBlock).Text(LOCTEXT("Call", "Call")) ]
        ]
    ];
```
Add `#include "MocapDatabase.h"` to the Prop panel if absent.

- [ ] **Step 4: Verify (Madi, on host)**

Build. With a database assigned and an active project + day set (use the DataAsset editor for now, or Task 4 later), open Call Sheet → Actors. Each actor row has a Call checkbox; toggling it persists (re-open the section — state holds). Same for Props.

- [ ] **Step 5: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPActorDatabasePanel.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPPropDatabasePanel.cpp
git commit -m "feat(pcap): Call Sheet — per-row Call toggle wired to the day call sheet"
```

---

### Task 4: Project + Shoot day section editors

Replace the two stubs with real pickers that set the active project/day and create new ones (the file/folder CRUD requirement).

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/SPCAPCallSheetPanel.h`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPCallSheetPanel.cpp`

- [ ] **Step 1: Declare the builders**

In `SPCAPCallSheetPanel.h`, add private methods:
```cpp
    TSharedRef<SWidget> BuildProjectSection();
    TSharedRef<SWidget> BuildShootDaySection();
    class UMocapDatabase* GetDB() const;
```

- [ ] **Step 2: Implement `GetDB` and the Project section**

In `SPCAPCallSheetPanel.cpp` add includes:
```cpp
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
```
```cpp
UMocapDatabase* SPCAPCallSheetPanel::GetDB() const
{
    UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetDatabase() : nullptr;
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildProjectSection()
{
    UMocapDatabase* DB = GetDB();
    const FString Active = DB ? DB->ActiveProductionCode : FString();

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("PickProject", "Active project")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Active.IsEmpty() ? TEXT("(none)") : Active)) ]
            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(true, nullptr);
                if (UMocapDatabase* D = GetDB())
                {
                    TArray<FProduction>& Prods = D->Productions;
                    Prods.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
                    for (const FProduction& P : Prods)
                    {
                        const FString Code = P.ProjectCode;
                        MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s — %s"), *Code, *P.ProductionName)),
                            FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Code]()
                            {
                                if (UMocapDatabase* D2 = GetDB()) { D2->ActiveProductionCode = Code; }
                                SelectSection(ESection::Project);
                            })));
                    }
                }
                return MB.MakeWidget();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("NewProject", "New project (code)")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SEditableTextBox)
            .HintText(LOCTEXT("NewProjHint", "+ project code  ↵"))
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type CommitType)
            {
                if (CommitType != ETextCommit::OnEnter) return;
                const FString Code = T.ToString().TrimStartAndEnd();
                if (Code.IsEmpty()) return;
                if (UMocapDatabase* D = GetDB())
                {
                    if (!D->GetProductionByCode(Code))
                    {
                        FProduction P; P.ProjectCode = Code; P.ProductionName = Code;
                        D->Productions.Add(P);
                        D->MarkPackageDirty();
                    }
                    D->ActiveProductionCode = Code;
                    SelectSection(ESection::Project);
                }
            })
        ];
}
```

- [ ] **Step 3: Implement the Shoot day section**

```cpp
TSharedRef<SWidget> SPCAPCallSheetPanel::BuildShootDaySection()
{
    UMocapDatabase* DB = GetDB();
    const FString Active = DB ? DB->ActiveDayID : FString();

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("PickDay", "Active shoot day")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Active.IsEmpty() ? TEXT("(none)") : (TEXT("Day_") + Active))) ]
            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(true, nullptr);
                if (UMocapDatabase* D = GetDB())
                {
                    if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                    {
                        P->Days.Sort([](const FShootDay& A, const FShootDay& B){ return A.DayID < B.DayID; });
                        for (const FShootDay& Day : P->Days)
                        {
                            const FString Id = Day.DayID;
                            MB.AddMenuEntry(FText::FromString(TEXT("Day_") + Id), FText::GetEmpty(), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([this, Id]()
                                {
                                    if (UMocapDatabase* D2 = GetDB()) { D2->ActiveDayID = Id; }
                                    SelectSection(ESection::ShootDay);
                                })));
                        }
                    }
                }
                return MB.MakeWidget();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("NewDay", "New shoot day (id, e.g. 002)")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SEditableTextBox)
            .HintText(LOCTEXT("NewDayHint", "+ day id  ↵"))
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type CommitType)
            {
                if (CommitType != ETextCommit::OnEnter) return;
                const FString Id = T.ToString().TrimStartAndEnd();
                if (Id.IsEmpty()) return;
                if (UMocapDatabase* D = GetDB())
                {
                    if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                    {
                        if (!D->GetDay(D->ActiveProductionCode, Id))
                        {
                            FShootDay Day; Day.DayID = Id;
                            P->Days.Add(Day);
                            D->MarkPackageDirty();
                        }
                        D->ActiveDayID = Id;
                    }
                    SelectSection(ESection::ShootDay);
                }
            })
        ];
}
```

- [ ] **Step 4: Route the stubs to the new builders**

In `BuildSectionContent`, replace the `Project` and `ShootDay` cases:
```cpp
        case ESection::Project:  return BuildProjectSection();
        case ESection::ShootDay: return BuildShootDaySection();
```

- [ ] **Step 5: Verify (Madi, on host)**

Build. Call Sheet → Project: pick an existing project or type a new code + Enter (it appears and becomes active). → Shoot day: same. The header context should reflect the active project/day. Then Actors/Props Call toggles now have a valid active day to write to.

- [ ] **Step 6: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/SPCAPCallSheetPanel.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPCallSheetPanel.cpp
git commit -m "feat(pcap): Call Sheet — Project + Shoot day pick/create sections"
```

---

### Task 5: Alphabetical everywhere (retro-fix)

Apply the global sort rule to the two non-compliant spots.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPOperatorConsole.cpp`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPDatabasePanel.cpp`

- [ ] **Step 1: Sort the Operator Console pickers**

In `SPCAPOperatorConsole.cpp`, in `BuildProductionPicker`'s `OnGetMenuContent` lambda, before the `for (const FProduction& P : DB->Productions)` loop, take a sorted copy:
```cpp
                TArray<FProduction> Prods = DB->Productions;
                Prods.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
```
and iterate `Prods` instead of `DB->Productions`. Apply the same pattern in `BuildDayPicker` (sort `P->Days` copy by `DayID`) and `BuildSessionPicker` (sort `D->Sessions` copy by `SessionID`), and in `RebuildShotList` (sort the `Session->Shots` copy by `ShotID` before adding to `ShotItems`).

- [ ] **Step 2: Sort the Mocap Database tree**

In `SPCAPDatabasePanel.cpp` `RebuildTree`, sort each child collection (productions by `ProjectCode`, days by `DayID`, sessions by `SessionID`, shots by `ShotID`, and roster actors/props/stages by their ID) before adding child nodes. Use `Array.Sort(...)` on local copies at each level where the tree is built.

- [ ] **Step 3: Verify (Madi, on host)**

Build. Operator Console pickers and the Mocap Database tree list everything alphabetically.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPOperatorConsole.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPDatabasePanel.cpp
git commit -m "feat(pcap): alphabetical order in Operator Console pickers + Mocap tree"
```

---

### Task 6: Database folder consolidation

Move the three roster create/load paths under one parent.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPActorDatabasePanel.cpp`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPPropDatabasePanel.cpp`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPStageDatabasePanel.cpp`

- [ ] **Step 1: Repath the create functions**

In `CreateActorAsset`, change:
```cpp
const FString PackageName = FString::Printf(TEXT("/Game/Mocap/_Roster/Actors/%s"), *ActorID);
```
to:
```cpp
const FString PackageName = FString::Printf(TEXT("/Game/Mocap/Database/Actors/%s"), *ActorID);
```
In `CreatePropAsset`: `/Game/Mocap/_Roster/Props/%s` → `/Game/Mocap/Database/Props/%s`.
In `CreateStageAsset`: `/Game/Mocap/_Roster/StageConfigs/%s` → `/Game/Mocap/Database/Stages/%s`.

(The panels load via the Asset Registry by class, not by path, so loading is unaffected — only the create destination moves. `Content/Mocap` is empty, so no migration.)

- [ ] **Step 2: Verify (Madi, on host)**

Build. Create a new actor/prop/stage from Call Sheet; confirm the asset lands under `Content/Mocap/Database/{Actors,Props,Stages}/`.

- [ ] **Step 3: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPActorDatabasePanel.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPPropDatabasePanel.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/SPCAPStageDatabasePanel.cpp
git commit -m "feat(pcap): consolidate rosters under /Game/Mocap/Database"
```

---

### Task 7: Remove the standalone DB tabs (cleanup)

Once the Call Sheet sections are proven, the Actor/Prop/Stage databases live only inside Call Sheet.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolModule.h`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTool.cpp`

- [ ] **Step 1: Remove the three tab registrations**

In `PCAPTool.cpp` `StartupModule()`, delete the `RegisterNomadTabSpawner` blocks for `ActorDBTabName`, `PropDBTabName`, `StageDBTabName`. In `ShutdownModule()`, delete their `UnregisterNomadTabSpawner` calls. Delete the `SpawnActorDBTab`, `SpawnPropDBTab`, `SpawnStageDBTab` methods and their `FName` definitions. In `PCAPToolModule.h`, delete the three `FName` statics and the three `Spawn*` declarations. Keep the includes (the panels are still used by Call Sheet).

- [ ] **Step 2: Verify (Madi, on host)**

Build, restart. The standalone Actor/Prop/Stage Database tabs are gone from Window → Tools; they appear only inside Call Sheet. `Mocap Database` (read-only) and `Operator Console` remain.

- [ ] **Step 3: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolModule.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTool.cpp
git commit -m "refactor(pcap): fold Actor/Prop/Stage DBs into Call Sheet (drop standalone tabs)"
```

---

## Self-review

**Spec coverage:** §2 toolset (Task 1 add tab, Task 7 remove old) ✓ · §3 left-rail UI (Task 1) ✓ · §4 sections: Stages/Actors/Props hosted (Task 1), Project/Shoot day (Task 4) ✓ · §5 data model (Task 2) ✓ · §6 operator contract = the `CalledActorIDs/PropIDs` it reads (Task 2; Operator-side wiring is its own spec) ✓ · §7.1 alphabetical (Task 5; sorts also added inline in Task 4 pickers) ✓ · §7.2 each DB own UI (Task 1 hosts them) ✓ · §7.3 +new in section (existing panels' create, hosted) ✓ · §8 CRUD (Task 4 create project/day; Task 6 folder) ✓.

**Placeholder scan:** Project/Shoot-day stubs in Task 1 are intentional and replaced in Task 4 (not a TBD). No other placeholders.

**Type consistency:** `IsActorCalled/SetActorCalled/IsPropCalled/SetPropCalled` defined in Task 2 and used unchanged in Task 3 ✓. `GetDB()` defined in Task 4 used by Task 4 sections ✓. `SelectSection/BuildSectionContent` defined Task 1, extended Task 4 ✓.

**Deferred to other specs:** Realtime Operator's call-sheet-prioritized picker; the Calibration tool. Out of scope here.
