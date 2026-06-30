#include "SPCAPPropDatabasePanel.h"
#include "PCAPMocapData.h"
#include "PCAPPropExtension.h"
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

#define LOCTEXT_NAMESPACE "PCAPPropDatabase"

#include "SPCAPPanelStyle.h"

void SPCAPPropDatabasePanel::Construct(const FArguments& InArgs)
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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "PROP DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search props…")).OnTextChanged(this, &SPCAPPropDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(170.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new prop  ↵")).OnTextCommitted(this, &SPCAPPropDatabasePanel::OnNewPropCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<FPropPtr>)
                        .ListItemsSource(&FilteredProps)
                        .OnGenerateTile(this, &SPCAPPropDatabasePanel::OnGenerateTile)
                        .OnSelectionChanged(this, &SPCAPPropDatabasePanel::OnSelectionChanged)
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
                                ? LOCTEXT("EmptyProps", "No props yet — type a name above to create one.")
                                : LOCTEXT("NoWorkflow", "Performance Capture Workflow plugin not available.");
                        })
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredProps.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedProp.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(372.f).MaxDesiredHeight(448.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadProps();
}

void SPCAPPropDatabasePanel::ReloadProps()
{
    AllProps.Reset();
    TileThumbnails.Empty();

    for (const FPCAPPropInfo& Info : UPCAPMocapData::GetAllProps())
    {
        AllProps.Add(MakeShared<FPCAPPropInfo>(Info));
    }

    AllProps.Sort([](const FPropPtr& A, const FPropPtr& B)
    { return A.IsValid() && B.IsValid() && A->PropName.LexicalLess(B->PropName); });

    ApplyFilter();
}

void SPCAPPropDatabasePanel::ApplyFilter()
{
    FilteredProps.Reset();
    for (const FPropPtr& Ptr : AllProps)
    {
        if (!Ptr.IsValid()) { continue; }
        if (FilterText.IsEmpty()
            || Ptr->PropName.ToString().Contains(FilterText)
            || Ptr->LiveLinkSubject.ToString().Contains(FilterText))
        {
            FilteredProps.Add(Ptr);
        }
    }
    if (TileView.IsValid()) { TileView->RequestListRefresh(); }
}

UObject* SPCAPPropDatabasePanel::ResolvePreview(const FPCAPPropInfo& Info)
{
    if (!Info.PropStaticMesh.IsNull())
    {
        if (UObject* M = Info.PropStaticMesh.LoadSynchronous()) { return M; }
    }
    if (!Info.PropSkeletalMesh.IsNull())
    {
        if (UObject* M = Info.PropSkeletalMesh.LoadSynchronous()) { return M; }
    }
    return Info.Asset.LoadSynchronous();
}

TSharedRef<ITableRow> SPCAPPropDatabasePanel::OnGenerateTile(FPropPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    const FString NameText = Item.IsValid() ? Item->PropName.ToString() : TEXT("(missing)");
    const FString SubText  = Item.IsValid() ? Item->LiveLinkSubject.ToString() : FString();

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh", "no mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (Item.IsValid())
    {
        if (UObject* Asset = ResolvePreview(*Item))
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

    return SNew(STableRow<FPropPtr>, Owner)
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

void SPCAPPropDatabasePanel::OnSelectionChanged(FPropPtr Item, ESelectInfo::Type)
{
    SelectedProp = Item;
    if (Item.IsValid() && DetailBox.IsValid())
    {
        DetailBox->SetContent(BuildDetailFor(Item));
    }
}

void SPCAPPropDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPPropDatabasePanel::OnNewPropCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) { return; }
    const FString Name = Text.ToString().TrimStartAndEnd();
    if (Name.IsEmpty()) { return; }

    if (UPCAPMocapData::CreatePropAsset(PropPackageDir(), FName(*Name), NAME_None))
    {
        ReloadProps();
    }
}

void SPCAPPropDatabasePanel::CloseDetail()
{
    SelectedProp.Reset();
    if (TileView.IsValid()) { TileView->ClearSelection(); }
}

TSharedRef<SWidget> SPCAPPropDatabasePanel::BuildDetailFor(FPropPtr Info)
{
    if (!Info.IsValid()) { return SNew(SBox); }

    UObject* PropAsset = Info->Asset.LoadSynchronous();
    UPCAPPropExtension* Ext = UPCAPMocapData::FindPropExtension(Info->AssetUID);
    TWeakObjectPtr<UPCAPPropExtension> WeakExt(Ext);

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

    TSharedRef<SWidget> Thumb =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh2", "no mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = ResolvePreview(*Info))
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
        Thumb = DetailThumbnail->MakeThumbnailWidget();
    }

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

    if (!Ext)
    {
        Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("NoExt", "Core fields are owned by the prop asset (edit via Open asset / Mocap Manager). Add a PCAPTool extension for history / status / notes."))
          .ColorAndOpacity(FSlateColor(ColText2)) ];
        Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("AddExt", "Add PCAPTool extension"))
          .OnClicked_Lambda([this, PropAsset]()
          {
              if (UPCAPMocapData::EnsurePropExtension(PropAsset))
              {
                  if (SelectedProp.IsValid() && DetailBox.IsValid())
                  {
                      DetailBox->SetContent(BuildDetailFor(SelectedProp));
                  }
              }
              return FReply::Handled();
          }) ];
    }
    else
    {
        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Ext->Notes)).OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type){ if (WeakExt.IsValid()) WeakExt->Notes = T.ToString(); }) ];
        Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("SaveExt", "Save extension")).OnClicked_Lambda([SaveExt]() { SaveExt(); return FReply::Handled(); }) ];
    }

    const FString PropId = Info->PropName.ToString();
    Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
    [ SNew(SCheckBox)
      .IsChecked_Lambda([PropId]()
      {
          UPCAPToolSettings* S = UPCAPToolSettings::Get();
          UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
          return (DB && DB->IsPropCalled(PropId)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
      })
      .OnCheckStateChanged_Lambda([PropId](ECheckBoxState S)
      {
          UPCAPToolSettings* Set = UPCAPToolSettings::Get();
          if (UMocapDatabase* DB = (Set ? Set->GetDatabase() : nullptr))
              DB->SetPropCalled(PropId, S == ECheckBoxState::Checked);
      })
      [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ];

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(96.f).HeightOverride(96.f)[ Thumb ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromName(Info->PropName)).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::Format(LOCTEXT("SubjFmt", "Live Link: {0}"), FText::FromName(Info->LiveLinkSubject))).ColorAndOpacity(FSlateColor(ColText2)) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton).Text(LOCTEXT("Open", "Open prop asset"))
              .OnClicked_Lambda([PropAsset]() { if (PropAsset && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(PropAsset); return FReply::Handled(); }) ]

            + SVerticalBox::Slot().AutoHeight()
            [ Body ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
