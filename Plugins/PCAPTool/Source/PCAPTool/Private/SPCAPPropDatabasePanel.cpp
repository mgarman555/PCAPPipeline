#include "SPCAPPropDatabasePanel.h"
#include "PCAPMocapData.h"
#include "PCAPPropExtension.h"
#include "PCAPToolTypes.h"      // EStreamStatus
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"      // PCAPPaths::PropsDir — never hardcode "/Game/..."

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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "FileHelpers.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPPropDatabase"

#include "SPCAPPanelStyle.h"

namespace
{
    // Editor toast — the plugin's standard operator feedback (SPCAPOperatorConsole::
    // NotifyOperator). A prop that doesn't get created, or a call-out that can't be
    // written, has to say so on screen; nobody reads the output log on the floor.
    void PCAPPropNotify(const FText& Message)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }

    // The master DB, resolved the way every panel resolves it. Created on construct,
    // so by the time a card is built this is a plain read.
    UMocapDatabase* PCAPPropDB()
    {
        UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
        return Settings ? Settings->GetDatabase() : nullptr;
    }

    // "Called to today's shoot" writes into the active FShootDay — with no production
    // and day picked there is nowhere to write and UMocapDatabase::SetPropCalled
    // silently does nothing. Same test the Call Sheet gates its whole sheet on.
    bool PCAPPropHasActiveDay(UMocapDatabase* DB)
    {
        return DB && !DB->ActiveProductionCode.IsEmpty()
            && DB->GetDay(DB->ActiveProductionCode, DB->ActiveDayID) != nullptr;
    }

    // Extension edits have to survive the editor closing: Modify() snapshots into the
    // transaction buffer (the extension is created RF_Transactional, so Ctrl-Z works)
    // and MarkPackageDirty() makes UE prompt to save it. "Save extension" below the
    // card is then a shortcut, not the only path a typed note has to disk.
    void PCAPPropEditExtension(const TWeakObjectPtr<UPCAPPropExtension>& WeakExt, const FText& TransactionText,
                               TFunction<void(UPCAPPropExtension&)> Edit)
    {
        UPCAPPropExtension* Ext = WeakExt.Get();
        if (!Ext) { return; }

        const FScopedTransaction Transaction(TransactionText);
        Ext->Modify();
        Edit(*Ext);
        Ext->MarkPackageDirty();
    }

    // Label + enum dropdown, matching the Stage / HMC library rows (MakeEnumRow there).
    TSharedRef<SWidget> PCAPPropEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set)
    {
        const FText CurrentText = EnumPtr ? EnumPtr->GetDisplayNameTextByValue((int64)Current) : FText::GetEmpty();
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SComboButton)
                .ButtonContent()[ SNew(STextBlock).Text(CurrentText) ]
                .OnGetMenuContent_Lambda([EnumPtr, Set]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);
                    if (EnumPtr)
                        for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)   // skip the implicit _MAX
                        {
                            const int32 Val = (int32)EnumPtr->GetValueByIndex(i);
                            MB.AddMenuEntry(EnumPtr->GetDisplayNameTextByIndex(i), FText::GetEmpty(), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([Set, Val]() { Set(Val); })));
                        }
                    return MB.MakeWidget();
                })
            ];
    }
}

void SPCAPPropDatabasePanel::Construct(const FArguments& InArgs)
{
    // The master DB is auto-created on first tool open — do it here too, or opening the
    // Prop Database first in a fresh project leaves the call-out checkbox pointing at
    // nothing. Same line as the Call Sheet / Production Database.
    if (UPCAPToolSettings* S = UPCAPToolSettings::Get()) { S->GetOrCreateDatabase(); }

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

    // The typed name becomes a package name, so it has to be legal first: this tool's own
    // example prop, "Lightsaber Hilt A", would otherwise build a path with a space in it
    // (INVALID_LONGPACKAGE_CHARACTERS) and CreatePackage would fail. Same two lines the
    // Stage and HMC libraries use.
    FString AssetName = Name;
    AssetName.ReplaceInline(TEXT(" "), TEXT("_"));
    AssetName = ObjectTools::SanitizeObjectName(AssetName);
    if (AssetName.IsEmpty())
    {
        PCAPPropNotify(FText::Format(LOCTEXT("NewBadName", "\"{0}\" has no characters that are legal in an asset name."), Text));
        return;
    }

    // Every way this can fail says so — the operator typed a name and pressed ↵, and the
    // empty-state hint about the missing plugin only shows on an empty library.
    if (!UPCAPMocapData::IsWorkflowAvailable())
    {
        PCAPPropNotify(LOCTEXT("NewNoWorkflow", "Prop not created — the Performance Capture Workflow plugin is not available."));
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Prop Database: '%s' not created — Performance Capture Workflow unavailable."), *AssetName);
        return;
    }

    const FString PropsDir = PCAPPaths::PropsDir();
    if (FPackageName::DoesPackageExist(FString::Printf(TEXT("%s/%s"), *PropsDir, *AssetName)))
    {
        PCAPPropNotify(FText::Format(LOCTEXT("NewExists", "A prop asset named \"{0}\" already exists."), FText::FromString(AssetName)));
        return;
    }

    if (!UPCAPMocapData::CreatePropAsset(PropsDir, FName(*AssetName), NAME_None))
    {
        PCAPPropNotify(FText::Format(LOCTEXT("NewFailed", "Could not create prop \"{0}\" — see the output log."), FText::FromString(AssetName)));
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Prop Database: CreatePropAsset failed for '%s' at %s."), *AssetName, *PropsDir);
        return;
    }

    ReloadProps();
    PCAPPropNotify(FText::Format(LOCTEXT("NewCreated", "Created prop \"{0}\"."), FText::FromString(AssetName)));
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
        // History — the production codes this prop has shot on, the roster's ["DA","TLOU"]
        // shape. One comma-separated box so it reads at a glance and edits in place.
        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("History", "Production history")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SEditableTextBox)
          .HintText(LOCTEXT("HistoryHint", "production codes, comma-separated"))
          .Text(FText::FromString(FString::Join(Ext->ProductionHistory, TEXT(", "))))
          .OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type)
          {
              TArray<FString> Codes;
              T.ToString().ParseIntoArray(Codes, TEXT(","), /*InCullEmpty=*/ true);
              for (FString& Code : Codes) { Code = Code.TrimStartAndEnd(); }
              Codes.RemoveAll([](const FString& Code) { return Code.IsEmpty(); });

              // Text boxes commit on focus loss too — only spend a transaction (and dirty
              // the package) when the value actually changed.
              if (!WeakExt.IsValid() || WeakExt->ProductionHistory == Codes) { return; }
              PCAPPropEditExtension(WeakExt, LOCTEXT("HistoryTx", "Edit Prop Production History"),
                  [&Codes](UPCAPPropExtension& E) { E.ProductionHistory = Codes; });
          }) ];

        // Status — the prop tracker's last known state, recorded by the operator. Nothing
        // streams it in yet; this is the card's own record of what it was doing last.
        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
        [ PCAPPropEnumRow(TEXT("Stream status"), StaticEnum<EStreamStatus>(), (int32)Ext->StreamStatus,
            [this, WeakExt](int32 Value)
            {
                if (!WeakExt.IsValid() || WeakExt->StreamStatus == (EStreamStatus)Value) { return; }
                PCAPPropEditExtension(WeakExt, LOCTEXT("StatusTx", "Set Prop Stream Status"),
                    [Value](UPCAPPropExtension& E) { E.StreamStatus = (EStreamStatus)Value; });
                if (SelectedProp.IsValid() && DetailBox.IsValid())
                {
                    DetailBox->SetContent(BuildDetailFor(SelectedProp));   // redraw the button's label
                }
            }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Ext->Notes)).OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type)
          {
              const FString NewNotes = T.ToString();
              if (!WeakExt.IsValid() || WeakExt->Notes == NewNotes) { return; }
              PCAPPropEditExtension(WeakExt, LOCTEXT("NotesTx", "Edit Prop Notes"),
                  [&NewNotes](UPCAPPropExtension& E) { E.Notes = NewNotes; });
          }) ];
        Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("SaveExt", "Save extension")).OnClicked_Lambda([SaveExt]() { SaveExt(); return FReply::Handled(); }) ];
    }

    // "Called to today's shoot" — writes the active day's CalledPropIDs, the list the Call
    // Sheet drives. Off a day there is nowhere to write, so it goes disabled with the
    // reason rather than snapping back on the next frame.
    const FString PropId = Info->PropName.ToString();
    Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
    [ SNew(SCheckBox)
      .IsEnabled_Lambda([]() { return PCAPPropHasActiveDay(PCAPPropDB()); })
      .ToolTipText_Lambda([]() -> FText
      {
          UMocapDatabase* DB = PCAPPropDB();
          if (!DB) { return LOCTEXT("CallNoDBTip", "No master database — set one in Project Settings ▸ PCAP Tool, or reopen the tool."); }
          return PCAPPropHasActiveDay(DB)
              ? FText::Format(LOCTEXT("CallTipFmt", "Adds this prop to {0} / {1}'s called props."),
                              FText::FromString(DB->ActiveProductionCode), FText::FromString(DB->ActiveDayID))
              : LOCTEXT("CallNoDayTip", "No shoot day is active — pick a production and shoot day in the Call Sheet first.");
      })
      .IsChecked_Lambda([PropId]()
      {
          UMocapDatabase* DB = PCAPPropDB();
          return (DB && DB->IsPropCalled(PropId)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
      })
      .OnCheckStateChanged_Lambda([PropId](ECheckBoxState S)
      {
          // Re-checked here as well as on IsEnabled — the active day can change under an
          // open card, and the click must never silently no-op.
          UMocapDatabase* DB = PCAPPropDB();
          if (!PCAPPropHasActiveDay(DB))
          {
              PCAPPropNotify(LOCTEXT("CallNoDayToast", "Nothing called — pick a production and shoot day in the Call Sheet first."));
              return;
          }

          const FScopedTransaction Transaction(LOCTEXT("CallTx", "Call Prop To Shoot Day"));
          DB->Modify();
          DB->SetPropCalled(PropId, S == ECheckBoxState::Checked);
          DB->MarkPackageDirty();   // the day lives in the master DB asset — dirty it or the call-out is gone on restart
      })
      [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ];

    // Why it's greyed out, on the card and not only in the tooltip — the Call Sheet says
    // the same thing in prose when there is no day to work on.
    Body->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
    [ SNew(STextBlock).AutoWrapText(true)
      .Text(LOCTEXT("CallNoDay", "No shoot day is active — pick a production and shoot day in the Call Sheet to call props out."))
      .ColorAndOpacity(FSlateColor(ColText2))
      .Visibility_Lambda([]() { return PCAPPropHasActiveDay(PCAPPropDB()) ? EVisibility::Collapsed : EVisibility::Visible; }) ];

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
