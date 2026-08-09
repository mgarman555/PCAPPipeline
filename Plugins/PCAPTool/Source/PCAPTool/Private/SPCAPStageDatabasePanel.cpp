#include "SPCAPStageDatabasePanel.h"
#include "StageConfigAsset.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"
#include "PCAPStageBridge.h"    // UPCAPStageBridge — pair / apply / compare against the Mocap Manager stage
#include "MocapDatabase.h"      // FProduction + FShootDay hold the soft refs a delete would quietly break
#include "PCAPToolSettings.h"   // the master DB, resolved the way every panel resolves it

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
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMesh.h"    // the Volume Visualizer only draws a UStaticMesh — the card says so
#include "Misc/MessageDialog.h"   // delete is destructive and unpicks the day's stage; it asks first
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

    // The Operator Console's amber, for the two states nobody may miss: a pairing whose Epic stage
    // has gone, and a reference mesh the Volume Visualizer will not draw. Prefixed because
    // SPCAPPanelStyle.h documents why a bare colour constant in an anonymous namespace collides
    // under UE's unity builds.
    const FLinearColor PCAPStageColAmber = FLinearColor(0.878f, 0.627f, 0.188f);

    // Editor toast — the plugin's standard operator feedback (SPCAPPropDatabasePanel::PCAPPropNotify).
    // A delete that's refused, a pairing that won't take, a preset that won't load: all of it has to
    // say so on screen; nobody reads the output log on the floor.
    void PCAPStageNotify(const FText& Message)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = 4.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }

    // The master DB, resolved the way every panel resolves it.
    UMocapDatabase* PCAPStageDB()
    {
        UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
        return Settings ? Settings->GetDatabase() : nullptr;
    }

    // Card edits have to survive the editor closing: Modify() snapshots into the transaction buffer
    // (stage configs are created RF_Transactional, so Ctrl-Z works) and MarkPackageDirty() makes UE
    // prompt to save. Without it an edit made here is gone on restart with no prompt at all, while
    // the same field edited from the Call Sheet's details view persists. "Save" below the card is a
    // shortcut, not the only path a typed value has to disk. Same shape as the Prop library's
    // PCAPPropEditExtension, and the same Modify()-then-dirty protocol PCAPStageBridge uses.
    void PCAPStageEdit(const TWeakObjectPtr<UStageConfigAsset>& Weak, const FText& TransactionText,
                       TFunction<void(UStageConfigAsset&)> Edit)
    {
        UStageConfigAsset* Entry = Weak.Get();
        if (!Entry) { return; }

        const FScopedTransaction Transaction(TransactionText);
        Entry->Modify();
        Edit(*Entry);
        Entry->MarkPackageDirty();
    }

    // Everything in the master database that soft-points at this stage config: the stage library,
    // each production's ActiveStageConfig, and each shoot day's override. ObjectTools' referencer
    // check sees none of them — a TSoftObjectPtr is not a referencer — so deleting the stage the
    // active day is set to would simply resolve that day's stage to null and the Call Sheet header
    // would revert to "(pick stage)" with nothing said. Placed APCAPVolumeVisualizer actors hold a
    // soft ref too; those live in levels this panel cannot enumerate, so they are not listed.
    TArray<FString> PCAPStageSoftReferencers(const UStageConfigAsset* Entry)
    {
        TArray<FString> Users;

        UMocapDatabase* DB = PCAPStageDB();
        if (!Entry || !DB) return Users;

        const FSoftObjectPath Target(Entry);

        for (const TSoftObjectPtr<UStageConfigAsset>& Listed : DB->StageConfigs)
            if (Listed.ToSoftObjectPath() == Target)
            {
                Users.Add(TEXT("the master database's stage library"));
                break;
            }

        for (const FProduction& Prod : DB->Productions)
        {
            if (Prod.ActiveStageConfig.ToSoftObjectPath() == Target)
                Users.Add(FString::Printf(TEXT("%s — the production's stage"), *Prod.ProjectCode));

            for (const FShootDay& Day : Prod.Days)
                if (Day.ActiveStageConfig.ToSoftObjectPath() == Target)
                    Users.Add(FString::Printf(TEXT("%s / %s"), *Prod.ProjectCode, *Day.DayID));
        }

        return Users;
    }

    // The card's mesh picker takes any UObject, because the field's contract is "any mesh" and any
    // UObject thumbnails. APCAPVolumeVisualizer::RefreshFromStageConfig only casts to UStaticMesh
    // and draws nothing for anything else, with no log line — so the card that made the choice is
    // where that has to be said. Reads the already-loaded object rather than loading: this runs per
    // frame from a Visibility_Lambda, and BuildDetailFor loads the mesh for the thumbnail anyway.
    bool PCAPStageMeshIsUndrawable(const TWeakObjectPtr<UStageConfigAsset>& Weak)
    {
        const UStageConfigAsset* Entry = Weak.Get();
        if (!Entry || Entry->StageReferenceMesh.IsNull()) return false;

        const UObject* Mesh = Entry->StageReferenceMesh.Get();
        return Mesh && !Mesh->IsA<UStaticMesh>();
    }

    // Applies the Live Link preset this stage names — until now the field was a note to self that
    // read like a control. Resolved by /Script path and UFUNCTION name rather than by including
    // LiveLinkPreset.h: the same reflection discipline PCAPStageBridge uses on the Workflow plugin,
    // and for the same reason — a lookup that misses reports why and no-ops instead of taking the
    // build with it. OutFailure is operator-facing and always set when this returns false.
    bool PCAPStageApplyLiveLinkPreset(const FString& PresetPath, FString& OutFailure)
    {
        const FString Path = PresetPath.TrimStartAndEnd();
        if (Path.IsEmpty())
        {
            OutFailure = TEXT("no Live Link preset is set on this stage");
            return false;
        }

        UClass* PresetClass = FindObject<UClass>(nullptr, TEXT("/Script/LiveLink.LiveLinkPreset"));
        if (!PresetClass)
        {
            OutFailure = TEXT("ULiveLinkPreset did not resolve — the Live Link plugin is not loaded in this editor");
            return false;
        }

        UObject* Preset = StaticLoadObject(PresetClass, nullptr, *Path);
        if (!Preset)
        {
            OutFailure = FString::Printf(
                TEXT("nothing loaded from '%s' — this wants a ULiveLinkPreset asset path (/Game/…/MyPreset.MyPreset), not a file on disk"), *Path);
            return false;
        }

        // ApplyToClient() is a BlueprintCallable returning bool, so exactly one parm — the return
        // value — and no inputs. Anything else means the signature moved and we do not guess at it.
        UFunction* Apply = Preset->FindFunction(FName(TEXT("ApplyToClient")));
        if (!Apply || Apply->NumParms != 1 || !Apply->GetReturnProperty())
        {
            OutFailure = FString::Printf(
                TEXT("'%s' has no zero-argument ApplyToClient() — confirm the Live Link API on Windows"), *Preset->GetName());
            return false;
        }

        struct FApplyToClientParams { bool bReturnValue = false; } Params;
        Preset->ProcessEvent(Apply, &Params);
        if (!Params.bReturnValue)
        {
            OutFailure = FString::Printf(TEXT("Live Link refused the preset '%s'"), *Preset->GetName());
            return false;
        }
        return true;
    }

    // The Mocap Manager block on the stage card: which Epic session template this config is paired
    // to, whether that template will accept a write, and what the two disagree about. Without it
    // the whole UPCAPStageBridge surface has no call site — PCapStageUID is only settable by
    // hand-typing a GUID into the raw DataAsset, and the divergence report written to be read
    // "before shoot day" is unreachable. Follows the shape SPCAPPropDatabasePanel uses for
    // UPCAPMocapData::IsWorkflowAvailable(): the section says why it is inert rather than vanishing.
    //
    // A free function and not a member because the panel's header owns its member list; everything
    // this needs is the config, a way to redraw the card, and somewhere to keep the report between
    // clicks. Refresh() is the same rebuild MakeEnumRow does.
    TSharedRef<SWidget> PCAPStageMocapManagerBody(const TWeakObjectPtr<UStageConfigAsset>& Weak,
                                                  TFunction<void()> Refresh,
                                                  TSharedRef<FString> Report)
    {
        if (!UPCAPStageBridge::IsStageModelAvailable())
            return SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("NoStageModel", "Performance Capture Workflow plugin not available — stage pairing is inert."))
                .ColorAndOpacity(FSlateColor(ColLabel));

        UStageConfigAsset* Entry = Weak.Get();
        if (!Entry) return SNew(SBox);

        // ResolvePairedStage, not FindStageByUID: the cached soft ref is the fast path and only a
        // stale one costs the full session-template scan. Null with a valid UID means the paired
        // template is gone from the project, which is a different thing from "not paired".
        UObject* PairedStage = UPCAPStageBridge::ResolvePairedStage(Entry);
        const bool bMissing   = !PairedStage && Entry->PCapStageUID.IsValid();
        const FString PairedLabel = PairedStage
            ? PairedStage->GetName()
            : (bMissing ? FString::Printf(TEXT("(stage %s not in this project)"),
                                          *Entry->PCapStageUID.ToString(EGuidFormats::DigitsWithHyphens))
                        : FString(TEXT("(not paired)")));

        // The divergence report is written to be read before shoot day, so a paired card shows it
        // without a click. Unpaired cards would only ever report "not paired", which the row above
        // already says, so they stay quiet.
        if (PairedStage && Report->IsEmpty())
        {
            const TArray<FString> Lines = UPCAPStageBridge::DescribeStageDivergence(Entry);
            *Report = Lines.Num() > 0
                ? FString::Join(Lines, TEXT("\n"))
                : FString(TEXT("Nothing diverges — this config and its Mocap Manager stage agree."));
        }

        // Why an Apply would be refused, said up front. A locked template is one a session has
        // already been created from, and whatever it says now is what that session will run with.
        FString LockReason;
        const bool bEditable = PairedStage && UPCAPStageBridge::IsPairedStageEditable(Entry, LockReason);

        return SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("PairedStage", "Paired stage")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                + SHorizontalBox::Slot().FillWidth(0.6f)
                [
                    SNew(SComboButton)
                    .ToolTipText(FText::FromString(Entry->PCapStageUID.IsValid()
                        ? FString::Printf(TEXT("Paired by AssetUID %s — the pairing survives the Epic asset being renamed, moved or re-pathed."),
                                          *Entry->PCapStageUID.ToString(EGuidFormats::DigitsWithHyphens))
                        : FString(TEXT("Pick the Mocap Manager session template this stage describes. The pairing is by AssetUID, so renaming that asset does not break it."))))
                    .ButtonContent()
                    [ SNew(STextBlock).Text(FText::FromString(PairedLabel))
                      .ColorAndOpacity(FSlateColor(bMissing ? PCAPStageColAmber : ColLabel)) ]
                    .OnGetMenuContent_Lambda([Weak, Refresh]() -> TSharedRef<SWidget>
                    {
                        FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);

                        const TArray<FPCAPStageInfo> Stages = UPCAPStageBridge::GetAllStages();
                        if (Stages.Num() == 0)
                            MB.AddMenuEntry(LOCTEXT("NoStages", "(no Mocap Manager stages in this project)"), FText::GetEmpty(), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([](){}),
                                          FCanExecuteAction::CreateLambda([](){ return false; })));

                        for (const FPCAPStageInfo& Info : Stages)
                        {
                            const FName StageName = Info.StageName;
                            const TSoftObjectPtr<UObject> StageAsset = Info.Asset;
                            MB.AddMenuEntry(FText::FromName(StageName), FText::GetEmpty(), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([Weak, Refresh, StageName, StageAsset]()
                                {
                                    // PairStageConfig does its own Modify() + save on both packages,
                                    // and logs every way it can refuse.
                                    PCAPStageNotify(UPCAPStageBridge::PairStageConfig(Weak.Get(), StageAsset.LoadSynchronous())
                                        ? FText::Format(LOCTEXT("Paired", "Paired to the Mocap Manager stage \"{0}\"."), FText::FromName(StageName))
                                        : FText::Format(LOCTEXT("PairFailed", "Could not pair to \"{0}\" — see the output log."), FText::FromName(StageName)));
                                    Refresh();
                                })));
                        }

                        MB.AddMenuEntry(LOCTEXT("Unpair", "(not paired)"), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([Weak, Refresh]()
                            {
                                UPCAPStageBridge::UnpairStageConfig(Weak.Get());
                                Refresh();
                            })));

                        return MB.MakeWidget();
                    })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(FText::FromString(LockReason.IsEmpty() ? FString() : FString::Printf(TEXT("Apply will refuse — %s."), *LockReason)))
              .ColorAndOpacity(FSlateColor(PCAPStageColAmber))
              .Visibility(LockReason.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("ApplyStage", "Apply to Mocap Manager"))
                  .IsEnabled(bEditable)
                  .ToolTipText(LOCTEXT("ApplyStageTip", "Pushes what PCAPTool owns onto the paired session template — today that is the timecode source, as its recording clock source. Refuses a locked template."))
                  .OnClicked_Lambda([Weak, Refresh]()
                  {
                      PCAPStageNotify(UPCAPStageBridge::ApplyStageConfigToSession(Weak.Get()) > 0
                          ? LOCTEXT("Applied", "Applied — the Mocap Manager stage's recording clock source now matches this stage's timecode source.")
                          : LOCTEXT("ApplyRefused", "Nothing applied — see the output log for why (locked, unresolved, or no equivalent)."));
                      Refresh();   // clears the stale report and re-reads the lock
                      return FReply::Handled();
                  }) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("CompareStage", "Compare"))
                  .IsEnabled(PairedStage != nullptr)
                  .ToolTipText(LOCTEXT("CompareStageTip", "Re-reads the paired session template and lists what the two disagree about. Empty means they agree — it never means \"could not check\"."))
                  .OnClicked_Lambda([Weak, Report]()
                  {
                      const TArray<FString> Lines = UPCAPStageBridge::DescribeStageDivergence(Weak.Get());
                      *Report = Lines.Num() > 0
                          ? FString::Join(Lines, TEXT("\n"))
                          : FString(TEXT("Nothing diverges — this config and its Mocap Manager stage agree."));
                      return FReply::Handled();
                  }) ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text_Lambda([Report]() { return FText::FromString(*Report); })
              .ColorAndOpacity(FSlateColor(ColLabel))
              .Visibility_Lambda([Report]() { return Report->IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ];
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
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<TWeakObjectPtr<UStageConfigAsset>>)
                        .ListItemsSource(&FilteredStages)
                        .OnGenerateTile(this, &SPCAPStageDatabasePanel::OnGenerateTile)
                        .OnSelectionChanged(this, &SPCAPStageDatabasePanel::OnSelectionChanged)
                        .ItemWidth(150.f)
                        .ItemHeight(160.f)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("EmptyStages", "No stages yet — type a name in the box above to create one."))
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredStages.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
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

    // Delete used to be one unguarded click. Name the shoot days it silently unpicks first —
    // those are soft refs, so neither this dialog nor ObjectTools' own referencer check would
    // otherwise mention them, and the only symptom on the floor is the Call Sheet header
    // reverting to "(pick stage)" for no visible reason.
    const TArray<FString> Referencers = PCAPStageSoftReferencers(Entry);

    const FString Label = Entry->ConfigName.IsEmpty() ? Entry->GetName() : Entry->ConfigName;

    FString Prompt = FString::Printf(TEXT("Delete the stage config \"%s\"?"), *Label);
    if (Referencers.Num() > 0)
        Prompt += FString::Printf(TEXT("\n\nIt is still the stage for:\n  %s\n\nThose fall back to \"(pick stage)\" with nothing said at the time."),
            *FString::Join(Referencers, TEXT("\n  ")));
    Prompt += TEXT("\n\nThis cannot be undone.");

    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Prompt)) != EAppReturnType::Yes)
        return false;

    // Reference check on: ObjectTools puts up its own referencer dialog for the hard references
    // it can see (an open asset editor, anything holding the object) instead of deleting out
    // from under them. It returns false if the operator backs out there too.
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ true);
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

    // Redraw the card in place — the same rebuild MakeEnumRow does after a pick.
    TFunction<void()> Refresh = [this]()
    {
        if (SelectedStage.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedStage.Get()));
    };

    // The Mocap Manager divergence report, kept alive by the widgets that write and read it. One
    // per card build, so a rebuild after an Apply drops a report that is no longer true.
    const TSharedRef<FString> DivergenceReport = MakeShared<FString>();

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
                    [ SNew(SEditableTextBox).Text(FText::FromString(Entry->ConfigName)).OnTextCommitted_Lambda([this, Weak](const FText& T, ETextCommit::Type)
                      {
                          // Text boxes commit on focus loss too — only spend a transaction (and dirty
                          // the package) when the value actually changed.
                          const FString NewName = T.ToString();
                          if (!Weak.IsValid() || Weak->ConfigName == NewName) return;
                          PCAPStageEdit(Weak, LOCTEXT("NameTx", "Rename Stage Config"), [&NewName](UStageConfigAsset& S){ S.ConfigName = NewName; });
                          if (TileView.IsValid()) TileView->RequestListRefresh();
                      }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("RefMesh", "Stage reference mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SObjectPropertyEntryBox).AllowedClass(UObject::StaticClass()).DisplayThumbnail(false)
                      .ObjectPath_Lambda([Weak]() { return Weak.IsValid() ? Weak->StageReferenceMesh.ToString() : FString(); })
                      .OnObjectChanged_Lambda([Weak, Refresh](const FAssetData& AD)
                      {
                          const TSoftObjectPtr<UObject> NewMesh(AD.GetSoftObjectPath());
                          if (!Weak.IsValid() || Weak->StageReferenceMesh == NewMesh) return;
                          PCAPStageEdit(Weak, LOCTEXT("MeshTx", "Set Stage Reference Mesh"), [&NewMesh](UStageConfigAsset& S){ S.StageReferenceMesh = NewMesh; });
                          Refresh();
                      }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
                    [ SNew(STextBlock).AutoWrapText(true)
                      .Text(LOCTEXT("MeshNotStatic", "Not a Static Mesh — it thumbnails here, but the Volume Visualizer draws no stage geometry for it."))
                      .ColorAndOpacity(FSlateColor(PCAPStageColAmber))
                      .Visibility_Lambda([Weak]() { return PCAPStageMeshIsUndrawable(Weak) ? EVisibility::Visible : EVisibility::Collapsed; }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 6.f)
            [ SNew(STextBlock).Text(LOCTEXT("Recording", "Looking to record")).ColorAndOpacity(FSlateColor(ColGreen)) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
            [ MakeEnumRow(TEXT("Body"),  StaticEnum<EBodySystem>(),  (int32)Entry->BodySystem,  [Weak](int32 V){ if (Weak.IsValid() && Weak->BodySystem != (EBodySystem)V)
                PCAPStageEdit(Weak, LOCTEXT("BodyTx", "Set Stage Body System"), [V](UStageConfigAsset& S){ S.BodySystem = (EBodySystem)V; }); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
            [ MakeEnumRow(TEXT("Face"),  StaticEnum<EFaceSystem>(),  (int32)Entry->FaceSystem,  [Weak](int32 V){ if (Weak.IsValid() && Weak->FaceSystem != (EFaceSystem)V)
                PCAPStageEdit(Weak, LOCTEXT("FaceTx", "Set Stage Face System"), [V](UStageConfigAsset& S){ S.FaceSystem = (EFaceSystem)V; }); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
            [ MakeEnumRow(TEXT("Audio"), StaticEnum<EAudioSystem>(), (int32)Entry->AudioSystem, [Weak](int32 V){ if (Weak.IsValid() && Weak->AudioSystem != (EAudioSystem)V)
                PCAPStageEdit(Weak, LOCTEXT("AudioTx", "Set Stage Audio System"), [V](UStageConfigAsset& S){ S.AudioSystem = (EAudioSystem)V; }); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
            [ MakeEnumRow(TEXT("VCam"),  StaticEnum<EVCamSystem>(),  (int32)Entry->VCamSystem,  [Weak](int32 V){ if (Weak.IsValid() && Weak->VCamSystem != (EVCamSystem)V)
                PCAPStageEdit(Weak, LOCTEXT("VCamTx", "Set Stage VCam System"), [V](UStageConfigAsset& S){ S.VCamSystem = (EVCamSystem)V; }); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
            [ MakeEnumRow(TEXT("Timecode"), StaticEnum<ETimecodeSource>(), (int32)Entry->TimecodeSource, [Weak](int32 V){ if (Weak.IsValid() && Weak->TimecodeSource != (ETimecodeSource)V)
                PCAPStageEdit(Weak, LOCTEXT("TimecodeTx", "Set Stage Timecode Source"), [V](UStageConfigAsset& S){ S.TimecodeSource = (ETimecodeSource)V; }); }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(LOCTEXT("Preset", "Live Link preset (ULiveLinkPreset asset path)")).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(SEditableTextBox)
                  .HintText(LOCTEXT("PresetHint", "/Game/…/MyPreset.MyPreset"))
                  .Text(FText::FromString(Entry->LiveLinkPresetPath))
                  .OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type)
                  {
                      const FString NewPath = T.ToString();
                      if (!Weak.IsValid() || Weak->LiveLinkPresetPath == NewPath) return;
                      PCAPStageEdit(Weak, LOCTEXT("PresetTx", "Set Stage Live Link Preset"), [&NewPath](UStageConfigAsset& S){ S.LiveLinkPresetPath = NewPath; });
                  }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("ApplyPreset", "Apply now"))
                  .ToolTipText(LOCTEXT("ApplyPresetTip", "Brings this stage's Live Link sources up now. Nothing applies it automatically — picking the stage in the Call Sheet does not."))
                  .OnClicked_Lambda([Weak]()
                  {
                      const FString Path = Weak.IsValid() ? Weak->LiveLinkPresetPath : FString();
                      FString Failure;
                      if (PCAPStageApplyLiveLinkPreset(Path, Failure))
                      {
                          PCAPStageNotify(LOCTEXT("PresetApplied", "Live Link preset applied — its sources and subjects are up."));
                      }
                      else
                      {
                          PCAPStageNotify(FText::Format(LOCTEXT("PresetFailed", "Live Link preset not applied — {0}."), FText::FromString(Failure)));
                          UE_LOG(LogTemp, Warning, TEXT("[PCAP] Stage Database: Live Link preset '%s' not applied — %s."), *Path, *Failure);
                      }
                      return FReply::Handled();
                  }) ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Entry->Notes)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type)
              {
                  const FString NewNotes = T.ToString();
                  if (!Weak.IsValid() || Weak->Notes == NewNotes) return;
                  PCAPStageEdit(Weak, LOCTEXT("NotesTx", "Edit Stage Notes"), [&NewNotes](UStageConfigAsset& S){ S.Notes = NewNotes; });
              }) ]

            // Mocap Manager — which Epic session template this stage is, and what the two disagree
            // about before shoot day. UPCAPStageBridge's only call site.
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)
            [ SNew(STextBlock).Text(LOCTEXT("MocapMgrHdr", "Mocap Manager")).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ PCAPStageMocapManagerBody(Weak, Refresh, DivergenceReport) ]

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
