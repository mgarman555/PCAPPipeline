#include "SPCAPTakeBrowser.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPTakeRecorderSubsystem.h"     // record state — a finished take must show up here
#include "PCAPTakeRecordWriter.h"          // UPCAPTakeRecordWriter::WriteTake — republish a relabelled take to Epic
#include "PCAPTakeProcessingQueue.h"       // UPCAPTakeProcessingQueue — the queue this panel drives
#include "ActorRosterEntry.h"              // UActorRosterEntry — ActorID → performer name
#include "PropRosterEntry.h"               // UPropRosterEntry — PropID → display name

#include "Engine/Engine.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"   // FAssetRegistryModule — enumerate the rosters
#include "Modules/ModuleManager.h"               // FModuleManager
#include "FileHelpers.h"                         // FEditorFileUtils — DB edits only dirty the package
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PCAPTakeBrowser"

#include "SPCAPPanelStyle.h"

// UPCAPTakeProcessingQueue owns the whole processing state machine: which steps apply
// to a take, the legal transitions between them, and the wording that says where each
// step actually happens. This panel reads and drives it; it decides none of that itself.
//
// The one thing the queue keeps private is a per-step accessor — it exposes step
// metadata and transitions, but FTakeProcessingState carries five named members and no
// step array, so rendering a step's own status/timestamps/error needs this mapping.
static const FProcessingStep* PCAPTakeBrowserFindStep(const FTakeProcessingState& State, EPCAPProcessingStep Step)
{
    switch (Step)
    {
        case EPCAPProcessingStep::BodySolveCleanup: return &State.BodySolveCleanup;
        case EPCAPProcessingStep::HMCSolve:         return &State.HMCSolve;
        case EPCAPProcessingStep::BodyRetarget:     return &State.BodyRetarget;
        case EPCAPProcessingStep::AudioSyncTrim:    return &State.AudioSyncTrim;
        case EPCAPProcessingStep::MergeToSequencer: return &State.MergeToSequencer;
    }
    return nullptr;
}

// Said in several places, so it is written once. Every action here needs the master
// database, and none of them may fail quietly when it is unset.
static FText PCAPTakeBrowserNoDatabaseText()
{
    return LOCTEXT("NoDatabase", "No PCAP database — set one in Project Settings > PCAP > PCAP Tool.");
}

static FText PCAPTakeBrowserNoQueueText()
{
    return LOCTEXT("NoQueue", "The take processing queue is unavailable — the PCAP Tool engine subsystem did not start.");
}

// The queue addresses takes by the same five-part coordinate this panel's rows carry.
static FPCAPTakeKey PCAPTakeBrowserHandleFor(const FPCAPTakeBrowserRow& Row)
{
    FPCAPTakeKey Handle;
    Handle.ProjectCode = Row.ProjectCode;
    Handle.DayID       = Row.DayID;
    Handle.SessionID   = Row.SessionID;
    Handle.ShotID      = Row.ShotID;
    Handle.TakeID      = Row.TakeID;
    return Handle;
}

// Resolve a handle the queue handed back. Via GetShot(), never UMocapDatabase::GetTake(),
// whose signature omits SessionID and returns the first shot-slot match in the day.
static const FTake* PCAPTakeBrowserFindTake(UMocapDatabase& DB, const FPCAPTakeKey& Handle)
{
    FShot* Shot = DB.GetShot(Handle.ProjectCode, Handle.DayID, Handle.SessionID, Handle.ShotID);
    if (!Shot) { return nullptr; }
    for (const FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == Handle.TakeID) { return &Take; }
    }
    return nullptr;
}

void SPCAPTakeBrowser::Construct(const FArguments& InArgs)
{
    // Seed the day filter from the DB's active selection — the operator almost always
    // opens this straight off a shoot day. Everything below it starts unfiltered.
    if (UMocapDatabase* DB = GetDB())
    {
        SelProject = DB->ActiveProductionCode;
        SelDay     = DB->ActiveDayID;
    }

    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            // Header — title + the four locked filter axes + search
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SAssignNew(HeaderBox, SBox) ]
            ]

            // Queue bar — "Process All Queued" and, beside it, exactly what it will do
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SAssignNew(QueueBarBox, SBox) ]
            ]

            // Body — take list spine + take detail
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(0.4f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4.f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                        [ BuildColumnStrip() ]
                        + SVerticalBox::Slot().FillHeight(1.f)
                        [
                            SNew(SOverlay)
                            + SOverlay::Slot()
                            [
                                SAssignNew(TakeListView, SListView<FTakeRowPtr>)
                                .ListItemsSource(&FilteredTakes)
                                .OnGenerateRow(this, &SPCAPTakeBrowser::OnGenerateTakeRow)
                                .OnSelectionChanged(this, &SPCAPTakeBrowser::OnTakeSelected)
                                .SelectionMode(ESelectionMode::Single)
                            ]
                            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this]()
                                {
                                    return AllTakes.Num() == 0
                                        ? LOCTEXT("EmptyNoTakes", "No takes recorded yet.")
                                        : LOCTEXT("EmptyFiltered", "No take matches these filters.");
                                })
                                .ColorAndOpacity(FSlateColor(ColText2))
                                .Visibility_Lambda([this]() { return FilteredTakes.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                            ]
                        ]
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(0.6f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(DetailBox, SBox) ]
                ]
            ]
        ]
    ];

    // Guarded poll, not an unconditional one: rebuilding a day's take list four times a
    // second would thrash. The guards are the recorder's state (a take just finished
    // recording) and the queue's change serial (queue or attestation state was written,
    // here or anywhere else) — UMocapDatabase itself has no change delegate.
    RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SPCAPTakeBrowser::PollRecordState));

    RebuildHeader();
    RefreshAll();
}

UMocapDatabase* SPCAPTakeBrowser::GetDB() const
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    return Settings ? Settings->GetDatabase() : nullptr;
}

UPCAPTakeRecorderSubsystem* SPCAPTakeBrowser::GetRecorder() const
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>() : nullptr;
}

UPCAPTakeProcessingQueue* SPCAPTakeBrowser::GetQueue() const
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPTakeProcessingQueue>() : nullptr;
}

EActiveTimerReturnType SPCAPTakeBrowser::PollRecordState(double, float)
{
    bool bChanged = false;

    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        const uint8 RecordState = (uint8)Rec->GetRecordState();
        if (RecordState != LastRecordState) { LastRecordState = RecordState; bChanged = true; }
    }
    if (UPCAPTakeProcessingQueue* Queue = GetQueue())
    {
        if (Queue->GetChangeSerial() != LastQueueSerial) { bChanged = true; }
    }

    if (bChanged)
    {
        // The day/session/shot menus are built from the takes that exist, so the header
        // has to follow a take landing too.
        RebuildHeader();
        RefreshAll();
    }
    return EActiveTimerReturnType::Continue;
}

void SPCAPTakeBrowser::RefreshAll()
{
    // Resync the guard first: everything this panel writes goes through the queue, which
    // bumps the serial, and without this the next poll would rebuild all of it again.
    if (UPCAPTakeProcessingQueue* Queue = GetQueue()) { LastQueueSerial = Queue->GetChangeSerial(); }

    ReloadTakes();
    RebuildQueueBar();
    RebuildDetail();
}

// ── Data ────────────────────────────────────────────────────────────────────

void SPCAPTakeBrowser::ReloadTakes()
{
    AllTakes.Reset();
    ActorNameByID.Reset();
    PropNameByID.Reset();

    UMocapDatabase* DB = GetDB();
    if (!DB)
    {
        ApplyFilter();
        return;
    }

    // The plugin's only full-take iterator (ForEachTakeInDay) is file-local to
    // MocapDatabase.cpp, and neither it nor GetTakesByLabel() carries the production
    // code, so the walk lives here — it is the only place the full coordinate exists.
    for (const FProduction& Prod : DB->Productions)
    {
        for (const FShootDay& Day : Prod.Days)
        {
            for (const FSession& Session : Day.Sessions)
            {
                for (const FShot& Shot : Session.Shots)
                {
                    for (const FTake& Take : Shot.Takes)
                    {
                        FPCAPTakeBrowserRow Row;
                        Row.ProjectCode     = Prod.ProjectCode;
                        Row.DayID           = Day.DayID;
                        Row.SessionID       = Session.SessionID;
                        Row.ShotID          = Shot.ShotID;
                        Row.TakeID          = Take.TakeID;
                        Row.SessionLabel    = Session.Label;
                        Row.ShotDescription = Shot.Description;
                        Row.Label           = Take.Label;
                        Row.Status          = Take.ProcessingState.OverallStatus;
                        Row.ShotType        = Shot.ShotType;
                        Row.RecordedAt      = Take.RecordedAt;
                        Row.DurationSeconds = Take.DurationSeconds;
                        Row.NumSubjects     = Take.SubjectManifest.Num();
                        Row.NumProps        = Take.PropManifest.Num();
                        AllTakes.Add(MakeShared<FPCAPTakeBrowserRow>(Row));
                    }
                }
            }
        }
    }

    AllTakes.Sort([](const FTakeRowPtr& A, const FTakeRowPtr& B)
    {
        if (!A.IsValid() || !B.IsValid()) { return false; }
        if (A->ProjectCode != B->ProjectCode) { return A->ProjectCode < B->ProjectCode; }
        if (A->DayID       != B->DayID)       { return A->DayID       < B->DayID; }
        if (A->SessionID   != B->SessionID)   { return A->SessionID   < B->SessionID; }
        if (A->ShotID      != B->ShotID)      { return A->ShotID      < B->ShotID; }
        return A->TakeID < B->TakeID;
    });

    // Roster display names, resolved once here. The manifest stores IDs only, and the
    // human names live on the roster DataAssets — the same Asset Registry enumeration
    // the Call Sheet uses to build its actor/prop pickers.
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    {
        TArray<FAssetData> Found;
        ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), Found, false);
        for (const FAssetData& AD : Found)
        {
            if (UActorRosterEntry* Entry = Cast<UActorRosterEntry>(AD.GetAsset()))
            {
                const FString FullName = (Entry->FirstName + TEXT(" ") + Entry->LastName).TrimStartAndEnd();
                if (!FullName.IsEmpty()) { ActorNameByID.Add(Entry->ActorID, FullName); }
            }
        }
    }
    {
        TArray<FAssetData> Found;
        ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), Found, false);
        for (const FAssetData& AD : Found)
        {
            if (UPropRosterEntry* Entry = Cast<UPropRosterEntry>(AD.GetAsset()))
            {
                if (!Entry->DisplayName.IsEmpty()) { PropNameByID.Add(Entry->PropID, Entry->DisplayName); }
            }
        }
    }

    // Keep the operator on the same take across a reload — the row object is new, the
    // coordinate is not.
    if (SelectedTake.IsValid())
    {
        const FPCAPTakeBrowserRow Previous = *SelectedTake;
        SelectedTake.Reset();
        for (const FTakeRowPtr& Ptr : AllTakes)
        {
            if (Ptr.IsValid()
                && Ptr->ProjectCode == Previous.ProjectCode
                && Ptr->DayID       == Previous.DayID
                && Ptr->SessionID   == Previous.SessionID
                && Ptr->ShotID      == Previous.ShotID
                && Ptr->TakeID      == Previous.TakeID)
            {
                SelectedTake = Ptr;
                break;
            }
        }
    }

    ApplyFilter();
}

void SPCAPTakeBrowser::ApplyFilter()
{
    FilteredTakes.Reset();
    for (const FTakeRowPtr& Ptr : AllTakes)
    {
        if (!Ptr.IsValid()) { continue; }
        if (!SelDay.IsEmpty()     && (Ptr->DayID != SelDay || Ptr->ProjectCode != SelProject)) { continue; }
        if (!SelSession.IsEmpty() && Ptr->SessionID != SelSession) { continue; }
        if (!SelShot.IsEmpty()    && Ptr->ShotID != SelShot) { continue; }
        if (SelLabel != INDEX_NONE && (int32)Ptr->Label != SelLabel) { continue; }
        if (!FilterText.IsEmpty()
            && !Ptr->TakeID.Contains(FilterText)
            && !Ptr->ShotID.Contains(FilterText)
            && !Ptr->ShotDescription.Contains(FilterText)) { continue; }
        FilteredTakes.Add(Ptr);
    }

    if (TakeListView.IsValid())
    {
        TakeListView->RequestListRefresh();
        if (SelectedTake.IsValid() && FilteredTakes.Contains(SelectedTake))
        {
            TakeListView->SetSelection(SelectedTake);
        }
        else if (SelectedTake.IsValid())
        {
            // Filtered out from under the detail pane — drop it rather than leave a
            // card open for a take the operator can no longer see in the list.
            SelectedTake.Reset();
            TakeListView->ClearSelection();
            RebuildDetail();
        }
    }
}

void SPCAPTakeBrowser::SaveDB()
{
    // Take labels and processing state live in the master DB asset, and DB edits only
    // MarkPackageDirty — without this a full day of Best/Alt/Burn decisions and queue
    // state would survive only until the editor closes. Same guard as the Call Sheet.
    UMocapDatabase* DB = GetDB();
    if (!DB) { return; }
    UPackage* Pkg = DB->GetPackage();
    if (Pkg && Pkg->IsDirty())
    {
        FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

FTake* SPCAPTakeBrowser::ResolveTake(const FPCAPTakeBrowserRow& Row) const
{
    UMocapDatabase* DB = GetDB();
    if (!DB) { return nullptr; }

    // Via GetShot(), never GetTake(): GetTake's signature omits SessionID and returns
    // the first shot-slot match across every session in the day, which is the wrong
    // take as soon as a day has two sessions — precisely the case this panel exists for.
    FShot* Shot = DB->GetShot(Row.ProjectCode, Row.DayID, Row.SessionID, Row.ShotID);
    if (!Shot) { return nullptr; }
    for (FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == Row.TakeID) { return &Take; }
    }
    return nullptr;
}

// ── Header filters ──────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPTakeBrowser::BuildDayPicker()
{
    const FString ButtonLabel = SelDay.IsEmpty()
        ? FString(TEXT("(all days)"))
        : FString::Printf(TEXT("%s · Day_%s"), *SelProject, *SelDay);

    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(ButtonLabel)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            MB.AddMenuEntry(LOCTEXT("AllDays", "(all days)"), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]()
                {
                    SelProject.Empty(); SelDay.Empty(); SelSession.Empty(); SelShot.Empty();
                    RebuildHeader(); ApplyFilter(); RebuildDetail();
                })));

            // Day IDs repeat across productions, so an entry is the (production, day)
            // pair — otherwise "Day_001" would silently mix two shoots.
            TArray<TPair<FString, FString>> Days;
            for (const FTakeRowPtr& Ptr : AllTakes)
            {
                if (Ptr.IsValid()) { Days.AddUnique(TPair<FString, FString>(Ptr->ProjectCode, Ptr->DayID)); }
            }
            Days.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
            { return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value; });

            for (const TPair<FString, FString>& Entry : Days)
            {
                const FString Project = Entry.Key;
                const FString Day     = Entry.Value;
                MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s · Day_%s"), *Project, *Day)), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Project, Day]()
                    {
                        SelProject = Project; SelDay = Day; SelSession.Empty(); SelShot.Empty();
                        RebuildHeader(); ApplyFilter(); RebuildDetail();
                    })));
            }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildSessionPicker()
{
    const FString ButtonLabel = SelSession.IsEmpty() ? FString(TEXT("(all sessions)")) : (TEXT("Session_") + SelSession);

    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(ButtonLabel)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            MB.AddMenuEntry(LOCTEXT("AllSessions", "(all sessions)"), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]()
                {
                    SelSession.Empty(); SelShot.Empty();
                    RebuildHeader(); ApplyFilter(); RebuildDetail();
                })));

            // Session IDs are created in two different shapes ("S01" and "001_S01"), so
            // the human-facing FSession::Label is shown alongside the ID it filters on.
            TArray<TPair<FString, FString>> Sessions;
            for (const FTakeRowPtr& Ptr : AllTakes)
            {
                if (!Ptr.IsValid()) { continue; }
                if (!SelDay.IsEmpty() && (Ptr->DayID != SelDay || Ptr->ProjectCode != SelProject)) { continue; }
                // Dedupe on the ID alone — the label is decoration, and the same session
                // ID can carry different labels across days when the day filter is off.
                const FString SessionID = Ptr->SessionID;
                if (!Sessions.ContainsByPredicate([&SessionID](const TPair<FString, FString>& Existing){ return Existing.Key == SessionID; }))
                {
                    Sessions.Add(TPair<FString, FString>(SessionID, Ptr->SessionLabel));
                }
            }
            Sessions.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B){ return A.Key < B.Key; });

            for (const TPair<FString, FString>& Entry : Sessions)
            {
                const FString SessionID = Entry.Key;
                const FString Text = Entry.Value.IsEmpty()
                    ? FString::Printf(TEXT("Session_%s"), *SessionID)
                    : FString::Printf(TEXT("Session_%s — %s"), *SessionID, *Entry.Value);
                MB.AddMenuEntry(FText::FromString(Text), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, SessionID]()
                    {
                        SelSession = SessionID; SelShot.Empty();
                        RebuildHeader(); ApplyFilter(); RebuildDetail();
                    })));
            }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildShotPicker()
{
    const FString ButtonLabel = SelShot.IsEmpty() ? FString(TEXT("(all shots)")) : (TEXT("Shot ") + SelShot);

    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(ButtonLabel)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            MB.AddMenuEntry(LOCTEXT("AllShots", "(all shots)"), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]()
                {
                    SelShot.Empty();
                    RebuildHeader(); ApplyFilter(); RebuildDetail();
                })));

            TArray<TPair<FString, FString>> Shots;
            for (const FTakeRowPtr& Ptr : AllTakes)
            {
                if (!Ptr.IsValid()) { continue; }
                if (!SelDay.IsEmpty()     && (Ptr->DayID != SelDay || Ptr->ProjectCode != SelProject)) { continue; }
                if (!SelSession.IsEmpty() && Ptr->SessionID != SelSession) { continue; }
                const FString ShotID = Ptr->ShotID;
                if (!Shots.ContainsByPredicate([&ShotID](const TPair<FString, FString>& Existing){ return Existing.Key == ShotID; }))
                {
                    Shots.Add(TPair<FString, FString>(ShotID, Ptr->ShotDescription));
                }
            }
            Shots.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B){ return A.Key < B.Key; });

            for (const TPair<FString, FString>& Entry : Shots)
            {
                const FString ShotID = Entry.Key;
                const FString Text = Entry.Value.IsEmpty()
                    ? ShotID
                    : FString::Printf(TEXT("%s — %s"), *ShotID, *Entry.Value);
                MB.AddMenuEntry(FText::FromString(Text), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, ShotID]()
                    {
                        SelShot = ShotID;
                        RebuildHeader(); ApplyFilter(); RebuildDetail();
                    })));
            }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildLabelPicker()
{
    const bool bAll = (SelLabel == INDEX_NONE);
    const FString ButtonLabel = bAll ? FString(TEXT("(all labels)")) : LabelName((ETakeLabel)SelLabel);
    const FLinearColor ButtonColor = bAll ? FLinearColor::White : LabelColor((ETakeLabel)SelLabel);

    return SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(ButtonLabel)).ColorAndOpacity(FSlateColor(ButtonColor)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            MB.AddMenuEntry(LOCTEXT("AllLabels", "(all labels)"), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this]()
                {
                    SelLabel = INDEX_NONE;
                    RebuildHeader(); ApplyFilter(); RebuildDetail();
                })));

            // Straight off the UENUM so the four locked names come from one place.
            if (UEnum* LabelEnum = StaticEnum<ETakeLabel>())
            {
                for (int32 Index = 0; Index < LabelEnum->NumEnums() - 1; ++Index)   // skip the implicit _MAX
                {
                    const int32 Value = (int32)LabelEnum->GetValueByIndex(Index);
                    MB.AddMenuEntry(LabelEnum->GetDisplayNameTextByIndex(Index), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Value]()
                        {
                            SelLabel = Value;
                            RebuildHeader(); ApplyFilter(); RebuildDetail();
                        })));
                }
            }
            return MB.MakeWidget();
        });
}

void SPCAPTakeBrowser::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPTakeBrowser::RebuildHeader()
{
    if (!HeaderBox.IsValid()) { return; }

    HeaderBox->SetContent(
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("Title", "TAKE BROWSER")).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildDayPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildSessionPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildShotPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildLabelPicker() ]
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search take / shot…")).OnTextChanged(this, &SPCAPTakeBrowser::OnFilterChanged) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(STextBlock)
          .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d of %d takes"), FilteredTakes.Num(), AllTakes.Num())); })
          .ColorAndOpacity(FSlateColor(ColText3)) ]);
}

// ── Queue bar ───────────────────────────────────────────────────────────────

bool SPCAPTakeBrowser::CanProcessAllQueued(FText& OutReason, FString& OutSummary) const
{
    OutSummary.Empty();

    UMocapDatabase* DB = GetDB();
    if (!DB)      { OutReason = PCAPTakeBrowserNoDatabaseText(); return false; }
    UPCAPTakeProcessingQueue* Queue = GetQueue();
    if (!Queue)   { OutReason = PCAPTakeBrowserNoQueueText();    return false; }

    // Eligible = the queue's own rule (Best or Alt, actually recorded, not already
    // Complete). Admittable = eligible AND actually admittable right now, which is what a
    // press would add. CanQueueTake draws that line, so this preview and the batch itself
    // can never disagree about the count.
    const TArray<FPCAPTakeKey> Eligible = Queue->GetEligibleTakes();
    const TArray<EPCAPProcessingStep> Steps = Queue->GetPipelineSteps();

    int32 NumAdmittable = 0;
    int32 NumBlocked = 0;
    FString FirstBlockedReason;

    // Indexed by the step's own enum value, not by position in GetPipelineSteps() — a
    // parallel array would silently mislabel the breakdown if that order ever changed.
    TArray<int32> StepTotals;
    StepTotals.AddZeroed((int32)EPCAPProcessingStep::MergeToSequencer + 1);

    for (const FPCAPTakeKey& Handle : Eligible)
    {
        FString BlockedReason;
        if (!Queue->CanQueueTake(Handle, BlockedReason))
        {
            ++NumBlocked;
            if (FirstBlockedReason.IsEmpty()) { FirstBlockedReason = BlockedReason; }
            continue;
        }
        ++NumAdmittable;

        // Which steps this take will carry, from its own manifest — the same pure
        // function the batch uses to write the plan.
        if (const FTake* Take = PCAPTakeBrowserFindTake(*DB, Handle))
        {
            const FPCAPProcessingPlan Plan = Queue->BuildProcessingPlan(*Take);
            if (Plan.bApplyBodySolve)     { ++StepTotals[(int32)EPCAPProcessingStep::BodySolveCleanup]; }
            if (Plan.bApplyHMCSolve)      { ++StepTotals[(int32)EPCAPProcessingStep::HMCSolve]; }
            if (Plan.bApplyBodyRetarget)  { ++StepTotals[(int32)EPCAPProcessingStep::BodyRetarget]; }
            if (Plan.bApplyAudioSyncTrim) { ++StepTotals[(int32)EPCAPProcessingStep::AudioSyncTrim]; }
            ++StepTotals[(int32)EPCAPProcessingStep::MergeToSequencer];   // unconditional once admitted
        }
    }

    if (NumAdmittable <= 0)
    {
        // Name the actual blocker rather than guessing at it — CanQueueTake already
        // phrased it for the operator.
        OutReason = (NumBlocked > 0)
            ? FText::FromString(FString::Printf(
                TEXT("Nothing new to queue — %d eligible take(s), none admittable right now. %s"),
                NumBlocked, *FirstBlockedReason))
            : LOCTEXT("QueueNothingWaiting", "Nothing to queue — no take is labelled Best or Alt. Select a take and label it Best or Alt first; Burn is archived raw and is never processed.");
        return false;
    }

    // Spell the payload out before the click: how many takes, and which steps those takes
    // will carry. A batch button whose effect is unknown until after it runs is the one
    // thing this bar must never be.
    OutSummary = FString::Printf(TEXT("Will queue %d take(s)"), NumAdmittable);

    FString StepBreakdown;
    for (const EPCAPProcessingStep Step : Steps)   // pipeline order, from the queue
    {
        const int32 StepIndex = (int32)Step;
        if (!StepTotals.IsValidIndex(StepIndex) || StepTotals[StepIndex] <= 0) { continue; }
        if (!StepBreakdown.IsEmpty()) { StepBreakdown += TEXT("  ·  "); }
        StepBreakdown += FString::Printf(TEXT("%s ×%d"), *Queue->GetStepDisplayName(Step), StepTotals[StepIndex]);
    }
    if (!StepBreakdown.IsEmpty()) { OutSummary += TEXT("  —  ") + StepBreakdown; }
    if (NumBlocked > 0)
    {
        OutSummary += FString::Printf(TEXT("   (%d already queued or blocked, left alone)"), NumBlocked);
    }
    return true;
}

void SPCAPTakeBrowser::RebuildQueueBar()
{
    if (!QueueBarBox.IsValid()) { return; }

    FText BlockedReason;
    FString Summary;
    const bool bCanQueue = CanProcessAllQueued(BlockedReason, Summary);

    const FString PlanLine = bCanQueue ? Summary : BlockedReason.ToString();

    FString ToolTip = PlanLine;
    if (bCanQueue)
    {
        // The button says "Process". It must be impossible to read that as "solve".
        ToolTip += TEXT("\n\nThis is the end-of-day batch and it covers the whole database, not just the takes the filters above are showing.");
        ToolTip += TEXT("\n\nIt admits takes to the worklist and records which steps each one needs. It runs nothing: body solve cleanup happens in Shogun Post / Motive and the HMC solve in MetaHuman Animator, and no engine automation exists for body retarget, audio sync/trim or the Sequencer merge. Every step is done by the operator and marked here.");
    }

    TSharedRef<SVerticalBox> Bar = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SButton)
                .Text(LOCTEXT("ProcessAllQueued", "Process All Queued"))
                .ToolTipText(FText::FromString(ToolTip))
                .IsEnabled(bCanQueue)
                .OnClicked(this, &SPCAPTakeBrowser::OnProcessAllQueuedClicked)
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(PlanLine))
              .ColorAndOpacity(FSlateColor(bCanQueue ? ColText3 : ColAmber)) ]
        ];

    // Takes that were queued and then relabelled cannot be worked, and dropping them
    // silently is how an operator loses a shot. The queue tracks them; say how many.
    if (UPCAPTakeProcessingQueue* Queue = GetQueue())
    {
        const int32 NumStranded = Queue->GetStrandedQueuedTakes().Num();
        if (NumStranded > 0)
        {
            Bar->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(FText::FromString(FString::Printf(
                  TEXT("%d queued take(s) were relabelled after admission and will not be processed. Filter by label to find them, then re-label or remove them from the queue."),
                  NumStranded)))
              .ColorAndOpacity(FSlateColor(ColAmber)) ];
        }
    }

    QueueBarBox->SetContent(Bar);
}

FReply SPCAPTakeBrowser::OnProcessAllQueuedClicked()
{
    FText BlockedReason;
    FString Summary;
    if (!CanProcessAllQueued(BlockedReason, Summary))
    {
        // The button is disabled in every one of these states; this only fires if the
        // database changed under a stale rebuild. Say so rather than no-op.
        NotifyOperator(BlockedReason);
        RebuildQueueBar();
        return FReply::Handled();
    }

    UPCAPTakeProcessingQueue* Queue = GetQueue();
    if (!Queue) { return FReply::Handled(); }   // re-checked above — keeps the deref honest

    // The queue dirties and saves the database itself, and its own wording is the single
    // source for what a batch result means — so the toast cannot drift from the truth.
    const FPCAPQueueReport Result = Queue->QueueAllTakes();

    UE_LOG(LogTemp, Log, TEXT("[PCAP] Take Browser: %s"), *Queue->DescribeReport(Result));
    NotifyOperator(FText::FromString(Queue->DescribeReport(Result)));

    RefreshAll();
    return FReply::Handled();
}

// ── Take list ───────────────────────────────────────────────────────────────

TSharedRef<SHorizontalBox> SPCAPTakeBrowser::MakeTakeColumns(TSharedRef<SWidget> Lead,
                                                             TSharedRef<SWidget> IdCell,
                                                             TSharedRef<SWidget> LabelCell,
                                                             TSharedRef<SWidget> RecordedCell,
                                                             TSharedRef<SWidget> DurationCell,
                                                             TSharedRef<SWidget> StatusCell) const
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 4.f, 8.f, 4.f)  [ Lead ]
        + SHorizontalBox::Slot().FillWidth(1.5f).VAlign(VAlign_Center)                          [ IdCell ]
        + SHorizontalBox::Slot().FillWidth(0.8f).VAlign(VAlign_Center).Padding(4.f, 0.f)        [ LabelCell ]
        + SHorizontalBox::Slot().FillWidth(1.3f).VAlign(VAlign_Center).Padding(4.f, 0.f)        [ RecordedCell ]
        + SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center).Padding(4.f, 0.f)        [ DurationCell ]
        + SHorizontalBox::Slot().FillWidth(0.9f).VAlign(VAlign_Center).Padding(4.f, 0.f, 8.f, 0.f) [ StatusCell ];
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildColumnStrip()
{
    auto MakeHeading = [this](const FText& Heading) -> TSharedRef<SWidget>
    {
        return SNew(STextBlock).Text(Heading).ColorAndOpacity(FSlateColor(ColLabel));
    };

    return SNew(SBox).Padding(FMargin(0.f, 2.f, 0.f, 0.f))
    [
        MakeTakeColumns(
            // A hidden copy of the row glyph, so the strip and the rows share a lead
            // column of exactly the same width.
            SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Visibility(EVisibility::Hidden),
            MakeHeading(LOCTEXT("ColTake",     "Take")),
            MakeHeading(LOCTEXT("ColLabelHdr", "Label")),
            MakeHeading(LOCTEXT("ColRecorded", "Recorded")),
            // Nothing in the pipeline writes FTake::DurationSeconds today, so this column
            // reads "—" for every take. Said here rather than left looking like a bug.
            SNew(STextBlock).Text(LOCTEXT("ColDuration", "Dur"))
                .ToolTipText(LOCTEXT("ColDurationTip", "Take duration is not captured by the pipeline yet — every take reads \"—\" until the record pass writes it."))
                .ColorAndOpacity(FSlateColor(ColLabel)),
            MakeHeading(LOCTEXT("ColStatus",   "Processing")))
    ];
}

TSharedRef<ITableRow> SPCAPTakeBrowser::OnGenerateTakeRow(FTakeRowPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    if (!Item.IsValid())
    {
        return SNew(STableRow<FTakeRowPtr>, Owner)[ SNew(SBox) ];
    }

    const FPCAPTakeBrowserRow& Row = *Item;
    // Manifest counts belong on the row, not just in the detail card: a take with no
    // performers recorded is one the batch will skip, and that should be visible while
    // scanning the list rather than only after clicking into it.
    const FString Context = FString::Printf(TEXT("Day_%s · %s · shot %s  ·  %d performers  ·  %d props"),
        *Row.DayID, *Row.SessionID, *Row.ShotID, Row.NumSubjects, Row.NumProps);

    return SNew(STableRow<FTakeRowPtr>, Owner)
    [
        MakeTakeColumns(
            // The locked label colour is the row's primary signal — gray / gold / green / red.
            SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(FSlateColor(LabelColor(Row.Label))),

            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(Row.TakeID)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(Context)).ColorAndOpacity(FSlateColor(ColText3)) ],

            SNew(STextBlock).Text(FText::FromString(LabelName(Row.Label))).ColorAndOpacity(FSlateColor(LabelColor(Row.Label))),
            SNew(STextBlock).Text(FText::FromString(FormatRecordedAt(Row.RecordedAt))).ColorAndOpacity(FSlateColor(ColText2)),
            SNew(STextBlock).Text(FText::FromString(FormatDuration(Row.DurationSeconds))).ColorAndOpacity(FSlateColor(ColText3)),
            SNew(STextBlock).Text(FText::FromString(StatusName(Row.Status))).ColorAndOpacity(FSlateColor(StatusColor(Row.Status))))
    ];
}

void SPCAPTakeBrowser::OnTakeSelected(FTakeRowPtr Item, ESelectInfo::Type)
{
    // ApplyFilter() re-asserts the selection on every keystroke in the search box, and
    // the view broadcasts that back. Rebuilding the detail card each time would discard
    // anything half-typed into a step's error box, so a no-op selection stays a no-op.
    if (SelectedTake == Item) { return; }
    SelectedTake = Item;
    RebuildDetail();
}

// ── Detail ──────────────────────────────────────────────────────────────────

void SPCAPTakeBrowser::RebuildDetail()
{
    if (!DetailBox.IsValid()) { return; }

    if (!SelectedTake.IsValid())
    {
        DetailBox->SetContent(SNew(STextBlock)
            .Text(LOCTEXT("PickTake", "Select a take from the list."))
            .ColorAndOpacity(FSlateColor(ColText2)));
        return;
    }

    DetailBox->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[ BuildDetailFor(SelectedTake) ]);
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildDetailFor(FTakeRowPtr Item)
{
    if (!Item.IsValid()) { return SNew(SBox); }

    const FPCAPTakeBrowserRow& Row = *Item;
    const FTake* Take = ResolveTake(Row);
    if (!Take)
    {
        // The take moved or was removed under an open card — say so rather than render
        // a stale copy of a record that no longer exists.
        return SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("TakeGone", "This take is no longer in the database. It may have been removed, or its shot moved to another session."))
            .ColorAndOpacity(FSlateColor(ColAmber));
    }

    const FString Coordinate = Row.SessionLabel.IsEmpty()
        ? FString::Printf(TEXT("%s · Day_%s · Session_%s · Shot %s"), *Row.ProjectCode, *Row.DayID, *Row.SessionID, *Row.ShotID)
        : FString::Printf(TEXT("%s · Day_%s · Session_%s (%s) · Shot %s"), *Row.ProjectCode, *Row.DayID, *Row.SessionID, *Row.SessionLabel, *Row.ShotID);

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(Row.TakeID)).ColorAndOpacity(FSlateColor(ColGreen)) ]

        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Coordinate)).ColorAndOpacity(FSlateColor(ColText2)) ];

    if (!Row.ShotDescription.IsEmpty())
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Row.ShotDescription)).ColorAndOpacity(FSlateColor(ColText3)) ];
    }

    // Calibration / test / retargeting slots are seeded on every shoot day and are not
    // performances. Say which kind of shot this take belongs to when it isn't a
    // production shot, so a queued calibration take is obvious on sight.
    if (Row.ShotType != EShotType::Production)
    {
        UEnum* ShotTypeEnum = StaticEnum<EShotType>();
        const FString ShotTypeText = ShotTypeEnum ? ShotTypeEnum->GetDisplayNameTextByValue((int64)Row.ShotType).ToString() : FString();
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(ShotTypeText + TEXT(" shot — not a performance"))).ColorAndOpacity(FSlateColor(ColAmber)) ];
    }

    // Label — the four locked labels, in the four locked colours.
    Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
    [ SNew(STextBlock).Text(LOCTEXT("LabelHeading", "Label")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    Box->AddSlot().AutoHeight()[ BuildLabelRow(Row, *Take) ];

    // Timing.
    Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
    [ SNew(STextBlock).Text(LOCTEXT("TimingHeading", "Recorded")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    Box->AddSlot().AutoHeight()
    [ SNew(STextBlock).Text(FText::FromString(FormatRecordedAt(Take->RecordedAt))).ColorAndOpacity(FSlateColor(ColText2)) ];
    Box->AddSlot().AutoHeight()
    [ SNew(STextBlock).AutoWrapText(true)
      .Text(FText::FromString(Take->DurationSeconds > 0.f
          ? FString::Printf(TEXT("duration %s"), *FormatDuration(Take->DurationSeconds))
          : FString(TEXT("duration not captured — nothing in the pipeline writes it yet"))))
      .ColorAndOpacity(FSlateColor(ColText3)) ];

    if (Take->RecordedAt == FDateTime(0))
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("NotRecorded", "This take has no record time — it is a seeded slot, not a captured performance."))
          .ColorAndOpacity(FSlateColor(ColAmber)) ];
    }

    // Assets — only MasterSequence is ever written, so only it is claimed.
    Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
    [ SNew(STextBlock).Text(LOCTEXT("SequenceHeading", "Recorded sequence")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    Box->AddSlot().AutoHeight()
    [ SNew(STextBlock).AutoWrapText(true)
      .Text(FText::FromString(Take->MasterSequence.IsNull() ? FString(TEXT("none linked")) : Take->MasterSequence.ToString()))
      .ColorAndOpacity(FSlateColor(Take->MasterSequence.IsNull() ? ColText3 : ColText2)) ];
    if (Take->bHasVCam)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).Text(LOCTEXT("HasVCam", "VCam was recorded on this take.")).ColorAndOpacity(FSlateColor(ColText3)) ];
    }

    Box->AddSlot().AutoHeight()[ BuildManifestSection(*Take) ];
    Box->AddSlot().AutoHeight()[ BuildProcessingSection(Row, *Take) ];

    // Notes, read-only here — the Call Sheet and the record path own note authoring.
    if (!Take->Notes.IsEmpty() || !Take->DirectorNotes.IsEmpty() || !Take->CommentatorNotes.IsEmpty())
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("NotesHeading", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        if (!Take->Notes.IsEmpty())
        {
            Box->AddSlot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Take->Notes)) ];
        }
        if (!Take->DirectorNotes.IsEmpty())
        {
            Box->AddSlot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Director: ") + Take->DirectorNotes)).ColorAndOpacity(FSlateColor(ColText2)) ];
        }
        if (!Take->CommentatorNotes.IsEmpty())
        {
            Box->AddSlot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Commentator: ") + Take->CommentatorNotes)).ColorAndOpacity(FSlateColor(ColText2)) ];
        }
    }

    return Box;
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildLabelRow(const FPCAPTakeBrowserRow& Row, const FTake& Take)
{
    TSharedRef<SVerticalBox> Wrapper = SNew(SVerticalBox);
    TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);

    if (UEnum* LabelEnum = StaticEnum<ETakeLabel>())
    {
        for (int32 Index = 0; Index < LabelEnum->NumEnums() - 1; ++Index)   // skip the implicit _MAX
        {
            const ETakeLabel Value = (ETakeLabel)LabelEnum->GetValueByIndex(Index);
            const bool bCurrent = (Take.Label == Value);
            const FString Text = FString::Printf(TEXT("%s %s"),
                bCurrent ? TEXT("●") : TEXT("○"),
                *LabelEnum->GetDisplayNameTextByIndex(Index).ToString());

            const FPCAPTakeBrowserRow RowCopy = Row;
            Chips->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                .ToolTipText(Value == ETakeLabel::Burn
                    ? LOCTEXT("BurnTip", "Burn — archived raw. A Burn take never enters the processing queue.")
                    : (Value == ETakeLabel::Captured
                        ? LOCTEXT("CapturedTip", "Captured — the default. Captured takes do not enter the processing queue.")
                        : LOCTEXT("QueueableTip", "Best and Alt takes are the ones \"Process All Queued\" picks up.")))
                .OnClicked_Lambda([this, RowCopy, Value]()
                {
                    SetLabelForRow(RowCopy, Value);
                    return FReply::Handled();
                })
                [ SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(FSlateColor(LabelColor(Value))) ]
            ];
        }
    }

    Wrapper->AddSlot().AutoHeight()[ Chips ];

    // A take can be relabelled after it was queued — nothing in the data model stops it.
    // Surface the consequence instead of silently dropping the take from the batch.
    if (Take.Label == ETakeLabel::Burn && Take.ProcessingState.bHasQueued)
    {
        Wrapper->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("BurnAfterQueue", "Labelled Burn after it was queued — this take will not be processed. Its recorded data is untouched."))
          .ColorAndOpacity(FSlateColor(ColAmber)) ];
    }
    else if (Take.Label == ETakeLabel::Captured && Take.ProcessingState.bHasQueued)
    {
        Wrapper->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("CapturedAfterQueue", "Labelled Captured after it was queued — this take will not be processed. Re-label it Best or Alt to put it back in the batch."))
          .ColorAndOpacity(FSlateColor(ColAmber)) ];
    }

    return Wrapper;
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildManifestSection(const FTake& Take)
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    // Performers.
    Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 2.f)
    [ SNew(STextBlock)
      .Text(FText::FromString(FString::Printf(TEXT("Performers · %d"), Take.SubjectManifest.Num())))
      .ColorAndOpacity(FSlateColor(ColLabel)) ];

    // The manifest records which streams were ARMED for the take, copied at record time
    // from the shot's configured Live Link defaults. It does not record whether a stream
    // stayed connected, so this must not be labelled "streams live".
    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
    [ SNew(STextBlock).AutoWrapText(true)
      .Text(LOCTEXT("StreamsCaveat", "Streams armed at record time. The take records what was armed, not whether each stream stayed connected through the take."))
      .ColorAndOpacity(FSlateColor(ColText3)) ];

    if (Take.SubjectManifest.Num() == 0)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).Text(LOCTEXT("NoSubjects", "no performers recorded on this take")).ColorAndOpacity(FSlateColor(ColText3)) ];
    }
    for (const FTakeSubjectSnapshot& Subject : Take.SubjectManifest)
    {
        const FString* Resolved = ActorNameByID.Find(Subject.ActorID);
        FString Who = Subject.ActorID;
        if (Resolved) { Who += FString::Printf(TEXT("  (%s)"), **Resolved); }
        if (!Subject.CharacterName.IsEmpty()) { Who += FString::Printf(TEXT("  as %s"), *Subject.CharacterName); }

        TSharedRef<SHorizontalBox> SubjectRow = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Who)) ];

        // One badge per armed stream, dim when the stream was not armed for this take.
        SubjectRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("BodyBadge", "body"))
          .ColorAndOpacity(FSlateColor(Subject.bHadBodyStream ? ColGreen : ColText3)) ];
        SubjectRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("FaceBadge", "face"))
          .ColorAndOpacity(FSlateColor(Subject.bHadFaceStream ? ColGreen : ColText3)) ];
        SubjectRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock)
          .Text(FText::FromString(Subject.AudioChannels.Num() > 0
              ? FString::Printf(TEXT("audio ×%d"), Subject.AudioChannels.Num())
              : FString(TEXT("audio"))))
          .ToolTipText(Subject.AudioChannels.Num() > 0
              ? FText::FromString(FString::Join(Subject.AudioChannels, TEXT(", ")))
              : LOCTEXT("NoAudioTip", "No audio channel was armed for this performer."))
          .ColorAndOpacity(FSlateColor(Subject.AudioChannels.Num() > 0 ? ColGreen : ColText3)) ];

        Box->AddSlot().AutoHeight().Padding(0.f, 2.f)[ SubjectRow ];
    }

    // Props.
    Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
    [ SNew(STextBlock)
      .Text(FText::FromString(FString::Printf(TEXT("Props · %d"), Take.PropManifest.Num())))
      .ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (Take.PropManifest.Num() == 0)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).Text(LOCTEXT("NoProps", "no props recorded on this take")).ColorAndOpacity(FSlateColor(ColText3)) ];
    }
    for (const FTakePropSnapshot& Prop : Take.PropManifest)
    {
        const FString* Resolved = PropNameByID.Find(Prop.PropID);
        const FString What = Resolved ? FString::Printf(TEXT("%s  (%s)"), *Prop.PropID, **Resolved) : Prop.PropID;

        Box->AddSlot().AutoHeight().Padding(0.f, 2.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(What)) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock)
              .Text(Prop.bWasTracked ? LOCTEXT("PropTracked", "tracked") : LOCTEXT("PropUntracked", "untracked"))
              .ColorAndOpacity(FSlateColor(Prop.bWasTracked ? ColGreen : ColText3)) ]
        ];
    }

    return Box;
}

TSharedRef<SWidget> SPCAPTakeBrowser::BuildProcessingSection(const FPCAPTakeBrowserRow& Row, const FTake& Take)
{
    const FTakeProcessingState& State = Take.ProcessingState;

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 2.f)
    [ SNew(STextBlock).Text(LOCTEXT("ProcHeading", "Processing")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    UPCAPTakeProcessingQueue* Queue = GetQueue();
    if (!Queue)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).AutoWrapText(true).Text(PCAPTakeBrowserNoQueueText()).ColorAndOpacity(FSlateColor(ColAmber)) ];
        return Box;
    }

    const FPCAPTakeKey Handle = PCAPTakeBrowserHandleFor(Row);

    // Overall status — derived by the queue from the applicable steps, never hand-set.
    TSharedRef<SHorizontalBox> StatusRow = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(StatusName(State.OverallStatus))).ColorAndOpacity(FSlateColor(StatusColor(State.OverallStatus))) ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock)
          .Text(FText::FromString(State.bHasQueued
              ? FString::Printf(TEXT("queued %s"), *FormatRecordedAt(State.QueuedAt))
              : FString(TEXT("not queued yet"))))
          .ColorAndOpacity(FSlateColor(ColText3)) ];

    if (State.bHasQueued)
    {
        // The only route back out of the queue. Without it a take queued by mistake is
        // stuck there for good, because every other transition moves away from Pending.
        StatusRow->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SButton)
            .Text(LOCTEXT("RemoveFromQueue", "Remove from queue"))
            .ToolTipText(LOCTEXT("RemoveFromQueueTip", "Take this out of the worklist and clear its step plan and all recorded step progress. The recorded take itself is untouched."))
            .OnClicked_Lambda([this, Handle]()
            {
                UPCAPTakeProcessingQueue* Q = GetQueue();
                if (!Q) { NotifyOperator(PCAPTakeBrowserNoQueueText()); return FReply::Handled(); }
                FString Reason;
                if (!Q->RemoveTakeFromQueue(Handle, Reason))
                {
                    NotifyOperator(FText::FromString(Reason));
                    return FReply::Handled();
                }
                RefreshAll();
                return FReply::Handled();
            })
        ];
    }
    Box->AddSlot().AutoHeight()[ StatusRow ];

    // The single most important sentence in this panel. Each step then repeats it in its
    // own terms, using the queue's wording so there is exactly one source for the claim.
    Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 6.f)
    [ SNew(STextBlock).AutoWrapText(true)
      .Text(LOCTEXT("ProcAttested", "Every step below is done outside this panel and marked here by the operator. The editor performs none of them."))
      .ColorAndOpacity(FSlateColor(ColText3)) ];

    if (Take.Label == ETakeLabel::Burn)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("BurnNeverProcessed", "Burn — archived raw. This take is never processed."))
          .ColorAndOpacity(FSlateColor(ColRed)) ];
        return Box;
    }

    if (!State.bHasQueued)
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("NotQueuedYet", "Not queued yet — which steps this take needs is computed from its own manifest at the moment it is queued. Label it Best or Alt, then use \"Process All Queued\"."))
          .ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    for (const EPCAPProcessingStep Step : Queue->GetPipelineSteps())
    {
        const FProcessingStep* Entry = PCAPTakeBrowserFindStep(State, Step);
        if (!Entry) { continue; }

        const bool bApplies = Queue->IsStepApplicable(State, Step);
        const FLinearColor NameColor = bApplies ? FLinearColor::White : ColText3;

        TSharedRef<SVerticalBox> StepBox = SNew(SVerticalBox);

        StepBox->AddSlot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Queue->GetStepDisplayName(Step))).ColorAndOpacity(FSlateColor(NameColor)) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(STextBlock)
              .Text(FText::FromString(bApplies ? StatusName(Entry->Status)
                                               : FString(State.bHasQueued ? TEXT("not applicable") : TEXT("—"))))
              .ColorAndOpacity(FSlateColor(bApplies ? StatusColor(Entry->Status) : ColText3)) ]
        ];

        // Where the work actually happens, in the queue's own words — so this panel cannot
        // drift into implying the engine did it.
        StepBox->AddSlot().AutoHeight()
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(FText::FromString(Queue->GetStepExecutionNote(Step)))
          .ColorAndOpacity(FSlateColor(ColText3)) ];

        if (State.bHasQueued && !bApplies)
        {
            StepBox->AddSlot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(LOCTEXT("StepNotApplicable", "This take carries no stream that needs this step."))
              .ColorAndOpacity(FSlateColor(ColText3)) ];
        }
        else
        {
            // Timestamps. The queue stamps CompletedAt on both completion and failure, so
            // it reads as "ended", and bHasCompleted is the success bit.
            FString Times;
            if (Entry->bHasStarted) { Times += FString::Printf(TEXT("started %s"), *FormatRecordedAt(Entry->StartedAt)); }
            if (Entry->CompletedAt != FDateTime(0))
            {
                const FString EndVerb = Entry->bHasCompleted ? FString(TEXT("finished")) : FString(TEXT("ended"));
                if (!Times.IsEmpty()) { Times += TEXT("  ·  "); }
                Times += FString::Printf(TEXT("%s %s"), *EndVerb, *FormatRecordedAt(Entry->CompletedAt));
            }
            if (!Times.IsEmpty())
            {
                StepBox->AddSlot().AutoHeight()
                [ SNew(STextBlock).Text(FText::FromString(Times)).ColorAndOpacity(FSlateColor(ColText2)) ];
            }

            if (!Entry->ErrorMessage.IsEmpty())
            {
                StepBox->AddSlot().AutoHeight()
                [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Entry->ErrorMessage)).ColorAndOpacity(FSlateColor(ColRed)) ];
            }

            // Attestation controls. The queue owns the state machine and fills OutReason
            // with the operator-facing explanation whenever it refuses — so every disabled
            // control here says why, and every refusal is spoken rather than swallowed.
            FString BeginReason, CompleteReason, FailReason, ResetReason;
            const bool bCanBegin    = Queue->CanBeginStep(Handle, Step, BeginReason);
            const bool bCanComplete = Queue->CanCompleteStep(Handle, Step, CompleteReason);
            const bool bCanFail     = Queue->CanFailStep(Handle, Step, FailReason);
            const bool bCanReset    = Queue->CanResetStep(Handle, Step, ResetReason);

            StepBox->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("MarkStarted", "Mark started"))
                    .ToolTipText(bCanBegin
                        ? LOCTEXT("MarkStartedTip", "Record that the operator has begun this step elsewhere. It starts nothing in the editor.")
                        : FText::FromString(BeginReason))
                    .IsEnabled(bCanBegin)
                    .OnClicked_Lambda([this, Handle, Step]()
                    {
                        UPCAPTakeProcessingQueue* Q = GetQueue();
                        if (!Q) { NotifyOperator(PCAPTakeBrowserNoQueueText()); return FReply::Handled(); }
                        FString Reason;
                        if (!Q->BeginStep(Handle, Step, Reason)) { NotifyOperator(FText::FromString(Reason)); return FReply::Handled(); }
                        RefreshAll();
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("MarkComplete", "Mark complete"))
                    .ToolTipText(bCanComplete
                        ? LOCTEXT("MarkCompleteTip", "Record that the operator has finished this step, for every performer it covers.")
                        : FText::FromString(CompleteReason))
                    .IsEnabled(bCanComplete)
                    .OnClicked_Lambda([this, Handle, Step]()
                    {
                        UPCAPTakeProcessingQueue* Q = GetQueue();
                        if (!Q) { NotifyOperator(PCAPTakeBrowserNoQueueText()); return FReply::Handled(); }
                        FString Reason;
                        if (!Q->CompleteStep(Handle, Step, Reason)) { NotifyOperator(FText::FromString(Reason)); return FReply::Handled(); }
                        RefreshAll();
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [
                    // Retry a failure, or reopen something marked complete by mistake. The
                    // queue resets whatever was built on this step along with it.
                    SNew(SButton)
                    .Text(LOCTEXT("ResetStep", "Reset"))
                    .ToolTipText(bCanReset
                        ? LOCTEXT("ResetStepTip", "Put this step back in the queue. Any step that depends on it is reset too, since a result built on it is no longer true.")
                        : FText::FromString(ResetReason))
                    .IsEnabled(bCanReset)
                    .OnClicked_Lambda([this, Handle, Step]()
                    {
                        UPCAPTakeProcessingQueue* Q = GetQueue();
                        if (!Q) { NotifyOperator(PCAPTakeBrowserNoQueueText()); return FReply::Handled(); }
                        FString Reason;
                        if (!Q->ResetStep(Handle, Step, Reason)) { NotifyOperator(FText::FromString(Reason)); return FReply::Handled(); }
                        RefreshAll();
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    // A failure needs a sentence, not a red dot. ErrorMessage is the step's
                    // only free text, and the steps are per take while the HMC solve and the
                    // retarget are really per subject — so "failed for actor B, actor A is
                    // fine" has nowhere else to live.
                    SNew(SEditableTextBox)
                    .HintText(LOCTEXT("ErrorHint", "record a problem  ↵"))
                    .ToolTipText(bCanFail
                        ? LOCTEXT("FailTip", "Record what went wrong. The message is required and is shown against this step until it is reset.")
                        : FText::FromString(FailReason))
                    .IsEnabled(bCanFail)
                    .OnTextCommitted_Lambda([this, Handle, Step](const FText& Committed, ETextCommit::Type CommitType)
                    {
                        if (CommitType != ETextCommit::OnEnter) { return; }
                        const FString Message = Committed.ToString().TrimStartAndEnd();
                        if (Message.IsEmpty()) { return; }
                        UPCAPTakeProcessingQueue* Q = GetQueue();
                        if (!Q) { NotifyOperator(PCAPTakeBrowserNoQueueText()); return; }
                        FString Reason;
                        if (!Q->FailStep(Handle, Step, Message, Reason)) { NotifyOperator(FText::FromString(Reason)); return; }
                        RefreshAll();
                    })
                ]
            ];
        }

        Box->AddSlot().AutoHeight().Padding(0.f, 4.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f, 6.f))
            [ StepBox ]
        ];
    }

    if (!State.OutputSequence.IsNull())
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(FText::FromString(FString::Printf(TEXT("output sequence: %s"), *State.OutputSequence.ToString())))
          .ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    return Box;
}

// ── Actions ─────────────────────────────────────────────────────────────────

void SPCAPTakeBrowser::SetLabelForRow(const FPCAPTakeBrowserRow& Row, ETakeLabel NewLabel)
{
    UMocapDatabase* DB = GetDB();
    FTake* Take = ResolveTake(Row);
    if (!DB || !Take)
    {
        NotifyOperator(LOCTEXT("LabelNoTake", "That take is no longer in the database — nothing was changed."));
        RefreshAll();
        return;
    }

    if (Take->Label == NewLabel)
    {
        return;   // already there — no write, no toast, no churn
    }

    const bool bWasQueued = Take->ProcessingState.bHasQueued;
    Take->Label = NewLabel;
    DB->MarkPackageDirty();
    SaveDB();

    // Epic's Mocap Manager keeps its own copy of the label as FPCapTakeRecord.TakeStatus.
    // Republish so the two stores do not drift; WriteTake resolves its own session binding
    // by TakeID, so it works for any historical take. Advisory only — the PCAP database is
    // the record of truth and a failed republish must never undo a label the operator set.
    if (!UPCAPTakeRecordWriter::WriteTake(*Take))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Take Browser: take '%s' relabelled, but the Mocap Manager take record was not updated. The label is safe in the database."),
            *Row.TakeID);
    }

    if (bWasQueued && (NewLabel == ETakeLabel::Burn || NewLabel == ETakeLabel::Captured))
    {
        NotifyOperator(FText::FromString(FString::Printf(
            TEXT("%s is now %s. It was already queued — it will no longer be processed. Its recorded data is untouched."),
            *Row.TakeID, *LabelName(NewLabel))));
    }

    RefreshAll();   // the batch payload just changed too
}

void SPCAPTakeBrowser::NotifyOperator(const FText& Message)
{
    FNotificationInfo Info(Message);
    Info.ExpireDuration = 4.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

// ── Formatting / palette ────────────────────────────────────────────────────

FString SPCAPTakeBrowser::FormatRecordedAt(const FDateTime& When)
{
    return (When == FDateTime(0)) ? FString(TEXT("—")) : When.ToString(TEXT("%Y-%m-%d %H:%M"));
}

FString SPCAPTakeBrowser::FormatDuration(float Seconds)
{
    // FTake::DurationSeconds has no writer anywhere in the pipeline, so this is "—"
    // for every take that exists today. Shown as unknown rather than as 0:00.
    if (Seconds <= 0.f) { return FString(TEXT("—")); }
    const int32 Whole = FMath::FloorToInt(Seconds);
    return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

FString SPCAPTakeBrowser::LabelName(ETakeLabel Label)
{
    // Straight off the UENUM so the four locked names have one source of truth.
    UEnum* LabelEnum = StaticEnum<ETakeLabel>();
    return LabelEnum ? LabelEnum->GetDisplayNameTextByValue((int64)Label).ToString() : FString();
}

FString SPCAPTakeBrowser::StatusName(EProcessingStatus Status)
{
    UEnum* StatusEnum = StaticEnum<EProcessingStatus>();
    return StatusEnum ? StatusEnum->GetDisplayNameTextByValue((int64)Status).ToString() : FString();
}

FLinearColor SPCAPTakeBrowser::LabelColor(ETakeLabel Label) const
{
    // The locked take-label palette: Captured gray, Best gold, Alt green, Burn red.
    switch (Label)
    {
        case ETakeLabel::Best: return ColGold;
        case ETakeLabel::Alt:  return ColGreen;
        case ETakeLabel::Burn: return ColRed;
        default:               return ColText2;   // Captured — the panel's gray
    }
}

FLinearColor SPCAPTakeBrowser::StatusColor(EProcessingStatus Status) const
{
    switch (Status)
    {
        case EProcessingStatus::Queued:     return ColText2;
        case EProcessingStatus::InProgress: return ColAmber;
        case EProcessingStatus::Complete:   return ColGreen;
        case EProcessingStatus::Failed:     return ColRed;
        default:                            return ColText3;   // Pending — never admitted
    }
}

#undef LOCTEXT_NAMESPACE
