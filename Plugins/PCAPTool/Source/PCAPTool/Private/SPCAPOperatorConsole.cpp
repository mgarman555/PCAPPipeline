#include "SPCAPOperatorConsole.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPTakeRecorderSubsystem.h"
#include "PCAPMocapBridge.h"        // UPCAPMocapBridge::SpawnShotToStage — the 5.8 Mocap Manager bridge
#include "PropRosterEntry.h"        // UPropRosterEntry — prop records SpawnShotToStage matches by PropID
#include "PCAPVCamSubsystem.h"      // UPCAPVCamSubsystem::GetStreamStatus — the strip's VCam rollup
#include "StageConfigAsset.h"       // UStageConfigAsset::VCamSystem — does this stage record a vcam at all

#include "Engine/Engine.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"   // director / commentator notes on the review card
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"   // FAssetRegistryModule — enumerate the prop roster
#include "Modules/ModuleManager.h"               // FModuleManager
#include "PropertyCustomizationHelpers.h"        // SObjectPropertyEntryBox — the DrivenTarget search box
#include "Editor.h"                              // GEditor
#include "Engine/World.h"                        // UWorld — the spawn target
#include "ScopedTransaction.h"                   // FScopedTransaction — Ctrl-Z a mis-fire
#include "FileHelpers.h"                         // FEditorFileUtils — DB edits only dirty the package
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PCAPOperatorConsole"

#include "SPCAPPanelStyle.h"

namespace
{

    FLinearColor StreamColor(EStreamStatus S)
    {
        switch (S)
        {
            case EStreamStatus::Connected:  return FLinearColor(0.290f, 0.878f, 0.502f);
            case EStreamStatus::Degraded:   return FLinearColor(0.878f, 0.627f, 0.188f);
            default:                        return FLinearColor(0.878f, 0.251f, 0.251f);
        }
    }

    // Disconnected is worse than Degraded is worse than Connected. The operator strip rolls
    // up to the worst, because the worst is the one AreActiveStreamsReady() refuses on.
    int32 StreamSeverity(EStreamStatus S)
    {
        switch (S)
        {
            case EStreamStatus::Connected:  return 0;
            case EStreamStatus::Degraded:   return 1;
            default:                        return 2;
        }
    }

    // A Live Link subject name for a dot's tooltip — empty rather than the FName's "None",
    // which reads like a subject actually called that.
    FText SubjectNameText(FName SubjectName)
    {
        return SubjectName.IsNone() ? FText::GetEmpty() : FText::FromName(SubjectName);
    }
}

void SPCAPOperatorConsole::Construct(const FArguments& InArgs)
{
    // Seed selection from the DB's active state, else the first production/day/session.
    if (UMocapDatabase* DB = GetDB())
    {
        SelProduction = DB->ActiveProductionCode;
        SelDay        = DB->ActiveDayID;
        SelSession    = DB->ActiveSessionID;
        SelShot       = DB->ActiveShotID;
        if (SelProduction.IsEmpty() && DB->Productions.Num() > 0)
        {
            const FProduction& P = DB->Productions[0];
            SelProduction = P.ProjectCode;
            if (P.Days.Num() > 0)
            {
                SelDay = P.Days[0].DayID;
                if (P.Days[0].Sessions.Num() > 0) SelSession = P.Days[0].Sessions[0].SessionID;
            }
        }
    }

    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            // Header — pickers + record state
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SAssignNew(HeaderBox, SBox) ]
            ]

            // Body — shot spine + context
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(0.4f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4.f)
                    [
                        SAssignNew(ShotListView, SListView<TSharedPtr<FString>>)
                        .ListItemsSource(&ShotItems)
                        .OnGenerateRow(this, &SPCAPOperatorConsole::OnGenerateShotRow)
                        .OnSelectionChanged(this, &SPCAPOperatorConsole::OnShotSelected)
                        .SelectionMode(ESelectionMode::Single)
                        // The spine is built once and never rebuilt, so its lock has to be a
                        // live attribute rather than a snapshot: clicking another shot mid-take
                        // rewrites the DB's active selection under the take in flight.
                        .IsEnabled_Lambda([this]() { return !IsNavigationLocked(); })
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(0.6f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(ShotContextBox, SBox) ]
                ]
            ]

            // Operator strip — the Body · Face · Audio · VCam rollup, always visible so
            // readiness is glanceable without selecting a shot first.
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SAssignNew(HealthStripBox, SBox) ]
            ]
        ]
    ];

    RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SPCAPOperatorConsole::PollRecordState));

    LastHealthSignature = ComputeHealthSignature();

    RebuildHeader();
    RebuildShotList();
    RebuildContext();
    RebuildHealthStrip();
}

UMocapDatabase* SPCAPOperatorConsole::GetDB() const
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    return Settings ? Settings->GetDatabase() : nullptr;
}

UPCAPTakeRecorderSubsystem* SPCAPOperatorConsole::GetRecorder() const
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>() : nullptr;
}

EActiveTimerReturnType SPCAPOperatorConsole::PollRecordState(double, float)
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    const EPCAPRecordState State = Rec ? Rec->GetRecordState() : EPCAPRecordState::Ready;

    const bool bStateChanged = ((uint8)State != LastRecordState);

    // Stream health moves without the record state moving — a lav drops, an HMC reconnects —
    // and both the strip and the RECORD gate have to follow it. REVIEWING is exempt: the
    // review card owns the context pane, and rebuilding it under the operator's hands would
    // throw away half-typed notes.
    const uint32 Signature   = ComputeHealthSignature();
    const bool bHealthChanged = (Signature != LastHealthSignature) && (State != EPCAPRecordState::Reviewing);

    LastRecordState     = (uint8)State;
    LastHealthSignature = Signature;

    if (bStateChanged)
    {
        RebuildHeader();
        // A finished take changes the spine's take count and its ○/✓/★ coverage glyph, both
        // of which are computed per row and would otherwise keep the pre-take values.
        RefreshShotRows();
    }
    if (bStateChanged || bHealthChanged)
    {
        RebuildContext();
        RebuildHealthStrip();
    }
    return EActiveTimerReturnType::Continue;
}

void SPCAPOperatorConsole::PushSelectionToDB()
{
    if (UMocapDatabase* DB = GetDB())
    {
        DB->ActiveProductionCode = SelProduction;
        DB->ActiveDayID          = SelDay;
        DB->ActiveSessionID      = SelSession;
        DB->ActiveShotID         = SelShot;
    }
}

// ── Header ────────────────────────────────────────────────────────────────

// The three navigation pickers are rebuilt by RebuildHeader() on every record-state change,
// so their lock is a snapshot rather than an attribute — same shape as the Send button's gate.

TSharedRef<SWidget> SPCAPOperatorConsole::BuildProductionPicker()
{
    const FString Label = SelProduction.IsEmpty() ? TEXT("(production)") : SelProduction;
    return SNew(SComboButton).IsEnabled(!IsNavigationLocked()).ToolTipText(NavLockTip())
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
            {
                TArray<FProduction> Prods = DB->Productions;   // sorted copy — alphabetical
                Prods.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
                for (const FProduction& P : Prods)
                {
                    const FString Code = P.ProjectCode;
                    MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s — %s"), *Code, *P.ProductionName)), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Code]() { SelProduction = Code; SelDay.Empty(); SelSession.Empty(); SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); RebuildHealthStrip(); })));
                }
            }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildDayPicker()
{
    const FString Label = SelDay.IsEmpty() ? TEXT("(day)") : (TEXT("Day_") + SelDay);
    return SNew(SComboButton).IsEnabled(!IsNavigationLocked()).ToolTipText(NavLockTip())
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
                if (FProduction* P = DB->GetProductionByCode(SelProduction))
                {
                    TArray<FShootDay> Days = P->Days;   // sorted copy — alphabetical
                    Days.Sort([](const FShootDay& A, const FShootDay& B){ return A.DayID < B.DayID; });
                    for (const FShootDay& D : Days)
                    {
                        const FString Day = D.DayID;
                        MB.AddMenuEntry(FText::FromString(TEXT("Day_") + Day), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Day]() { SelDay = Day; SelSession.Empty(); SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); RebuildHealthStrip(); })));
                    }
                }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildSessionPicker()
{
    const FString Label = SelSession.IsEmpty() ? TEXT("(session)") : (TEXT("Session_") + SelSession);
    return SNew(SComboButton).IsEnabled(!IsNavigationLocked()).ToolTipText(NavLockTip())
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
                if (FShootDay* D = DB->GetDay(SelProduction, SelDay))
                {
                    TArray<FSession> Sessions = D->Sessions;   // sorted copy — alphabetical
                    Sessions.Sort([](const FSession& A, const FSession& B){ return A.SessionID < B.SessionID; });
                    for (const FSession& S : Sessions)
                    {
                        const FString Sess = S.SessionID;
                        MB.AddMenuEntry(FText::FromString(TEXT("Session_") + Sess), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Sess]() { SelSession = Sess; SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); RebuildHealthStrip(); })));
                    }
                }
            return MB.MakeWidget();
        });
}

// ── Transport ownership / deferral (UE 5.8 Mocap Manager) ─────────────────────
//
// There is exactly one Take Recorder transport and the Mocap Manager drives it too. The
// subsystem already refuses to compete for it; the console's job is to say so, because a
// deferral nobody can see looks identical to a broken RECORD button.

FText SPCAPOperatorConsole::TransportNoteText() const
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    if (!Rec) return LOCTEXT("NoRecorder", "Record controller unavailable.");

    // "PCAPTool", "Mocap Manager", or empty while nothing is recording.
    const FString Owner = Rec->GetTransportOwner();

    // Kept to one short line — it sits in the header. The full sentence is on the RECORD and
    // STOP tooltips, which is where an operator looks once this line tells them to.
    if (Rec->IsObservingExternalRecord())
    {
        return FText::Format(
            LOCTEXT("TransportObserving", "{0} is driving this take — stop it there"),
            FText::FromString(Owner.IsEmpty() ? TEXT("Mocap Manager") : Owner));
    }
    if (!Owner.IsEmpty())
    {
        return FText::Format(LOCTEXT("TransportOwner", "transport: {0}"), FText::FromString(Owner));
    }
    if (Rec->ShouldDeferToMocapManager())
    {
        return LOCTEXT("TransportDeferring", "deferring to the Mocap Manager — record from there");
    }
    return FText::GetEmpty();
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildDeferralPicker()
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    const EPCAPRecorderDeferral Mode = Rec ? Rec->GetDeferralMode() : EPCAPRecorderDeferral::Auto;

    // The enum's display names are full sentences — right for the menu, far too wide for the
    // header. The button carries the short enumerator name instead.
    const UEnum* ModeEnum  = StaticEnum<EPCAPRecorderDeferral>();
    const int32 ModeIndex  = ModeEnum ? ModeEnum->GetIndexByValue((int64)Mode) : INDEX_NONE;
    const FString ModeName = (ModeEnum && ModeIndex != INDEX_NONE) ? ModeEnum->GetNameStringByIndex(ModeIndex) : TEXT("Auto");

    // NeverDefer removes the only guard against two controllers on one transport, so it does
    // not read as a neutral setting.
    const FLinearColor ModeColor = (Mode == EPCAPRecorderDeferral::NeverDefer) ? ColAmber : ColText2;

    return SNew(SComboButton)
        .IsEnabled(Rec != nullptr && !IsNavigationLocked())
        .ToolTipText(LOCTEXT("DeferralTip", "There is one Take Recorder transport. This is what PCAPTool does when the Mocap Manager is driving it — Auto defers and observes, which is the safe default."))
        .ButtonContent()
        [ SNew(STextBlock).Text(FText::FromString(TEXT("defer: ") + ModeName)).ColorAndOpacity(FSlateColor(ModeColor)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (const UEnum* E = StaticEnum<EPCAPRecorderDeferral>())
            {
                for (int32 Index = 0; Index < E->NumEnums() - 1; ++Index)   // skip the implicit _MAX
                {
                    const EPCAPRecorderDeferral Value = (EPCAPRecorderDeferral)E->GetValueByIndex(Index);
                    MB.AddMenuEntry(E->GetDisplayNameTextByIndex(Index), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Value]()
                        {
                            if (UPCAPTakeRecorderSubsystem* R = GetRecorder()) R->SetDeferralMode(Value);
                            RebuildHeader();
                            RebuildContext();   // the RECORD gate reads the mode through ShouldDeferToMocapManager()
                        })));
                }
            }
            return MB.MakeWidget();
        });
}

void SPCAPOperatorConsole::RebuildHeader()
{
    if (!HeaderBox.IsValid()) return;

    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    EPCAPRecordState State = Rec ? Rec->GetRecordState() : EPCAPRecordState::Ready;

    FText StateText = LOCTEXT("Ready", "READY");
    FLinearColor StateColor = ColText2;
    if (State == EPCAPRecordState::Capturing) { StateText = LOCTEXT("Capturing", "CAPTURING"); StateColor = ColGreen; }
    else if (State == EPCAPRecordState::Reviewing) { StateText = LOCTEXT("Reviewing", "REVIEWING"); StateColor = ColAmber; }

    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildProductionPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildDayPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildSessionPicker() ]
        + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(8.f, 0.f)
          [ SNew(STextBlock).Text(TransportNoteText()).ColorAndOpacity(FSlateColor(ColAmber)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f) [ BuildDeferralPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [ SNew(STextBlock).Text(StateText).ColorAndOpacity(FSlateColor(StateColor)) ];

    if (State == EPCAPRecordState::Capturing)
    {
        // STOP still shows on a take PCAPTool is only observing — the state bar must not
        // pretend nothing is recording — but it is disabled and says where to stop it.
        // StopRecord() refuses a foreign take by design; what it never did was say so.
        const bool bObserving = Rec && Rec->IsObservingExternalRecord();
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
        [
            SNew(SButton).Text(LOCTEXT("Stop", "STOP"))
            .IsEnabled(!bObserving)
            .ToolTipText(bObserving
                ? LOCTEXT("StopForeign", "The Mocap Manager started this take — PCAPTool cannot stop it. Stop it there; the take is still harvested and published here.")
                : LOCTEXT("StopTip", "End the take in flight."))
            .OnClicked(this, &SPCAPOperatorConsole::OnStopClicked)
        ];
    }

    HeaderBox->SetContent(Row);
}

// ── Shot list ────────────────────────────────────────────────────────────

void SPCAPOperatorConsole::RebuildShotList()
{
    ShotItems.Reset();
    if (UMocapDatabase* DB = GetDB())
        if (FSession* Session = DB->GetSession(SelProduction, SelDay, SelSession))
        {
            for (const FShot& S : Session->Shots)
                ShotItems.Add(MakeShared<FString>(S.ShotID));
            ShotItems.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
            {
                return A.IsValid() && B.IsValid() && *A < *B;   // alphabetical
            });
        }

    if (ShotListView.IsValid()) ShotListView->RequestListRefresh();
}

void SPCAPOperatorConsole::RefreshShotRows()
{
    // The take count and the ○/✓/★ coverage glyph are computed inside OnGenerateShotRow, so
    // after a take lands the spine keeps whatever the rows were last generated with — the
    // shot that just got its first take still reads ○ for the rest of the day.
    //
    // RequestListRefresh re-generates the rows against the same items. RebuildShotList()
    // would too, but it replaces every ShotItems entry, which drops the list's selection.
    if (ShotListView.IsValid()) ShotListView->RequestListRefresh();
}

TSharedRef<ITableRow> SPCAPOperatorConsole::OnGenerateShotRow(TSharedPtr<FString> ShotID, const TSharedRef<STableViewBase>& Owner)
{
    FShot* Shot = nullptr;
    if (UMocapDatabase* DB = GetDB()) Shot = DB->GetShot(SelProduction, SelDay, SelSession, ShotID.IsValid() ? *ShotID : FString());

    const int32 NumTakes = Shot ? Shot->Takes.Num() : 0;
    const bool bBest = Shot && Shot->Takes.ContainsByPredicate([](const FTake& T) { return T.Label == ETakeLabel::Best; });
    const FString Glyph = bBest ? TEXT("★") : (NumTakes > 0 ? TEXT("✓") : TEXT("○"));
    const FString Desc  = Shot ? Shot->Description : FString();
    const bool bCurrent = ShotID.IsValid() && *ShotID == SelShot;

    return SNew(STableRow<TSharedPtr<FString>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 4.f, 8.f, 4.f)
        [ SNew(STextBlock).Text(FText::FromString(Glyph)).ColorAndOpacity(FSlateColor(bBest ? ColGreen : ColText2)) ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(ShotID.IsValid() ? *ShotID : FString())).ColorAndOpacity(FSlateColor(bCurrent ? ColGreen : FLinearColor::White)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(Desc)).ColorAndOpacity(FSlateColor(ColText3)) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 4.f, 0.f)
        [ SNew(STextBlock).Text(FText::AsNumber(NumTakes)).ColorAndOpacity(FSlateColor(ColText3)) ]
    ];
}

void SPCAPOperatorConsole::OnShotSelected(TSharedPtr<FString> ShotID, ESelectInfo::Type)
{
    // The spine is disabled during CAPTURING / REVIEWING, so this is the second lock: it
    // keeps a programmatic selection change from rewriting the DB's active shot through
    // RebuildContext() while a take is in flight.
    if (IsNavigationLocked()) return;

    SelShot = ShotID.IsValid() ? *ShotID : FString();
    RebuildContext();
    RebuildHealthStrip();
}

// ── Context ────────────────────────────────────────────────────────────────

void SPCAPOperatorConsole::RebuildContext()
{
    if (!ShotContextBox.IsValid()) return;

    UMocapDatabase* DB = GetDB();
    FShot* Shot = DB ? DB->GetShot(SelProduction, SelDay, SelSession, SelShot) : nullptr;

    // Resolve the record state BEFORE the no-shot guard. REVIEWING has exactly one
    // non-error exit in the whole plugin — the review card's Done, which calls
    // FinishReview — so if a selection stops resolving mid-review (a shot renamed or
    // removed under us, or Active* pointing at a day that no longer holds it) and this
    // returned early, the console would sit in amber REVIEWING with no way out and every
    // take stuck on the default Captured label.
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    EPCAPRecordState State = Rec ? Rec->GetRecordState() : EPCAPRecordState::Ready;

    if (!Shot)
    {
        if (State == EPCAPRecordState::Reviewing)
        {
            ShotContextBox->SetContent(
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                [ SNew(STextBlock)
                    .Text(LOCTEXT("ReviewNoShot", "Reviewing the last take, but its shot is no longer selectable.\nFinish the review to get back to the shot list."))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(ColAmber)) ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SButton)
                    .Text(LOCTEXT("ReviewNoShotDone", "Done"))
                    .ToolTipText(LOCTEXT("ReviewNoShotDoneTip", "Close the review and return to READY. The take keeps whatever label it already has."))
                    .OnClicked(this, &SPCAPOperatorConsole::OnFinishReviewClicked) ]);
            return;
        }

        ShotContextBox->SetContent(SNew(STextBlock).Text(LOCTEXT("PickShot", "Select a shot from the list.")).ColorAndOpacity(FSlateColor(ColText2)));
        return;
    }

    FString NextTakeID;
    if (DB) { DB->ActiveProductionCode = SelProduction; DB->ActiveDayID = SelDay; DB->ActiveSessionID = SelSession; DB->ActiveShotID = SelShot; NextTakeID = DB->BuildNextTakeID(); }

    // REVIEWING owns the pane. Every successful harvest enters it, and the review card's
    // Done is the only non-error way out (FinishReview) — without it the state is a one-way
    // door and every take keeps the default Captured label.
    if (State == EPCAPRecordState::Reviewing)
    {
        ShotContextBox->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[ BuildReviewCard(*Shot) ]);
        return;
    }

    const bool bNavLocked = IsNavigationLocked();

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   %s"), *Shot->ShotID, *Shot->Description))).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("Talent", "Talent")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    for (const FShotSubject& Subj : Shot->Subjects)
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 2.f)[ BuildSubjectRow(Subj, bNavLocked) ];
    }

    // Props — the send read-out below counts them, but nothing ever listed them, so a shot's
    // tracked-prop status was invisible on the one surface that runs the day.
    Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 4.f)
    [ SNew(STextBlock).Text(LOCTEXT("Props", "Props")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (Shot->Props.Num() == 0)
    {
        Box->AddSlot().AutoHeight()
        [ SNew(STextBlock).Text(LOCTEXT("NoProps", "none")).ColorAndOpacity(FSlateColor(ColText3)) ];
    }
    else
    {
        for (const FPropEntry& Prop : Shot->Props)
        {
            Box->AddSlot().AutoHeight().Padding(0.f, 2.f)[ BuildPropRow(Prop) ];
        }
    }

    // Takes
    Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 4.f)[ SNew(STextBlock).Text(LOCTEXT("Takes", "Takes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    TSharedRef<SVerticalBox> TakeList = SNew(SVerticalBox);
    for (const FTake& T : Shot->Takes)
    {
        const FString LabelStr = StaticEnum<ETakeLabel>()->GetDisplayNameTextByValue((int64)T.Label).ToString();
        TakeList->AddSlot().AutoHeight().Padding(0.f, 1.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   %s"), *T.TakeID, *LabelStr))).ColorAndOpacity(FSlateColor(ColText2)) ];
    }
    Box->AddSlot().AutoHeight()[ TakeList ];

    // Record controls. RECORD names its blocker on the button instead of failing into the
    // Output Log — the gate is the same AreActiveStreamsReady() the record itself runs, so
    // the tooltip is the reason the press would have produced ("kevinDorman face stream not
    // connected"). STOP on an observed take is disabled rather than a silent no-op.
    FText RecordBlockedReason;
    const bool bCanRecord = CanRecordNow(RecordBlockedReason);
    const bool bObserving = Rec && Rec->IsObservingExternalRecord();

    Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(2.f).Padding(0.f, 0.f, 6.f, 0.f)
        [
            (State == EPCAPRecordState::Capturing)
            ? StaticCastSharedRef<SWidget>(SNew(SButton).Text(LOCTEXT("Stop2", "STOP"))
                .IsEnabled(!bObserving)
                .ToolTipText(bObserving
                    ? LOCTEXT("StopForeign2", "The Mocap Manager started this take — PCAPTool cannot stop it. Stop it there; the take is still harvested and published here.")
                    : LOCTEXT("StopTip2", "End the take in flight."))
                .OnClicked(this, &SPCAPOperatorConsole::OnStopClicked))
            : StaticCastSharedRef<SWidget>(SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("RECORD · %s"), *NextTakeID)))
                .IsEnabled(bCanRecord)
                .ToolTipText(bCanRecord
                    ? FText::Format(LOCTEXT("RecordTip", "Record {0} on {1}."), FText::FromString(NextTakeID), FText::FromString(Shot->ShotID))
                    : RecordBlockedReason)
                .OnClicked(this, &SPCAPOperatorConsole::OnRecordClicked))
        ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ SNew(SButton).Text(LOCTEXT("NextTake", "Next take"))
          .IsEnabled(bCanRecord)
          .ToolTipText(bCanRecord
              ? FText::Format(LOCTEXT("NextTakeTip", "Record {0} — same setup, next take number."), FText::FromString(NextTakeID))
              : RecordBlockedReason)
          .OnClicked(this, &SPCAPOperatorConsole::OnNextTakeClicked) ]
    ];

    // Prep — send the called shot to the Mocap Manager. Its own row below RECORD,
    // at natural width: this stages actors, it does not capture. Never a silent
    // no-op — when it's unavailable the button greys out and the tooltip says why.
    FText SendBlockedReason;
    const bool bCanSend    = CanSendShotToMocapManager(SendBlockedReason);
    const bool bAlreadySent = SentShotKeys.Contains(CurrentShotKey());
    // Count only what will actually be staged: the bridge spawns called (bIsActive)
    // talent, and every listed prop. Counting all subjects here would over-promise.
    int32 NumCalledTalent = 0;
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (Subj.bIsActive) { ++NumCalledTalent; }
    }
    const FString SendCounts = FString::Printf(TEXT("%d talent · %d props"), NumCalledTalent, Shot->Props.Num());

    Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SButton)
            .Text(bAlreadySent ? LOCTEXT("SendShotAgain", "Re-send shot to Mocap Manager")
                               : LOCTEXT("SendShot", "Send shot to Mocap Manager"))
            .ToolTipText(!bCanSend ? SendBlockedReason
                : (bAlreadySent
                    ? LOCTEXT("SendShotAgainTip", "This shot was already sent this session — sending again spawns a second set of actors. Ctrl-Z undoes a send.")
                    : LOCTEXT("SendShotTip", "Spawn this shot's called talent as Performance Capture performers and its props as tracked prop actors, in the current level. Ctrl-Z undoes.")))
            .IsEnabled(bCanSend)
            .OnClicked(this, &SPCAPOperatorConsole::OnSendShotToMocapManagerClicked)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(SendCounts)).ColorAndOpacity(FSlateColor(ColText3)) ]
    ];

    ShotContextBox->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[ Box ]);
}

// ── Talent / prop rows ───────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPOperatorConsole::MakeStreamDot(const FText& Label, bool bCarried, EStreamStatus Status, const FText& ToolTip) const
{
    // Nothing to say about a stream this shot does not carry — Status is meaningless there.
    const FText Tip = bCarried ? ToolTip : FText::GetEmpty();

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(Label).ToolTipText(Tip).ColorAndOpacity(FSlateColor(bCarried ? ColText2 : ColInactive)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ToolTipText(Tip)
          .ColorAndOpacity(FSlateColor(bCarried ? StreamColor(Status) : ColInactive)) ];
}

FShotSubject* SPCAPOperatorConsole::FindSelectedSubject(const FString& ActorID) const
{
    UMocapDatabase* DB = GetDB();
    FShot* Shot = DB ? DB->GetShot(SelProduction, SelDay, SelSession, SelShot) : nullptr;
    if (!Shot) return nullptr;

    for (FShotSubject& Subj : Shot->Subjects)
    {
        if (Subj.ActorID == ActorID) return &Subj;
    }
    return nullptr;
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildSubjectRow(const FShotSubject& Subj, bool bNavLocked)
{
    // Captured by value and re-resolved on use: FShotSubject lives inside FShot::Subjects,
    // so any Add anywhere in the hierarchy dangles a held pointer.
    const FString ActorID = Subj.ActorID;

    // Audio rolls up to its worst channel — one red lav is enough for
    // AreActiveStreamsReady() to refuse the record, so it belongs on screen next to body
    // and face rather than only in the Output Log.
    EStreamStatus AudioWorst = EStreamStatus::Connected;
    FString AudioChannels;
    for (const FAudioStreamEntry& Audio : Subj.AudioStreams)
    {
        if (StreamSeverity(Audio.StreamStatus) > StreamSeverity(AudioWorst)) AudioWorst = Audio.StreamStatus;
        if (!AudioChannels.IsEmpty()) AudioChannels += TEXT(", ");
        AudioChannels += Audio.ChannelID;
    }

    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Subj.ActorID)).ColorAndOpacity(FSlateColor(Subj.bIsActive ? FLinearColor::White : ColText3)) ]

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
        [ MakeStreamDot(LOCTEXT("Body", "body"), Subj.bHasBodyStream, Subj.BodyStream.StreamStatus,
                        SubjectNameText(Subj.BodyStream.LiveLinkSubjectName)) ]

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
        [ MakeStreamDot(LOCTEXT("Face", "face"), Subj.bHasFaceStream, Subj.FaceStream.StreamStatus,
                        SubjectNameText(Subj.FaceStream.LiveLinkSubjectName)) ]

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
        [ MakeStreamDot(LOCTEXT("Audio", "audio"), Subj.AudioStreams.Num() > 0, AudioWorst,
                        FText::FromString(AudioChannels)) ];

    // drives — the DrivenTarget every downstream consumer reads and nothing in the plugin
    // wrote: ACapturePerformer::SetMocapMesh takes it as the performer's mocap mesh (so the
    // Send button below spawns meshless performers while it is empty), and the take manifest
    // records it as provenance. Locked during a take — the manifest is snapshotted at
    // harvest, so swapping the target mid-performance would record a target that never drove.
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 4.f, 0.f)
    [ SNew(STextBlock).Text(LOCTEXT("Drives", "drives")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
    [
        // The lock sits on the box, not the picker: a disabled parent disables everything
        // inside it, and the entry box keeps its own argument list untouched.
        SNew(SBox).WidthOverride(240.f).IsEnabled(!bNavLocked)
        [
            SNew(SObjectPropertyEntryBox)
            .AllowedClass(UObject::StaticClass())
            .DisplayThumbnail(false)
            .ToolTipText(LOCTEXT("DrivesTip", "What this performance drives — a skeletal mesh, MetaHuman or placed actor. A USkeletalMesh here becomes the performer's mocap mesh when the shot is sent to the Mocap Manager, and every take records it as provenance."))
            .ObjectPath_Lambda([this, ActorID]() -> FString
            {
                const FShotSubject* Found = FindSelectedSubject(ActorID);
                return Found ? Found->DrivenTarget.ToString() : FString();
            })
            .OnObjectChanged_Lambda([this, ActorID](const FAssetData& AD)
            {
                if (FShotSubject* Found = FindSelectedSubject(ActorID))
                {
                    Found->DrivenTarget = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath());
                    if (UMocapDatabase* D = GetDB()) D->MarkPackageDirty();   // saved with the day, as the Call Sheet does
                    RebuildContext();
                }
            })
        ]
    ];

    return Row;
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildPropRow(const FPropEntry& Prop) const
{
    // An untracked prop carries no stream to be down — saying "untracked" is honest where a
    // red dot would read as a failure.
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Prop.PropID)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
        [
            Prop.bIsTracked
            ? MakeStreamDot(LOCTEXT("Tracked", "tracked"), true, Prop.StreamStatus,
                            SubjectNameText(Prop.LiveLinkSubjectName))
            : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Untracked", "untracked")).ColorAndOpacity(FSlateColor(ColInactive)))
        ];
}

// ── Operator strip — Body · Face · Audio · VCam ───────────────────────────────

uint32 SPCAPOperatorConsole::ComputeHealthSignature() const
{
    // Cheap fingerprint of everything the strip and the RECORD gate draw from. An unchanged
    // signature means nothing on screen would change, so the poll leaves the pane alone.
    uint32 Signature = 2166136261u;
    auto Mix = [&Signature](uint32 Value) { Signature = (Signature * 31u) + Value; };

    UMocapDatabase* DB = GetDB();
    const FShot* Shot = DB ? DB->GetShot(SelProduction, SelDay, SelSession, SelShot) : nullptr;
    if (Shot)
    {
        for (const FShotSubject& Subj : Shot->Subjects)
        {
            Mix(Subj.bIsActive ? 1u : 0u);
            Mix(Subj.bHasBodyStream ? (uint32)Subj.BodyStream.StreamStatus + 1u : 0u);
            Mix(Subj.bHasFaceStream ? (uint32)Subj.FaceStream.StreamStatus + 1u : 0u);
            for (const FAudioStreamEntry& Audio : Subj.AudioStreams) { Mix((uint32)Audio.StreamStatus + 1u); }
        }
        for (const FPropEntry& Prop : Shot->Props)
        {
            Mix(Prop.bIsTracked ? (uint32)Prop.StreamStatus + 1u : 0u);
        }
        Mix((uint32)Shot->Takes.Num());   // a take harvested by any surface changes the pane too
    }

    if (GEngine)
    {
        if (UPCAPVCamSubsystem* VCam = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
        {
            Mix((uint32)VCam->GetStreamStatus() + 1u);
        }
    }
    return Signature;
}

void SPCAPOperatorConsole::RebuildHealthStrip()
{
    if (!HealthStripBox.IsValid()) return;

    UMocapDatabase* DB = GetDB();
    const FShot* Shot = DB ? DB->GetShot(SelProduction, SelDay, SelSession, SelShot) : nullptr;

    // Rollups across the shot's *called* talent — uncalled subjects cannot block a record,
    // so counting them would make a ready shot look short.
    int32 BodyOK = 0, BodyTotal = 0, FaceOK = 0, FaceTotal = 0, AudioOK = 0, AudioTotal = 0;
    EStreamStatus BodyWorst = EStreamStatus::Connected;
    EStreamStatus FaceWorst = EStreamStatus::Connected;
    EStreamStatus AudioWorst = EStreamStatus::Connected;

    auto Accumulate = [](EStreamStatus Status, int32& NumOK, int32& Total, EStreamStatus& Worst)
    {
        ++Total;
        if (Status == EStreamStatus::Connected) { ++NumOK; }
        if (StreamSeverity(Status) > StreamSeverity(Worst)) { Worst = Status; }
    };

    if (Shot)
    {
        for (const FShotSubject& Subj : Shot->Subjects)
        {
            if (!Subj.bIsActive) continue;
            if (Subj.bHasBodyStream) Accumulate(Subj.BodyStream.StreamStatus, BodyOK, BodyTotal, BodyWorst);
            if (Subj.bHasFaceStream) Accumulate(Subj.FaceStream.StreamStatus, FaceOK, FaceTotal, FaceWorst);
            for (const FAudioStreamEntry& Audio : Subj.AudioStreams) Accumulate(Audio.StreamStatus, AudioOK, AudioTotal, AudioWorst);
        }
    }

    // VCam is stage-level, not per-subject: shown when the active stage records one, grey
    // when it does not.
    int32 VCamOK = 0, VCamTotal = 0;
    EStreamStatus VCamWorst = EStreamStatus::Connected;
    if (DB)
    {
        if (UStageConfigAsset* Stage = DB->GetActiveStageConfig())
        {
            if (Stage->VCamSystem != EVCamSystem::None && GEngine)
            {
                if (UPCAPVCamSubsystem* VCam = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
                {
                    Accumulate(VCam->GetStreamStatus(), VCamOK, VCamTotal, VCamWorst);
                }
            }
        }
    }

    // "Body ● 2/2" — the dot is the worst status, the count is connected / carried.
    auto MakeCell = [this](const FText& Label, int32 NumOK, int32 Total, EStreamStatus Worst) -> TSharedRef<SWidget>
    {
        const bool bCarried = Total > 0;
        const FString Counts = bCarried ? FString::Printf(TEXT("%d/%d"), NumOK, Total) : FString(TEXT("—"));
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ MakeStreamDot(Label, bCarried, Worst, FText::GetEmpty()) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(FText::FromString(Counts)).ColorAndOpacity(FSlateColor(bCarried ? ColText2 : ColInactive)) ];
    };

    HealthStripBox->SetContent(
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(Shot ? Shot->ShotID : FString(TEXT("(no shot)"))))
          .ColorAndOpacity(FSlateColor(Shot ? ColText2 : ColInactive)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
        [ MakeCell(LOCTEXT("StripBody",  "Body"),  BodyOK,  BodyTotal,  BodyWorst) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
        [ MakeCell(LOCTEXT("StripFace",  "Face"),  FaceOK,  FaceTotal,  FaceWorst) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
        [ MakeCell(LOCTEXT("StripAudio", "Audio"), AudioOK, AudioTotal, AudioWorst) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ MakeCell(LOCTEXT("StripVCam",  "VCam"),  VCamOK,  VCamTotal,  VCamWorst) ]);
}

// ── Post-take review (REVIEWING) ─────────────────────────────────────────────

FTake* SPCAPOperatorConsole::FindReviewTake(const FString& TakeID) const
{
    if (TakeID.IsEmpty()) return nullptr;

    UMocapDatabase* DB = GetDB();
    if (!DB) return nullptr;

    // The selected shot first — navigation is locked through REVIEWING, so the take that
    // just finished is almost always right here.
    if (FShot* Shot = DB->GetShot(SelProduction, SelDay, SelSession, SelShot))
    {
        for (FTake& Take : Shot->Takes)
        {
            if (Take.TakeID == TakeID) return &Take;
        }
    }

    // A take PCAPTool only observed is attributed to whatever was active when it started,
    // which need not be what the console shows now. Walk the hierarchy rather than calling
    // UMocapDatabase::GetTake(), which omits the session and resolves the wrong take once
    // two sessions share a shot slot.
    for (FProduction& Production : DB->Productions)
        for (FShootDay& Day : Production.Days)
            for (FSession& Session : Day.Sessions)
                for (FShot& Shot : Session.Shots)
                    for (FTake& Take : Shot.Takes)
                        if (Take.TakeID == TakeID) return &Take;

    return nullptr;
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildReviewCard(const FShot& Shot)
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    const FString TakeID = Rec ? Rec->GetLastHarvestedTakeID() : FString();

    // A different take to review: default the card to Best and clear the last take's notes.
    // Rebuilds of the *same* review keep the draft, so a health poll cannot wipe what the
    // director is halfway through saying.
    if (TakeID != ReviewTakeID)
    {
        ReviewTakeID = TakeID;
        ReviewLabel  = ETakeLabel::Best;
        ReviewDirectorNotes.Reset();
        ReviewCommentatorNotes.Reset();
    }

    const FTake* Take = FindReviewTake(TakeID);

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   %s"), *Shot.ShotID, *Shot.Description))).ColorAndOpacity(FSlateColor(ColGreen)) ];

    if (!Take)
    {
        // Harvest can legitimately produce nothing — an observed take with no active shot to
        // attribute it to, for one. Say so, and still offer the way back to READY.
        Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("ReviewNoTake", "The take that just finished is not in the database, so there is nothing to label. Done returns the console to READY."))
          .ColorAndOpacity(FSlateColor(ColAmber)) ];
    }
    else
    {
        Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::Format(LOCTEXT("ReviewJustShot", "{0} just recorded."), FText::FromString(TakeID))) ];

        // Label chips — same shape the Take Browser uses, but drafted here and committed by
        // Done in one write, so one label change is one re-publish.
        Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("ReviewLabelHeading", "Label")).ColorAndOpacity(FSlateColor(ColLabel)) ];

        TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
        if (const UEnum* LabelEnum = StaticEnum<ETakeLabel>())
        {
            for (int32 Index = 0; Index < LabelEnum->NumEnums() - 1; ++Index)   // skip the implicit _MAX
            {
                const ETakeLabel Value = (ETakeLabel)LabelEnum->GetValueByIndex(Index);
                const FString ChipText = FString::Printf(TEXT("%s %s"),
                    (ReviewLabel == Value) ? TEXT("●") : TEXT("○"),
                    *LabelEnum->GetDisplayNameTextByIndex(Index).ToString());

                Chips->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                    .ToolTipText(Value == ETakeLabel::Burn
                        ? LOCTEXT("ReviewBurnTip", "Burn — archived raw. A Burn take never enters the processing queue.")
                        : (Value == ETakeLabel::Captured
                            ? LOCTEXT("ReviewCapturedTip", "Captured — the recorder's default. Captured takes do not enter the processing queue.")
                            : LOCTEXT("ReviewQueueableTip", "Best and Alt takes are the ones the Take Browser's \"Process All Queued\" picks up.")))
                    .OnClicked_Lambda([this, Value]()
                    {
                        ReviewLabel = Value;
                        RebuildContext();   // redraw the chips; the notes draft survives it
                        return FReply::Handled();
                    })
                    [ SNew(STextBlock).Text(FText::FromString(ChipText))
                      .ColorAndOpacity(FSlateColor((ReviewLabel == Value) ? ColGreen : ColText2)) ]
                ];
            }
        }
        Box->AddSlot().AutoHeight()[ Chips ];

        Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("ReviewDirector", "Director notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Box->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(ReviewDirectorNotes))
          .OnTextChanged_Lambda([this](const FText& T) { ReviewDirectorNotes = T.ToString(); }) ];

        Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("ReviewCommentator", "Commentator notes")).ColorAndOpacity(FSlateColor(ColLabel)) ];
        Box->AddSlot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(ReviewCommentatorNotes))
          .OnTextChanged_Lambda([this](const FText& T) { ReviewCommentatorNotes = T.ToString(); }) ];
    }

    Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
    [
        SNew(SButton).Text(LOCTEXT("ReviewDone", "Done — back to READY"))
        .ToolTipText(LOCTEXT("ReviewDoneTip", "Write the label and notes onto the take, re-publish its record so the label reaches the Mocap Manager's take status, and unlock navigation."))
        .OnClicked(this, &SPCAPOperatorConsole::OnFinishReviewClicked)
    ];

    return Box;
}

FReply SPCAPOperatorConsole::OnFinishReviewClicked()
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    if (!Rec) return FReply::Handled();

    const FString TakeID = ReviewTakeID;
    if (FTake* Take = FindReviewTake(TakeID))
    {
        Take->Label            = ReviewLabel;
        Take->DirectorNotes    = ReviewDirectorNotes;
        Take->CommentatorNotes = ReviewCommentatorNotes;
        if (UMocapDatabase* DB = GetDB()) DB->MarkPackageDirty();
        SaveDB();

        NotifyOperator(FText::Format(LOCTEXT("ReviewSaved", "{0} labelled {1}."),
            FText::FromString(TakeID),
            StaticEnum<ETakeLabel>()->GetDisplayNameTextByValue((int64)ReviewLabel)));
    }

    // FinishReview() re-publishes the take's FPCapTakeRecord row — the row written at harvest
    // still carries the default Captured, so the label just set only reaches Epic's TakeStatus
    // through here — and returns the state to READY, which unlocks navigation.
    Rec->FinishReview();

    ReviewTakeID.Reset();
    ReviewLabel = ETakeLabel::Best;
    ReviewDirectorNotes.Reset();
    ReviewCommentatorNotes.Reset();

    LastRecordState = (uint8)Rec->GetRecordState();   // already handled here; don't rebuild again on the next poll
    RebuildHeader();
    RefreshShotRows();   // the take that just landed changes the count and the coverage glyph
    RebuildContext();
    RebuildHealthStrip();
    return FReply::Handled();
}

// ── Gating ───────────────────────────────────────────────────────────────────

bool SPCAPOperatorConsole::IsNavigationLocked() const
{
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        const EPCAPRecordState State = Rec->GetRecordState();
        return State == EPCAPRecordState::Capturing || State == EPCAPRecordState::Reviewing;
    }
    return false;
}

FText SPCAPOperatorConsole::NavLockTip() const
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    if (!Rec) return FText::GetEmpty();

    switch (Rec->GetRecordState())
    {
        case EPCAPRecordState::Capturing:
            return LOCTEXT("NavLockedCapturing", "Locked while a take is recording — moving now would point the next take at a different shot.");
        case EPCAPRecordState::Reviewing:
            return LOCTEXT("NavLockedReviewing", "Locked until the take is reviewed — label it on the right and the console returns to READY.");
        default:
            return FText::GetEmpty();
    }
}

bool SPCAPOperatorConsole::CanRecordNow(FText& OutReason) const
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    if (!Rec)
    {
        OutReason = LOCTEXT("RecordNoController", "The record controller is unavailable — RECORD cannot start a take.");
        return false;
    }

    switch (Rec->GetRecordState())
    {
        case EPCAPRecordState::Capturing:
            OutReason = LOCTEXT("RecordAlready", "A take is already recording.");
            return false;
        case EPCAPRecordState::Reviewing:
            OutReason = LOCTEXT("RecordReviewing", "Label the take that just finished first — the review card returns the console to READY.");
            return false;
        default:
            break;
    }

    // One transport, one controller. Name who has it rather than letting the press fail into
    // the log. Wording deliberately matches StartRecordForActiveShot's own refusal so the
    // button and the Output Log never say two different things.
    if (Rec->ShouldDeferToMocapManager())
    {
        OutReason = LOCTEXT("RecordDeferred", "The Mocap Manager is driving Take Recorder — PCAPTool is deferring to it. Record from the Mocap Manager; the take is still harvested and published here.");
        return false;
    }

    // The same check the record itself runs, so the disabled button carries the reason the
    // press would have logged: "kevinDorman face stream not connected", "kevinDorman audio
    // 'lav' not connected". Resolves against the DB's active shot, which RebuildContext has
    // just synced from the console's selection — the same contract the Send gate uses.
    FString StreamError;
    if (!Rec->AreActiveStreamsReady(StreamError))
    {
        OutReason = FText::FromString(StreamError);
        return false;
    }

    return true;
}

void SPCAPOperatorConsole::SaveDB()
{
    // Take labels and notes live in the master DB asset, and DB edits only MarkPackageDirty —
    // without this a day of Best/Alt/Burn decisions would survive only until the editor
    // closes. Same guard as the Call Sheet and the Take Browser.
    UMocapDatabase* DB = GetDB();
    if (!DB) return;

    UPackage* Pkg = DB->GetPackage();
    if (Pkg && Pkg->IsDirty())
    {
        FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

// ── Record actions ───────────────────────────────────────────────────────

FReply SPCAPOperatorConsole::OnRecordClicked()
{
    PushSelectionToDB();

    // The button is disabled in every blocked state; this only fires if a stream (or the
    // transport) changed under a stale rebuild. Say so rather than log-and-no-op — an
    // operator who presses RECORD and gets nothing has no reason to look in the Output Log.
    FText BlockedReason;
    if (!CanRecordNow(BlockedReason))
    {
        NotifyOperator(BlockedReason);
        RebuildContext();   // re-gate against what is true now
        return FReply::Handled();
    }

    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        FString Error;
        if (!Rec->StartRecordForActiveShot(Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCAP] RECORD blocked: %s"), *Error);
            NotifyOperator(FText::FromString(Error));
            RebuildContext();
        }
    }
    return FReply::Handled();
}

FReply SPCAPOperatorConsole::OnStopClicked()
{
    UPCAPTakeRecorderSubsystem* Rec = GetRecorder();
    if (!Rec) return FReply::Handled();

    // StopRecord() refuses a take PCAPTool did not start — correct, and the Mocap Manager
    // operator keeps their transport — but it refuses into the Output Log. STOP is the most
    // safety-critical button on a shoot day; it never presses to nothing.
    if (Rec->IsObservingExternalRecord())
    {
        const FString Owner = Rec->GetTransportOwner();
        NotifyOperator(FText::Format(
            LOCTEXT("StopForeignToast", "{0} started this take — PCAPTool cannot stop it. Stop it there; the take is still harvested and published here."),
            FText::FromString(Owner.IsEmpty() ? TEXT("The Mocap Manager") : Owner)));
        return FReply::Handled();
    }

    Rec->StopRecord();
    return FReply::Handled();
}

FReply SPCAPOperatorConsole::OnNextTakeClicked()
{
    PushSelectionToDB();

    FText BlockedReason;
    if (!CanRecordNow(BlockedReason))
    {
        NotifyOperator(BlockedReason);
        RebuildContext();
        return FReply::Handled();
    }

    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        FString Error;
        if (!Rec->RecordNextTake(Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCAP] Next take blocked: %s"), *Error);
            NotifyOperator(FText::FromString(Error));
            RebuildContext();
        }
    }
    return FReply::Handled();
}

// ── Mocap Manager — stage the called shot ────────────────────────────────────
//
// PCAPTool's database stays the source of truth for who/what is called; this is
// the call site that projects it onto Epic's Performance Capture actors so the
// engine's Mocap Manager can record them (see the 2026-06-29 integration spec).

FString SPCAPOperatorConsole::CurrentShotKey() const
{
    return FString::Printf(TEXT("%s|%s|%s|%s"), *SelProduction, *SelDay, *SelSession, *SelShot);
}

void SPCAPOperatorConsole::NotifyOperator(const FText& Message)
{
    FNotificationInfo Info(Message);
    Info.ExpireDuration = 4.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

TArray<UPropRosterEntry*> SPCAPOperatorConsole::GatherPropRoster() const
{
    TArray<UPropRosterEntry*> Out;
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> F;
    ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), F, false);
    for (const FAssetData& AD : F) if (UPropRosterEntry* E = Cast<UPropRosterEntry>(AD.GetAsset())) Out.Add(E);
    return Out;
}

bool SPCAPOperatorConsole::CanSendShotToMocapManager(FText& OutReason) const
{
    UMocapDatabase* DB = GetDB();
    const FShot* Shot = DB ? DB->GetActiveShot() : nullptr;
    if (!Shot)
    {
        OutReason = LOCTEXT("SendNoShot", "No active shot — pick one from the shot list first.");
        return false;
    }

    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        OutReason = LOCTEXT("SendNoWorld", "No editor world — open a level before staging a shot.");
        return false;
    }

    // The bridge places one performer per *called* (bIsActive) subject and one actor
    // per listed prop. A shot whose talent are all toggled off would spawn nothing,
    // so gate on the called count rather than the roster count.
    bool bAnyCalledTalent = false;
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (Subj.bIsActive) { bAnyCalledTalent = true; break; }
    }

    if (!bAnyCalledTalent && Shot->Props.Num() == 0)
    {
        OutReason = Shot->Subjects.Num() > 0
            // Name a control that actually exists. The Call Sheet's shot rows are a
            // read-only summary — there is no per-shot toggle to send anyone to. Talent
            // reaches a shot from the day's call-out, so that is where to point.
            ? LOCTEXT("SendNoneCalled", "This shot's talent are all toggled off. Call them on the day in the Call Sheet, then re-add the shot — shots inherit the day's call-out when they're created.")
            : LOCTEXT("SendNothingCalled", "Nothing called to this shot. Call actors and props for the day in the Call Sheet, then add the shot — it inherits the day's call-out.");
        return false;
    }

    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        if (Rec->GetRecordState() == EPCAPRecordState::Capturing)
        {
            OutReason = LOCTEXT("SendWhileCapturing", "Recording — stop the take before staging actors into the level.");
            return false;
        }
    }

    return true;
}

FReply SPCAPOperatorConsole::OnSendShotToMocapManagerClicked()
{
    PushSelectionToDB();   // GetActiveShot() resolves against the DB's Active* fields

    FText BlockedReason;
    if (!CanSendShotToMocapManager(BlockedReason))
    {
        // The button is disabled in every one of these states; this only fires if the
        // world/selection changed under a stale rebuild. Say so rather than no-op.
        NotifyOperator(BlockedReason);
        return FReply::Handled();
    }

    UMocapDatabase* DB = GetDB();
    FShot* Shot        = DB ? DB->GetActiveShot() : nullptr;
    UWorld* World      = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!Shot || !World) return FReply::Handled();   // re-checked above — keeps the deref honest

    const FString ShotID     = Shot->ShotID;
    const FString ShotKey    = CurrentShotKey();
    const bool bAlreadySent  = SentShotKeys.Contains(ShotKey);

    TArray<AActor*> Spawned;
    int32 NumSpawned = 0;
    {
        // One transaction for the whole shot, so a mis-fire is a single Ctrl-Z.
        const FScopedTransaction Transaction(LOCTEXT("SendShotTransaction", "Send Shot to Mocap Manager"));

        NumSpawned = UPCAPMocapBridge::SpawnShotToStage(World, *Shot, GatherPropRoster(), Spawned);

        // Leave the operator holding exactly what landed — obvious in the outliner,
        // and one Delete away if they'd rather not undo.
        if (Spawned.Num() > 0)
        {
            GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
            for (AActor* Actor : Spawned)
            {
                GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/true);
            }
        }
    }

    if (NumSpawned <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Send to Mocap Manager: shot '%s' spawned nothing."), *ShotID);
        NotifyOperator(FText::Format(
            LOCTEXT("SendShotNone", "{0}: nothing was placed. Check the Output Log — the Performance Capture actors could not be spawned."),
            FText::FromString(ShotID)));
        return FReply::Handled();
    }

    SentShotKeys.Add(ShotKey);

    UE_LOG(LogTemp, Log, TEXT("[PCAP] Sent shot '%s' to the Mocap Manager — %d actor(s) spawned."), *ShotID, NumSpawned);
    NotifyOperator(bAlreadySent
        ? FText::Format(
            LOCTEXT("SendShotAgainDone", "{0} re-sent — {1} more actors staged. This shot was already sent this session, so the level now holds duplicates; Ctrl-Z undoes."),
            FText::FromString(ShotID), FText::AsNumber(NumSpawned))
        : FText::Format(
            LOCTEXT("SendShotDone", "{0} sent to the Mocap Manager — {1} actors staged. Ctrl-Z undoes."),
            FText::FromString(ShotID), FText::AsNumber(NumSpawned)));

    RebuildContext();   // the button relabels to "Re-send…"
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
