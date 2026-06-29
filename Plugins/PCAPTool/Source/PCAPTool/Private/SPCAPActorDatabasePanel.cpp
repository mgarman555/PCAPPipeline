#include "SPCAPActorDatabasePanel.h"
#include "PCAPMocapData.h"
#include "PCAPPerformerExtension.h"
#include "HMCRigEntry.h"      // UHMCRigEntry — extension HMC rig slot
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"

#include "Widgets/SOverlay.h"
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
#include "Widgets/Views/STableRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "FileHelpers.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPActorDatabase"

#include "SPCAPPanelStyle.h"

void SPCAPActorDatabasePanel::Construct(const FArguments& InArgs)
{
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

    ChildSlot
    [
        SNew(SOverlay)

        // ── Gallery ────────────────────────────────────────────────────────
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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "PERFORMER DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search performers…")).OnTextChanged(this, &SPCAPActorDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(180.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new performer  ↵")).OnTextCommitted(this, &SPCAPActorDatabasePanel::OnNewPerformerCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<FPerformerPtr>)
                        .ListItemsSource(&FilteredPerformers)
                        .OnGenerateTile(this, &SPCAPActorDatabasePanel::OnGenerateTile)
                        .OnSelectionChanged(this, &SPCAPActorDatabasePanel::OnSelectionChanged)
                        .ItemWidth(132.f)
                        .ItemHeight(154.f)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return UPCAPMocapData::IsWorkflowAvailable()
                                ? LOCTEXT("EmptyPerf", "No performers yet — type a name above to create one.")
                                : LOCTEXT("NoWorkflow", "Performance Capture Workflow plugin not available.");
                        })
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredPerformers.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
                ]
            ]
        ]

        // ── Detail popup (scrim + centered card) ───────────────────────────
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedPerformer.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(372.f).MaxDesiredHeight(468.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadPerformers();
}

// ── Data ────────────────────────────────────────────────────────────────────

void SPCAPActorDatabasePanel::ReloadPerformers()
{
    AllPerformers.Reset();
    TileThumbnails.Empty();

    for (const FPCAPPerformerInfo& Info : UPCAPMocapData::GetAllPerformers())
    {
        AllPerformers.Add(MakeShared<FPCAPPerformerInfo>(Info));
    }

    AllPerformers.Sort([](const FPerformerPtr& A, const FPerformerPtr& B)
    { return A.IsValid() && B.IsValid() && A->PerformerName.LexicalLess(B->PerformerName); });

    ApplyFilter();
}

void SPCAPActorDatabasePanel::ApplyFilter()
{
    FilteredPerformers.Reset();
    for (const FPerformerPtr& Ptr : AllPerformers)
    {
        if (!Ptr.IsValid()) { continue; }
        if (FilterText.IsEmpty()
            || Ptr->PerformerName.ToString().Contains(FilterText)
            || Ptr->LiveLinkSubject.ToString().Contains(FilterText))
        {
            FilteredPerformers.Add(Ptr);
        }
    }
    if (TileView.IsValid()) { TileView->RequestListRefresh(); }
}

UObject* SPCAPActorDatabasePanel::ResolvePreview(const FPCAPPerformerInfo& Info, UPCAPPerformerExtension* Ext)
{
    if (Ext && !Ext->Headshot.IsNull())
    {
        if (UObject* H = Ext->Headshot.LoadSynchronous()) { return H; }
    }
    if (!Info.BaseSkeletalMesh.IsNull())
    {
        if (UObject* M = Info.BaseSkeletalMesh.LoadSynchronous()) { return M; }
    }
    return Info.Asset.LoadSynchronous();
}

// ── Gallery tiles ─────────────────────────────────────────────────────────

TSharedRef<ITableRow> SPCAPActorDatabasePanel::OnGenerateTile(FPerformerPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    const FString NameText = Item.IsValid() ? Item->PerformerName.ToString() : TEXT("(missing)");
    const FString SubText  = Item.IsValid() ? Item->LiveLinkSubject.ToString() : FString();

    UPCAPPerformerExtension* Ext = Item.IsValid() ? UPCAPMocapData::FindPerformerExtension(Item->AssetUID) : nullptr;

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (Item.IsValid())
    {
        if (UObject* Asset = ResolvePreview(*Item, Ext))
        {
            TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item->AssetUID);
            if (!Thumb.IsValid())
            {
                Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
                TileThumbnails.Add(Item->AssetUID, Thumb);
            }
            ThumbWidget = Thumb->MakeThumbnailWidget();
        }
    }

    return SNew(STableRow<FPerformerPtr>, Owner)
        .Padding(4.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).HeightOverride(88.f)[ ThumbWidget ] ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f, 0.f, 0.f)
                [ SNew(STextBlock).Text(FText::FromString(NameText)) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(SubText)).ColorAndOpacity(FSlateColor(ColText2)) ]
            ]
        ];
}

void SPCAPActorDatabasePanel::OnSelectionChanged(FPerformerPtr Item, ESelectInfo::Type)
{
    SelectedPerformer = Item;
    if (Item.IsValid() && DetailBox.IsValid())
    {
        DetailBox->SetContent(BuildDetailFor(Item));
    }
}

void SPCAPActorDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPActorDatabasePanel::OnNewPerformerCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) { return; }
    const FString Name = Text.ToString().TrimStartAndEnd();
    if (Name.IsEmpty()) { return; }

    if (UPCAPMocapData::CreatePerformerAsset(PerformerPackageDir(), FName(*Name), NAME_None))
    {
        ReloadPerformers();
    }
}

void SPCAPActorDatabasePanel::CloseDetail()
{
    SelectedPerformer.Reset();
    if (TileView.IsValid()) { TileView->ClearSelection(); }
}

// ── Detail card ───────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPActorDatabasePanel::BuildDetailFor(FPerformerPtr Info)
{
    if (!Info.IsValid()) { return SNew(SBox); }

    UObject* PerformerAsset = Info->Asset.LoadSynchronous();
    UPCAPPerformerExtension* Ext = UPCAPMocapData::FindPerformerExtension(Info->AssetUID);
    TWeakObjectPtr<UPCAPPerformerExtension> WeakExt(Ext);

    auto SaveExt = [WeakExt]()
    {
        if (WeakExt.IsValid())
        {
            if (UPackage* Pkg = WeakExt->GetPackage())
            {
                Pkg->MarkPackageDirty();
                FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
            }
        }
    };

    auto AssetSlot = [](const FString& Label, TFunction<FString()> Get, TFunction<void(const FAssetData&)> Set) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SObjectPropertyEntryBox).AllowedClass(UObject::StaticClass()).DisplayThumbnail(false)
              .ObjectPath_Lambda(MoveTemp(Get)).OnObjectChanged_Lambda(MoveTemp(Set)) ];
    };

    // Header preview thumbnail.
    TSharedRef<SWidget> Head =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg2", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = ResolvePreview(*Info, Ext))
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 72, 72, ThumbnailPool);
        Head = DetailThumbnail->MakeThumbnailWidget();
    }

    // Body that depends on whether the extension exists yet.
    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

    if (!Ext)
    {
        Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("NoExt", "Core fields are owned by the performer asset (edit via Open asset / Mocap Manager). Add a PCAPTool extension to attach face/HMC, audio and digital-double data."))
          .ColorAndOpacity(FSlateColor(ColText2)) ];
        Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("AddExt", "Add PCAPTool extension"))
          .OnClicked_Lambda([this, PerformerAsset]()
          {
              if (UPCAPMocapData::EnsurePerformerExtension(PerformerAsset))
              {
                  if (SelectedPerformer.IsValid() && DetailBox.IsValid())
                  {
                      DetailBox->SetContent(BuildDetailFor(SelectedPerformer));
                  }
              }
              return FReply::Handled();
          }) ];
    }
    else
    {
        // Face Live Link subject (FFaceStreamEntry on the extension).
        Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("FaceSubj", "Face Live Link subject")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SEditableTextBox)
          .Text(FText::FromName(Ext->FaceStream.LiveLinkSubjectName))
          .OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type){ if (WeakExt.IsValid()) WeakExt->FaceStream.LiveLinkSubjectName = FName(*T.ToString()); }) ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("HMC rig"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->HMCRig.ToString() : FString(); },
            [WeakExt](const FAssetData& AD) { if (WeakExt.IsValid()) WeakExt->HMCRig = TSoftObjectPtr<UHMCRigEntry>(AD.GetSoftObjectPath()); }) ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("Face scan"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->FaceScan.ToString() : FString(); },
            [WeakExt](const FAssetData& AD) { if (WeakExt.IsValid()) WeakExt->FaceScan = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
        [ SNew(SCheckBox)
          .IsChecked_Lambda([WeakExt]() { return (WeakExt.IsValid() && WeakExt->bUseFaceScanOnMetaHuman) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
          .OnCheckStateChanged_Lambda([WeakExt](ECheckBoxState S) { if (WeakExt.IsValid()) WeakExt->bUseFaceScanOnMetaHuman = (S == ECheckBoxState::Checked); })
          [ SNew(STextBlock).Text(LOCTEXT("UseFaceScan", "Use face scan on the MetaHuman")) ] ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("Headshot (overrides preview)"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->Headshot.ToString() : FString(); },
            [this, WeakExt](const FAssetData& AD) { if (WeakExt.IsValid()) { WeakExt->Headshot = TSoftObjectPtr<UTexture2D>(AD.GetSoftObjectPath()); if (SelectedPerformer.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedPerformer)); } }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Ext->Notes)).OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type){ if (WeakExt.IsValid()) WeakExt->Notes = T.ToString(); }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("SaveExt", "Save extension")).OnClicked_Lambda([SaveExt]() { SaveExt(); return FReply::Handled(); }) ];
    }

    // "Called today" — drives the existing call-sheet via the performer name.
    const FString PerfId = Info->PerformerName.ToString();
    Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
    [ SNew(SCheckBox)
      .IsChecked_Lambda([PerfId]()
      {
          UPCAPToolSettings* S = UPCAPToolSettings::Get();
          UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
          return (DB && DB->IsActorCalled(PerfId)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
      })
      .OnCheckStateChanged_Lambda([PerfId](ECheckBoxState S)
      {
          UPCAPToolSettings* Set = UPCAPToolSettings::Get();
          if (UMocapDatabase* DB = (Set ? Set->GetDatabase() : nullptr))
              DB->SetActorCalled(PerfId, S == ECheckBoxState::Checked);
      })
      [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ];

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            // Header: name + Live Link subject + open/close.
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(72.f).HeightOverride(72.f)[ Head ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromName(Info->PerformerName)).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::Format(LOCTEXT("SubjFmt", "Live Link: {0}"), FText::FromName(Info->LiveLinkSubject))).ColorAndOpacity(FSlateColor(ColText2)) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            // Open-the-Epic-asset button (core fields edited there / in Mocap Manager).
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton).Text(LOCTEXT("Open", "Open performer asset"))
              .OnClicked_Lambda([PerformerAsset]() { if (PerformerAsset && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(PerformerAsset); return FReply::Handled(); }) ]

            + SVerticalBox::Slot().AutoHeight()
            [ Body ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
