#include "SPCAPStageDatabasePanel.h"
#include "StageConfigAsset.h"
#include "PCAPToolTypes.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "PCAPToolPaths.h"

#define LOCTEXT_NAMESPACE "PCAPStageDatabase"

#include "SPCAPPanelStyle.h"

namespace
{

    // Short "what's recording" summary — non-None systems joined with · .
    FString StageSummary(UStageConfigAsset* S)
    {
        if (!S) return FString();
        TArray<FString> Parts;
        if (S->BodySystem  != EBodySystem::None)  Parts.Add(StaticEnum<EBodySystem>()->GetDisplayNameTextByValue((int64)S->BodySystem).ToString());
        if (S->FaceSystem  != EFaceSystem::None)  Parts.Add(StaticEnum<EFaceSystem>()->GetDisplayNameTextByValue((int64)S->FaceSystem).ToString());
        if (S->AudioSystem != EAudioSystem::None) Parts.Add(StaticEnum<EAudioSystem>()->GetDisplayNameTextByValue((int64)S->AudioSystem).ToString());
        if (S->VCamSystem  != EVCamSystem::None)  Parts.Add(StaticEnum<EVCamSystem>()->GetDisplayNameTextByValue((int64)S->VCamSystem).ToString());
        return FString::Join(Parts, TEXT(" · "));
    }
}

void SPCAPStageDatabasePanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("Title", "STAGE DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh")).OnClicked(this, &SPCAPStageDatabasePanel::OnRefreshClicked) ]
                ]
            ]

            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(0.42f).Padding(FMargin(6.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Filter stages…")).OnTextChanged(this, &SPCAPStageDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
                        [
                            SNew(SBox).WidthOverride(160.f)
                            [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new stage name  ↵")).OnTextCommitted(this, &SPCAPStageDatabasePanel::OnNewStageCommitted) ]
                        ]
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)
                    [
                        SAssignNew(ListView, SListView<TWeakObjectPtr<UStageConfigAsset>>)
                        .ListItemsSource(&FilteredStages)
                        .OnGenerateRow(this, &SPCAPStageDatabasePanel::OnGenerateRow)
                        .OnSelectionChanged(this, &SPCAPStageDatabasePanel::OnSelectionChanged)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(0.58f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(FormContainer, SBox) ]
                ]
            ]
        ]
    ];

    ReloadStages();
    RebuildForm();
}

void SPCAPStageDatabasePanel::ReloadStages()
{
    AllStages.Reset();
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UStageConfigAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
    {
        if (UStageConfigAsset* Entry = Cast<UStageConfigAsset>(AD.GetAsset())) AllStages.Add(Entry);
    }
    AllStages.Sort([](const TWeakObjectPtr<UStageConfigAsset>& A, const TWeakObjectPtr<UStageConfigAsset>& B)
    { return A.IsValid() && B.IsValid() && A->ConfigName < B->ConfigName; });
    ApplyFilter();
}

void SPCAPStageDatabasePanel::ApplyFilter()
{
    FilteredStages.Reset();
    for (const TWeakObjectPtr<UStageConfigAsset>& Ptr : AllStages)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty() || Ptr->ConfigName.Contains(FilterText)) FilteredStages.Add(Ptr);
    }
    if (ListView.IsValid()) ListView->RequestListRefresh();
}

UStageConfigAsset* SPCAPStageDatabasePanel::CreateStageAsset(const FString& StageName)
{
    if (StageName.IsEmpty()) return nullptr;

    // Sanitize for the asset path; keep the typed name as ConfigName.
    FString AssetName = StageName;
    AssetName.ReplaceInline(TEXT(" "), TEXT("_"));
    AssetName = ObjectTools::SanitizeObjectName(AssetName);

    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::StagesDir(), *AssetName);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UStageConfigAsset* Entry = NewObject<UStageConfigAsset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
    Entry->ConfigName = StageName;
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, false, false);
    return Entry;
}

void SPCAPStageDatabasePanel::SaveStageAsset(UStageConfigAsset* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, false, false);
    }
}

bool SPCAPStageDatabasePanel::DeleteStageAsset(UStageConfigAsset* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPStageDatabasePanel::OnGenerateRow(TWeakObjectPtr<UStageConfigAsset> Item, const TSharedRef<STableViewBase>& Owner)
{
    UStageConfigAsset* E = Item.Get();
    const FString NameText = E ? E->ConfigName : TEXT("(missing)");
    const FString Summary  = StageSummary(E);
    return SNew(STableRow<TWeakObjectPtr<UStageConfigAsset>>, Owner)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 4.f, 2.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(NameText)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(FText::FromString(Summary)).ColorAndOpacity(FSlateColor(ColText2)) ]
    ];
}

void SPCAPStageDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UStageConfigAsset> Item, ESelectInfo::Type)
{
    SelectedStage = Item;
    RebuildForm();
}

void SPCAPStageDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPStageDatabasePanel::OnNewStageCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString Name = Text.ToString().TrimStartAndEnd();
    if (Name.IsEmpty()) return;
    if (UStageConfigAsset* Created = CreateStageAsset(Name))
    {
        ReloadStages();
        SelectedStage = Created;
        if (ListView.IsValid()) ListView->SetSelection(TWeakObjectPtr<UStageConfigAsset>(Created));
        RebuildForm();
    }
}

FReply SPCAPStageDatabasePanel::OnRefreshClicked()
{
    ReloadStages();
    RebuildForm();
    return FReply::Handled();
}

void SPCAPStageDatabasePanel::RebuildForm()
{
    if (FormContainer.IsValid()) FormContainer->SetContent(BuildFormFor(SelectedStage.Get()));
}

TSharedRef<SWidget> SPCAPStageDatabasePanel::MakeEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set)
{
    const FText CurrentText = EnumPtr ? EnumPtr->GetDisplayNameTextByValue((int64)Current) : FText::GetEmpty();
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SHorizontalBox::Slot().FillWidth(0.6f)
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(CurrentText) ]
            .OnGetMenuContent_Lambda([this, EnumPtr, Set]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);
                if (EnumPtr)
                {
                    for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)   // skip the implicit _MAX
                    {
                        const int32 Val = (int32)EnumPtr->GetValueByIndex(i);
                        MB.AddMenuEntry(EnumPtr->GetDisplayNameTextByIndex(i), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Set, Val]() { Set(Val); RebuildForm(); })));
                    }
                }
                return MB.MakeWidget();
            })
        ];
}

TSharedRef<SWidget> SPCAPStageDatabasePanel::BuildFormFor(UStageConfigAsset* Entry)
{
    if (!Entry)
    {
        return SNew(STextBlock).Text(LOCTEXT("NoSel", "Select a stage, or + New to create one.")).ColorAndOpacity(FSlateColor(ColText2));
    }

    TWeakObjectPtr<UStageConfigAsset> Weak(Entry);

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(LOCTEXT("StageName", "Stage / location")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text(FText::FromString(Entry->ConfigName)).OnTextCommitted_Lambda([this, Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) { Weak->ConfigName = T.ToString(); if (ListView.IsValid()) ListView->RequestListRefresh(); } }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("Recording", "Looking to record")).ColorAndOpacity(FSlateColor(ColGreen)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
        [ MakeEnumRow(TEXT("Body"),  StaticEnum<EBodySystem>(),  (int32)Entry->BodySystem,  [Weak](int32 V){ if (Weak.IsValid()) Weak->BodySystem  = (EBodySystem)V; }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
        [ MakeEnumRow(TEXT("Face"),  StaticEnum<EFaceSystem>(),  (int32)Entry->FaceSystem,  [Weak](int32 V){ if (Weak.IsValid()) Weak->FaceSystem  = (EFaceSystem)V; }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
        [ MakeEnumRow(TEXT("Audio"), StaticEnum<EAudioSystem>(), (int32)Entry->AudioSystem, [Weak](int32 V){ if (Weak.IsValid()) Weak->AudioSystem = (EAudioSystem)V; }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
        [ MakeEnumRow(TEXT("VCam"),  StaticEnum<EVCamSystem>(),  (int32)Entry->VCamSystem,  [Weak](int32 V){ if (Weak.IsValid()) Weak->VCamSystem  = (EVCamSystem)V; }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ MakeEnumRow(TEXT("Timecode"), StaticEnum<ETimecodeSource>(), (int32)Entry->TimecodeSource, [Weak](int32 V){ if (Weak.IsValid()) Weak->TimecodeSource = (ETimecodeSource)V; }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Preset", "Live Link preset path (.llp)")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text(FText::FromString(Entry->LiveLinkPresetPath)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->LiveLinkPresetPath = T.ToString(); }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Entry->Notes)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->Notes = T.ToString(); }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("RetargetNote", "Retarget chain — edit in the full asset")).ColorAndOpacity(FSlateColor(ColLabel)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SaveStageAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("OpenFull", "Open full asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]() { if (Weak.IsValid() && DeleteStageAsset(Weak.Get())) { SelectedStage = nullptr; ReloadStages(); RebuildForm(); } return FReply::Handled(); }) ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
