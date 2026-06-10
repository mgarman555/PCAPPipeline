#include "SPCAPOperatorConsole.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPTakeRecorderSubsystem.h"

#include "Engine/Engine.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

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
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(0.6f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(ShotContextBox, SBox) ]
                ]
            ]
        ]
    ];

    RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SPCAPOperatorConsole::PollRecordState));

    RebuildHeader();
    RebuildShotList();
    RebuildContext();
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
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        const uint8 State = (uint8)Rec->GetRecordState();
        if (State != LastRecordState)
        {
            LastRecordState = State;
            RebuildHeader();
            RebuildContext();
        }
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

TSharedRef<SWidget> SPCAPOperatorConsole::BuildProductionPicker()
{
    const FString Label = SelProduction.IsEmpty() ? TEXT("(production)") : SelProduction;
    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
                for (const FProduction& P : DB->Productions)
                {
                    const FString Code = P.ProjectCode;
                    MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s — %s"), *Code, *P.ProductionName)), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Code]() { SelProduction = Code; SelDay.Empty(); SelSession.Empty(); SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); })));
                }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildDayPicker()
{
    const FString Label = SelDay.IsEmpty() ? TEXT("(day)") : (TEXT("Day_") + SelDay);
    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
                if (FProduction* P = DB->GetProductionByCode(SelProduction))
                    for (const FShootDay& D : P->Days)
                    {
                        const FString Day = D.DayID;
                        MB.AddMenuEntry(FText::FromString(TEXT("Day_") + Day), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Day]() { SelDay = Day; SelSession.Empty(); SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); })));
                    }
            return MB.MakeWidget();
        });
}

TSharedRef<SWidget> SPCAPOperatorConsole::BuildSessionPicker()
{
    const FString Label = SelSession.IsEmpty() ? TEXT("(session)") : (TEXT("Session_") + SelSession);
    return SNew(SComboButton).ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Label)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* DB = GetDB())
                if (FShootDay* D = DB->GetDay(SelProduction, SelDay))
                    for (const FSession& S : D->Sessions)
                    {
                        const FString Sess = S.SessionID;
                        MB.AddMenuEntry(FText::FromString(TEXT("Session_") + Sess), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Sess]() { SelSession = Sess; SelShot.Empty(); RebuildHeader(); RebuildShotList(); RebuildContext(); })));
                    }
            return MB.MakeWidget();
        });
}

void SPCAPOperatorConsole::RebuildHeader()
{
    if (!HeaderBox.IsValid()) return;

    EPCAPRecordState State = EPCAPRecordState::Ready;
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder()) State = Rec->GetRecordState();

    FText StateText = LOCTEXT("Ready", "READY");
    FLinearColor StateColor = ColText2;
    if (State == EPCAPRecordState::Capturing) { StateText = LOCTEXT("Capturing", "CAPTURING"); StateColor = ColGreen; }
    else if (State == EPCAPRecordState::Reviewing) { StateText = LOCTEXT("Reviewing", "REVIEWING"); StateColor = ColAmber; }

    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildProductionPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildDayPicker() ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f) [ BuildSessionPicker() ]
        + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
          [ SNew(STextBlock).Text(StateText).ColorAndOpacity(FSlateColor(StateColor)) ];

    if (State == EPCAPRecordState::Capturing)
    {
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("Stop", "STOP")).OnClicked(this, &SPCAPOperatorConsole::OnStopClicked) ];
    }

    HeaderBox->SetContent(Row);
}

// ── Shot list ────────────────────────────────────────────────────────────

void SPCAPOperatorConsole::RebuildShotList()
{
    ShotItems.Reset();
    if (UMocapDatabase* DB = GetDB())
        if (FSession* Session = DB->GetSession(SelProduction, SelDay, SelSession))
            for (const FShot& S : Session->Shots)
                ShotItems.Add(MakeShared<FString>(S.ShotID));

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
    SelShot = ShotID.IsValid() ? *ShotID : FString();
    RebuildContext();
}

// ── Context ────────────────────────────────────────────────────────────────

void SPCAPOperatorConsole::RebuildContext()
{
    if (!ShotContextBox.IsValid()) return;

    UMocapDatabase* DB = GetDB();
    FShot* Shot = DB ? DB->GetShot(SelProduction, SelDay, SelSession, SelShot) : nullptr;
    if (!Shot)
    {
        ShotContextBox->SetContent(SNew(STextBlock).Text(LOCTEXT("PickShot", "Select a shot from the list.")).ColorAndOpacity(FSlateColor(ColText2)));
        return;
    }

    EPCAPRecordState State = EPCAPRecordState::Ready;
    FString NextTakeID;
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder()) State = Rec->GetRecordState();
    if (DB) { DB->ActiveProductionCode = SelProduction; DB->ActiveDayID = SelDay; DB->ActiveSessionID = SelSession; DB->ActiveShotID = SelShot; NextTakeID = DB->BuildNextTakeID(); }

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   %s"), *Shot->ShotID, *Shot->Description))).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("Talent", "Talent")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    for (const FShotSubject& Subj : Shot->Subjects)
    {
        TSharedRef<SHorizontalBox> SubRow = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Subj.ActorID)).ColorAndOpacity(FSlateColor(Subj.bIsActive ? FLinearColor::White : ColText3)) ];
        if (Subj.bHasBodyStream)
            SubRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)[ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(FSlateColor(StreamColor(Subj.BodyStream.StreamStatus))) ];
        if (Subj.bHasFaceStream)
            SubRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)[ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(FSlateColor(StreamColor(Subj.FaceStream.StreamStatus))) ];
        Box->AddSlot().AutoHeight().Padding(0.f, 2.f)[ SubRow ];
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

    // Record controls
    Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(2.f).Padding(0.f, 0.f, 6.f, 0.f)
        [
            (State == EPCAPRecordState::Capturing)
            ? StaticCastSharedRef<SWidget>(SNew(SButton).Text(LOCTEXT("Stop2", "STOP")).OnClicked(this, &SPCAPOperatorConsole::OnStopClicked))
            : StaticCastSharedRef<SWidget>(SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("RECORD · %s"), *NextTakeID))).OnClicked(this, &SPCAPOperatorConsole::OnRecordClicked))
        ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ SNew(SButton).Text(LOCTEXT("NextTake", "Next take")).IsEnabled(State != EPCAPRecordState::Capturing).OnClicked(this, &SPCAPOperatorConsole::OnNextTakeClicked) ]
    ];

    ShotContextBox->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[ Box ]);
}

// ── Record actions ───────────────────────────────────────────────────────

FReply SPCAPOperatorConsole::OnRecordClicked()
{
    PushSelectionToDB();
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        FString Error;
        if (!Rec->StartRecordForActiveShot(Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCAP] RECORD blocked: %s"), *Error);
        }
    }
    return FReply::Handled();
}

FReply SPCAPOperatorConsole::OnStopClicked()
{
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder()) Rec->StopRecord();
    return FReply::Handled();
}

FReply SPCAPOperatorConsole::OnNextTakeClicked()
{
    PushSelectionToDB();
    if (UPCAPTakeRecorderSubsystem* Rec = GetRecorder())
    {
        FString Error;
        if (!Rec->RecordNextTake(Error)) UE_LOG(LogTemp, Warning, TEXT("[PCAP] Next take blocked: %s"), *Error);
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
