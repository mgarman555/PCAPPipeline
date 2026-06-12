#include "SPCAPStageDatabasePanel.h"
#include "StageConfigAsset.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"

#include "Widgets/SOverlay.h"
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
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

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
        return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" · ")) : FString(TEXT("no systems set"));
    }
}

void SPCAPStageDatabasePanel::Construct(const FArguments& InArgs)
{
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

    ChildSlot
    [
        SNew(SOverlay)

        + SOverlay::Slot()
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
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search stages…")).OnTextChanged(this, &SPCAPStageDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(170.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new stage name  ↵")).OnTextCommitted(this, &SPCAPStageDatabasePanel::OnNewStageCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SAssignNew(TileView, STileView<TWeakObjectPtr<UStageConfigAsset>>)
                    .ListItemsSource(&FilteredStages)
                    .OnGenerateTile(this, &SPCAPStageDatabasePanel::OnGenerateTile)
                    .OnSelectionChanged(this, &SPCAPStageDatabasePanel::OnSelectionChanged)
                    .ItemWidth(150.f)
                    .ItemHeight(160.f)
                    .SelectionMode(ESelectionMode::Single)
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedStage.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(400.f).MaxDesiredHeight(470.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadStages();
}

void SPCAPStageDatabasePanel::ReloadStages()
{
    AllStages.Reset();
    TileThumbnails.Empty();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UStageConfigAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
        if (UStageConfigAsset* Entry = Cast<UStageConfigAsset>(AD.GetAsset()))
            AllStages.Add(Entry);

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
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

UStageConfigAsset* SPCAPStageDatabasePanel::CreateStageAsset(const FString& StageName)
{
    if (StageName.IsEmpty()) return nullptr;

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
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPStageDatabasePanel::SaveStageAsset(UStageConfigAsset* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPStageDatabasePanel::DeleteStageAsset(UStageConfigAsset* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPStageDatabasePanel::OnGenerateTile(TWeakObjectPtr<UStageConfigAsset> Item, const TSharedRef<STableViewBase>& Owner)
{
    UStageConfigAsset* E = Item.Get();
    const FString NameText = E ? E->ConfigName : TEXT("(missing)");
    const FString Summary  = StageSummary(E);

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh", "no stage mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (UObject* Asset = (E ? E->StageReferenceMesh.LoadSynchronous() : nullptr))
    {
        TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item);
        if (!Thumb.IsValid())
        {
            Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
            TileThumbnails.Add(Item, Thumb);
        }
        ThumbWidget = Thumb->MakeThumbnailWidget();
    }

    return SNew(STableRow<TWeakObjectPtr<UStageConfigAsset>>, Owner)
        .Padding(4.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).HeightOverride(86.f)[ ThumbWidget ] ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f, 0.f, 0.f)
                [ SNew(STextBlock).Text(FText::FromString(NameText)) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Summary)).ColorAndOpacity(FSlateColor(ColText2)).AutoWrapText(true).Justification(ETextJustify::Center) ]
            ]
        ];
}

void SPCAPStageDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UStageConfigAsset> Item, ESelectInfo::Type)
{
    SelectedStage = Item;
    if (Item.IsValid() && DetailBox.IsValid())
        DetailBox->SetContent(BuildDetailFor(Item.Get()));
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
        if (TileView.IsValid()) TileView->SetSelection(TWeakObjectPtr<UStageConfigAsset>(Created));
    }
}

void SPCAPStageDatabasePanel::CloseDetail()
{
    SelectedStage = nullptr;
    if (TileView.IsValid()) TileView->ClearSelection();
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
                    for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)   // skip the implicit _MAX
                    {
                        const int32 Val = (int32)EnumPtr->GetValueByIndex(i);
                        MB.AddMenuEntry(EnumPtr->GetDisplayNameTextByIndex(i), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Set, Val]()
                            {
                                Set(Val);
                                if (SelectedStage.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedStage.Get()));
                                if (TileView.IsValid()) TileView->RequestListRefresh();
                            })));
                    }
                return MB.MakeWidget();
            })
        ];
}

TSharedRef<SWidget> SPCAPStageDatabasePanel::BuildDetailFor(UStageConfigAsset* Entry)
{
    if (!Entry) return SNew(SBox);

    TWeakObjectPtr<UStageConfigAsset> Weak(Entry);

    TSharedRef<SWidget> Thumb =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh2", "no stage mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = Entry->StageReferenceMesh.LoadSynchronous())
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 110, 110, ThumbnailPool);
        Thumb = DetailThumbnail->MakeThumbnailWidget();
    }

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("StageHdr", "Stage")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(120.f).HeightOverride(120.f)[ Thumb ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("StageName", "Stage / location")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SEditableTextBox).Text(FText::FromString(Entry->ConfigName)).OnTextCommitted_Lambda([this, Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) { Weak->ConfigName = T.ToString(); if (TileView.IsValid()) TileView->RequestListRefresh(); } }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("RefMesh", "Stage reference mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SObjectPropertyEntryBox).AllowedClass(UObject::StaticClass()).DisplayThumbnail(false)
                      .ObjectPath_Lambda([Weak]() { return Weak.IsValid() ? Weak->StageReferenceMesh.ToString() : FString(); })
                      .OnObjectChanged_Lambda([this, Weak](const FAssetData& AD) { if (Weak.IsValid()) { Weak->StageReferenceMesh = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); if (SelectedStage.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedStage.Get())); } }) ]
                ]
            ]

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

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SaveStageAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Open", "Open asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]()
                  { if (Weak.IsValid() && DeleteStageAsset(Weak.Get())) { CloseDetail(); ReloadStages(); } return FReply::Handled(); }) ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
