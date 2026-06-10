#include "SPCAPActorDatabasePanel.h"
#include "ActorRosterEntry.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "PCAPActorDatabase"

namespace
{
    const FLinearColor ColLabel = FLinearColor(0.478f, 0.541f, 0.502f);
}

void SPCAPActorDatabasePanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // Header
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("Title", "ACTOR DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh")).OnClicked(this, &SPCAPActorDatabasePanel::OnRefreshClicked) ]
                ]
            ]

            // Body — two panes
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)

                // Left: search + new + list
                + SHorizontalBox::Slot().FillWidth(0.42f).Padding(FMargin(6.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Filter actors…")).OnTextChanged(this, &SPCAPActorDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
                        [
                            SNew(SBox).WidthOverride(160.f)
                            [
                                SNew(SEditableTextBox)
                                .HintText(LOCTEXT("NewHint", "+ new actorID  ↵"))
                                .OnTextCommitted(this, &SPCAPActorDatabasePanel::OnNewActorCommitted)
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)
                    [
                        SAssignNew(ListView, SListView<TWeakObjectPtr<UActorRosterEntry>>)
                        .ListItemsSource(&FilteredActors)
                        .OnGenerateRow(this, &SPCAPActorDatabasePanel::OnGenerateRow)
                        .OnSelectionChanged(this, &SPCAPActorDatabasePanel::OnSelectionChanged)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                ]

                // Right: setup form
                + SHorizontalBox::Slot().FillWidth(0.58f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(FormContainer, SBox) ]
                ]
            ]
        ]
    ];

    ReloadActors();
    RebuildForm();
}

// ── Data ────────────────────────────────────────────────────────────────────

void SPCAPActorDatabasePanel::ReloadActors()
{
    AllActors.Reset();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
    {
        if (UActorRosterEntry* Entry = Cast<UActorRosterEntry>(AD.GetAsset()))
        {
            AllActors.Add(Entry);
        }
    }

    AllActors.Sort([](const TWeakObjectPtr<UActorRosterEntry>& A, const TWeakObjectPtr<UActorRosterEntry>& B)
    {
        return A.IsValid() && B.IsValid() && A->ActorID < B->ActorID;
    });

    ApplyFilter();
}

void SPCAPActorDatabasePanel::ApplyFilter()
{
    FilteredActors.Reset();
    for (const TWeakObjectPtr<UActorRosterEntry>& Ptr : AllActors)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty()
            || Ptr->ActorID.Contains(FilterText)
            || Ptr->FirstName.Contains(FilterText)
            || Ptr->LastName.Contains(FilterText))
        {
            FilteredActors.Add(Ptr);
        }
    }
    if (ListView.IsValid()) ListView->RequestListRefresh();
}

UActorRosterEntry* SPCAPActorDatabasePanel::CreateActorAsset(const FString& ActorID)
{
    if (ActorID.IsEmpty()) return nullptr;

    const FString PackageName = FString::Printf(TEXT("/Game/Mocap/_Roster/Actors/%s"), *ActorID);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;   // already taken

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;

    UActorRosterEntry* Entry = NewObject<UActorRosterEntry>(Package, FName(*ActorID), RF_Public | RF_Standalone | RF_Transactional);
    Entry->ActorID = ActorID;

    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPActorDatabasePanel::SaveActorAsset(UActorRosterEntry* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPActorDatabasePanel::DeleteActorAsset(UActorRosterEntry* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

// ── List ──────────────────────────────────────────────────────────────────

TSharedRef<ITableRow> SPCAPActorDatabasePanel::OnGenerateRow(TWeakObjectPtr<UActorRosterEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    UActorRosterEntry* E = Item.Get();
    const FString IDText   = E ? E->ActorID : TEXT("(missing)");
    const FString NameText = E ? FString::Printf(TEXT("%s %s"), *E->FirstName, *E->LastName).TrimStartAndEnd() : FString();

    return SNew(STableRow<TWeakObjectPtr<UActorRosterEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2.f, 4.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(IDText)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(NameText)).ColorAndOpacity(FSlateColor(ColText2)) ]
        ]
    ];
}

void SPCAPActorDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UActorRosterEntry> Item, ESelectInfo::Type)
{
    SelectedActor = Item;
    RebuildForm();
}

void SPCAPActorDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

// ── Form ────────────────────────────────────────────────────────────────────

void SPCAPActorDatabasePanel::RebuildForm()
{
    if (FormContainer.IsValid())
    {
        FormContainer->SetContent(BuildFormFor(SelectedActor.Get()));
    }
}

TSharedRef<SWidget> SPCAPActorDatabasePanel::BuildFormFor(UActorRosterEntry* Entry)
{
    if (!Entry)
    {
        return SNew(STextBlock)
            .Text(LOCTEXT("NoSelection", "Select an actor, or + New to create one."))
            .ColorAndOpacity(FSlateColor(ColText2));
    }

    TWeakObjectPtr<UActorRosterEntry> Weak(Entry);

    // Labelled editable field helper.
    auto Field = [](const FString& Label, const FString& Value, TFunction<void(const FString&)> Commit) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SEditableTextBox)
                .Text(FText::FromString(Value))
                .OnTextCommitted_Lambda([Commit](const FText& T, ETextCommit::Type) { Commit(T.ToString()); })
            ];
    };

    // Labelled asset-picker slot (soft object ref).
    auto AssetSlot = [](const FString& Label, TFunction<FString()> Get, TFunction<void(const FAssetData&)> Set) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UObject::StaticClass())
                .DisplayThumbnail(false)
                .ObjectPath_Lambda(Get)
                .OnObjectChanged_Lambda(Set)
            ];
    };

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SNew(SVerticalBox)

        // ActorID — locked
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("%s   (id locked)"), *Entry->ActorID)))
            .ColorAndOpacity(FSlateColor(ColGreen))
        ]

        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
            [ Field(TEXT("First name"), Entry->FirstName, [Weak](const FString& V){ if (Weak.IsValid()) Weak->FirstName = V; }) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f, 0.f, 0.f)
            [ Field(TEXT("Last name"), Entry->LastName, [Weak](const FString& V){ if (Weak.IsValid()) Weak->LastName = V; }) ]
        ]

        + SVerticalBox::Slot().AutoHeight()
        [ Field(TEXT("Default body — Live Link subject"), Entry->DefaultBodyStream.LiveLinkSubjectName.ToString(),
                [Weak](const FString& V){ if (Weak.IsValid()) Weak->DefaultBodyStream.LiveLinkSubjectName = FName(*V); }) ]
        + SVerticalBox::Slot().AutoHeight()
        [ Field(TEXT("Default body — Suit ID"), Entry->DefaultBodyStream.SuitID,
                [Weak](const FString& V){ if (Weak.IsValid()) Weak->DefaultBodyStream.SuitID = V; }) ]

        + SVerticalBox::Slot().AutoHeight()
        [ Field(TEXT("Default face — Live Link subject"), Entry->DefaultFaceStream.LiveLinkSubjectName.ToString(),
                [Weak](const FString& V){ if (Weak.IsValid()) Weak->DefaultFaceStream.LiveLinkSubjectName = FName(*V); }) ]
        + SVerticalBox::Slot().AutoHeight()
        [ Field(TEXT("Default face — Device ID"), Entry->DefaultFaceStream.DeviceID,
                [Weak](const FString& V){ if (Weak.IsValid()) Weak->DefaultFaceStream.DeviceID = V; }) ]

        // ── Digital double ──
        + SVerticalBox::Slot().AutoHeight()
        [ AssetSlot(TEXT("MetaHuman (driven character)"),
                    [Weak]() { return Weak.IsValid() ? Weak->MetaHuman.ToString() : FString(); },
                    [Weak](const FAssetData& AD) { if (Weak.IsValid()) Weak->MetaHuman = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); }) ]
        + SVerticalBox::Slot().AutoHeight()
        [ AssetSlot(TEXT("Face scan / identity"),
                    [Weak]() { return Weak.IsValid() ? Weak->FaceScan.ToString() : FString(); },
                    [Weak](const FAssetData& AD) { if (Weak.IsValid()) Weak->FaceScan = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Weak]() { return (Weak.IsValid() && Weak->bUseFaceScanOnMetaHuman) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Weak](ECheckBoxState S) { if (Weak.IsValid()) Weak->bUseFaceScanOnMetaHuman = (S == ECheckBoxState::Checked); })
            [ SNew(STextBlock).Text(LOCTEXT("UseFaceScan", "Use face scan on the MetaHuman")) ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("AudioNote", "Audio channels — edit in the full asset")).ColorAndOpacity(FSlateColor(ColLabel)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("NotesLabel", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SMultiLineEditableTextBox)
            .Text(FText::FromString(Entry->Notes))
            .OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->Notes = T.ToString(); })
        ]

        // Actions
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SButton).Text(LOCTEXT("Save", "Save"))
                .OnClicked_Lambda([this, Weak]() { if (Weak.IsValid()) SaveActorAsset(Weak.Get()); return FReply::Handled(); })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SButton).Text(LOCTEXT("OpenFull", "Open full asset"))
                .OnClicked_Lambda([Weak]()
                {
                    if (Weak.IsValid() && GEditor)
                    {
                        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get());
                    }
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton).Text(LOCTEXT("Delete", "Delete"))
                .OnClicked_Lambda([this, Weak]()
                {
                    if (Weak.IsValid() && DeleteActorAsset(Weak.Get()))
                    {
                        SelectedActor = nullptr;
                        ReloadActors();
                        RebuildForm();
                    }
                    return FReply::Handled();
                })
            ]
        ]
    ];
}

// ── Actions ──────────────────────────────────────────────────────────────────

void SPCAPActorDatabasePanel::OnNewActorCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;

    const FString ActorID = Text.ToString().TrimStartAndEnd();
    if (ActorID.IsEmpty()) return;

    if (UActorRosterEntry* Created = CreateActorAsset(ActorID))
    {
        ReloadActors();
        SelectedActor = Created;
        if (ListView.IsValid()) ListView->SetSelection(TWeakObjectPtr<UActorRosterEntry>(Created));
        RebuildForm();
    }
    // If the package already existed, CreateActorAsset returns null — the id is taken.
}

FReply SPCAPActorDatabasePanel::OnRefreshClicked()
{
    ReloadActors();
    RebuildForm();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
