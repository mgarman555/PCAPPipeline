#include "SPCAPActorDatabasePanel.h"
#include "PCAPMocapData.h"
#include "PCAPPerformerExtension.h"
#include "HMCRigEntry.h"      // UHMCRigEntry — extension HMC rig slot
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"    // PCAPPaths::ActorsDir — never hardcode /Game/…

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
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPActorDatabase"

#include "SPCAPPanelStyle.h"

// Helpers are prefixed per-panel: under UE's unity (combined-translation-unit) builds
// every anonymous namespace in the blob is the same namespace, so a bare name shared
// with a sibling panel is a redefinition (see the note in SPCAPPanelStyle.h).
namespace
{
    // Editor toast — the plugin's standard operator feedback. A create that goes nowhere
    // has to say so on screen; the operator is not reading the output log on the floor.
    void PCAPActorDBNotify(const FText& Message)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = 4.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }

    // Apply one committed edit to the extension: transacted (so it is undoable), Modify()'d,
    // and dirtied. Without the dirty the scrim — which closes the card on any click outside it,
    // the natural dismiss gesture — throws the edit away: nothing is dirty, so the editor never
    // prompts at exit. "Save extension" is what writes it to disk.
    void PCAPActorDBEditExtension(const TWeakObjectPtr<UPCAPPerformerExtension>& WeakExt, const FText& TransactionText,
                                  TFunction<void(UPCAPPerformerExtension&)> Edit)
    {
        UPCAPPerformerExtension* Ext = WeakExt.Get();
        if (!Ext) { return; }

        const FScopedTransaction Transaction(TransactionText);
        Ext->Modify();
        Edit(*Ext);
        Ext->MarkPackageDirty();
    }

    // Performer UID → its PCAPTool extension. UPCAPMocapData::FindPerformerExtension runs a
    // full asset-registry query and synchronously loads every extension in the project on
    // *each* call, and the gallery needs one per tile — with the search box regenerating
    // every tile on every keystroke that is O(N²) loads per character. Resolve the whole set
    // in one pass instead. Entries are weak, so a deleted extension reads back as none.
    TMap<FGuid, TWeakObjectPtr<UPCAPPerformerExtension>>& PCAPActorDBExtCache()
    {
        static TMap<FGuid, TWeakObjectPtr<UPCAPPerformerExtension>> Cache;
        return Cache;
    }

    void PCAPActorDBCacheExt(UPCAPPerformerExtension* Ext)
    {
        if (Ext && Ext->PCapPerformerUID.IsValid())
        {
            PCAPActorDBExtCache().Add(Ext->PCapPerformerUID, Ext);
        }
    }

    // One registry pass over every extension — called from ReloadPerformers, the panel's
    // single refresh entry point.
    void PCAPActorDBRebuildExtCache()
    {
        PCAPActorDBExtCache().Reset();

        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> Found;
        ARM.Get().GetAssetsByClass(UPCAPPerformerExtension::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
        for (const FAssetData& AD : Found)
        {
            PCAPActorDBCacheExt(Cast<UPCAPPerformerExtension>(AD.GetAsset()));
        }
    }

    UPCAPPerformerExtension* PCAPActorDBFindExt(const FGuid& PerformerUID)
    {
        const TWeakObjectPtr<UPCAPPerformerExtension>* Found = PCAPActorDBExtCache().Find(PerformerUID);
        return (Found && Found->IsValid()) ? Found->Get() : nullptr;
    }

    // Remove a performer from the library: the Epic asset *and* its paired extension — an
    // orphaned "_Ext" would keep a dangling UID pairing forever (a re-created performer of
    // the same name gets a fresh AssetUID). Same ObjectTools path as the Stage/HMC libraries.
    bool PCAPActorDBDeletePerformer(UObject* PerformerAsset, UPCAPPerformerExtension* Ext)
    {
        if (!PerformerAsset) { return false; }
        if (!ObjectTools::DeleteSingleObject(PerformerAsset, /*bPerformReferenceCheck*/ false)) { return false; }
        if (Ext) { ObjectTools::DeleteSingleObject(Ext, /*bPerformReferenceCheck*/ false); }
        return true;
    }
}

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
                        // The one-time roster importer's only call site. It has to live on the
                        // toolbar rather than in the empty state: a project that creates one
                        // performer by hand is no longer empty, and its roster is still unmigrated.
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
                        [ SNew(SButton).Text(LOCTEXT("Import", "Import legacy roster"))
                          .ToolTipText(LOCTEXT("ImportTip", "One-time migration: create a performer asset (plus a PCAPTool extension) for every legacy UActorRosterEntry that doesn't have one yet. Safe to run twice — entries that already exist are skipped."))
                          .OnClicked_Lambda([this]()
                          {
                              if (!UPCAPMocapData::IsWorkflowAvailable())
                              {
                                  PCAPActorDBNotify(LOCTEXT("ImportNoWorkflow", "Performance Capture Workflow plugin not available — there is nothing to import into."));
                                  return FReply::Handled();
                              }
                              const int32 Created = UPCAPMocapData::MigrateRosterToPCap(PCAPPaths::ActorsDir());
                              if (Created > 0)
                              {
                                  ReloadPerformers();
                                  PCAPActorDBNotify(FText::Format(LOCTEXT("ImportDone", "Imported {0} performer(s) from the legacy actor roster."), FText::AsNumber(Created)));
                              }
                              else
                              {
                                  PCAPActorDBNotify(LOCTEXT("ImportNone", "Nothing to import — every legacy roster entry already has a performer asset."));
                              }
                              return FReply::Handled();
                          }) ]
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
                                ? LOCTEXT("EmptyPerf", "No performers yet — type a name above to create one, or import the legacy actor roster.")
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
    PCAPActorDBRebuildExtCache();   // one pass, so tile generation never queries the registry

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

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (Item.IsValid())
    {
        // Resolve the extension + preview only on a thumbnail miss: every keystroke in the
        // search box regenerates every tile, and resolving loads assets.
        TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item->AssetUID);
        if (!Thumb.IsValid())
        {
            if (UObject* Asset = ResolvePreview(*Item, PCAPActorDBFindExt(Item->AssetUID)))
            {
                Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
                TileThumbnails.Add(Item->AssetUID, Thumb);
            }
        }
        if (Thumb.IsValid()) { ThumbWidget = Thumb->MakeThumbnailWidget(); }
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

    // The typed text becomes a package name, and UE forbids spaces and punctuation there
    // (INVALID_LONGPACKAGE_CHARACTERS — see CONTRIBUTING), so a human name like
    // "Kevin Dorman" would create an asset that can never be saved. Same guard as the
    // Stage/HMC libraries.
    FString AssetName = Name;
    AssetName.ReplaceInline(TEXT(" "), TEXT("_"));
    AssetName = ObjectTools::SanitizeObjectName(AssetName);
    if (AssetName.IsEmpty()) { return; }

    if (!UPCAPMocapData::IsWorkflowAvailable())
    {
        PCAPActorDBNotify(LOCTEXT("CreateNoWorkflow", "Performance Capture Workflow plugin not available — performers can't be created."));
        return;
    }

    // Creation returns null on a name collision as well as on failure, and the panel is the
    // only thing that can say which: silence here reads as "the tool is broken" on a shoot day.
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::ActorsDir(), *AssetName);
    if (FPackageName::DoesPackageExist(PackageName))
    {
        PCAPActorDBNotify(FText::Format(LOCTEXT("CreateExists", "A performer named \"{0}\" already exists."), FText::FromString(AssetName)));
        return;
    }

    if (UPCAPMocapData::CreatePerformerAsset(PCAPPaths::ActorsDir(), FName(*AssetName), NAME_None))
    {
        ReloadPerformers();
    }
    else
    {
        PCAPActorDBNotify(FText::Format(LOCTEXT("CreateFailed", "Could not create performer \"{0}\" — see the Output Log."), FText::FromString(AssetName)));
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

    UPCAPPerformerExtension* Ext = PCAPActorDBFindExt(Info->AssetUID);
    if (!Ext)
    {
        // Cache miss on the editing surface (an extension made in the Content Browser since
        // the last reload): pay for the authoritative lookup once, here, not per tile.
        Ext = UPCAPMocapData::FindPerformerExtension(Info->AssetUID);
        PCAPActorDBCacheExt(Ext);
    }
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
              if (UPCAPPerformerExtension* New = UPCAPMocapData::EnsurePerformerExtension(PerformerAsset))
              {
                  PCAPActorDBCacheExt(New);   // the card rebuilds straight away — don't re-query for it
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
        // Every committed edit below goes through PCAPActorDBEditExtension — the card is
        // dismissed by clicking the scrim, so an edit that only writes the UObject is lost.

        // Face Live Link subject (FFaceStreamEntry on the extension).
        Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("FaceSubj", "Face Live Link subject")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SEditableTextBox)
          .Text(FText::FromName(Ext->FaceStream.LiveLinkSubjectName))
          .OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type)
          {
              const FName Subject(*T.ToString());
              PCAPActorDBEditExtension(WeakExt, LOCTEXT("FaceSubjTx", "Set Performer Face Live Link Subject"),
                  [Subject](UPCAPPerformerExtension& E) { E.FaceStream.LiveLinkSubjectName = Subject; });
          }) ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("HMC rig"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->HMCRig.ToString() : FString(); },
            [WeakExt](const FAssetData& AD)
            {
                PCAPActorDBEditExtension(WeakExt, LOCTEXT("HMCRigTx", "Set Performer HMC Rig"),
                    [&AD](UPCAPPerformerExtension& E) { E.HMCRig = TSoftObjectPtr<UHMCRigEntry>(AD.GetSoftObjectPath()); });
            }) ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("Face scan"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->FaceScan.ToString() : FString(); },
            [WeakExt](const FAssetData& AD)
            {
                PCAPActorDBEditExtension(WeakExt, LOCTEXT("FaceScanTx", "Set Performer Face Scan"),
                    [&AD](UPCAPPerformerExtension& E) { E.FaceScan = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); });
            }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
        [ SNew(SCheckBox)
          .IsChecked_Lambda([WeakExt]() { return (WeakExt.IsValid() && WeakExt->bUseFaceScanOnMetaHuman) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
          .OnCheckStateChanged_Lambda([WeakExt](ECheckBoxState S)
          {
              const bool bUse = (S == ECheckBoxState::Checked);
              PCAPActorDBEditExtension(WeakExt, LOCTEXT("UseFaceScanTx", "Toggle Face Scan on the MetaHuman"),
                  [bUse](UPCAPPerformerExtension& E) { E.bUseFaceScanOnMetaHuman = bUse; });
          })
          [ SNew(STextBlock).Text(LOCTEXT("UseFaceScan", "Use face scan on the MetaHuman")) ] ];

        Body->AddSlot().AutoHeight()
        [ AssetSlot(TEXT("Headshot (overrides preview)"),
            [WeakExt]() { return WeakExt.IsValid() ? WeakExt->Headshot.ToString() : FString(); },
            [this, WeakExt](const FAssetData& AD)
            {
                PCAPActorDBEditExtension(WeakExt, LOCTEXT("HeadshotTx", "Set Performer Headshot"),
                    [&AD](UPCAPPerformerExtension& E) { E.Headshot = TSoftObjectPtr<UTexture2D>(AD.GetSoftObjectPath()); });
                if (SelectedPerformer.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedPerformer));
            }) ];

        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Body->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Ext->Notes))
          .OnTextCommitted_Lambda([WeakExt](const FText& T, ETextCommit::Type)
          {
              const FString NewNotes = T.ToString();
              PCAPActorDBEditExtension(WeakExt, LOCTEXT("NotesTx", "Edit Performer Notes"),
                  [&NewNotes](UPCAPPerformerExtension& E) { E.Notes = NewNotes; });
          }) ];

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
          UMocapDatabase* DB = Set ? Set->GetDatabase() : nullptr;
          if (!DB) return;
          DB->SetActorCalled(PerfId, S == ECheckBoxState::Checked);
          // SetActorCalled only edits the in-memory FShootDay. The call list lives in the master
          // DB asset and there is no Save on this card, so dirty *and* write it — otherwise a
          // whole day of call-outs survives only until the editor closes. Same guard as the Call
          // Sheet and the Take Browser.
          DB->MarkPackageDirty();
          UPackage* Pkg = DB->GetPackage();
          if (Pkg && Pkg->IsDirty())
          {
              FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
          }
      })
      [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ];

    // Delete — the library toolbar's remove half (docs/tools/databases.md). Without it a
    // mistyped performer can only be cleaned up from the Content Browser. Matches the
    // Stage/HMC libraries: no extra confirmation, the Epic asset and its extension go together.
    Body->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f)
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SButton).Text(LOCTEXT("Delete", "Delete"))
          .ToolTipText(LOCTEXT("DeleteTip", "Delete this performer asset and its PCAPTool extension."))
          .OnClicked_Lambda([this, PerformerAsset, WeakExt]()
          {
              if (PCAPActorDBDeletePerformer(PerformerAsset, WeakExt.Get()))
              {
                  CloseDetail();
                  ReloadPerformers();
              }
              return FReply::Handled();
          }) ]
    ];

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
