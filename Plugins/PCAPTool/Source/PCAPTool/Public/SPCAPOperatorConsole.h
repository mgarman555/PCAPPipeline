#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UMocapDatabase;
class UPCAPTakeRecorderSubsystem;
class SBox;

/**
 * Operator Console — the unified solo-operator surface (Tool 4). One screen to
 * navigate shots, see the active shot's full context (talent + streams + takes),
 * and run takes. Reads the UMocapDatabase hierarchy, drives the Phase 2 record
 * backend (UPCAPTakeRecorderSubsystem), and reflects READY/CAPTURING/REVIEWING.
 *
 * Layout: header (Production/Day/Session pickers + record state + STOP) /
 * left shot-list spine (status + take counts) / right shot context (talent,
 * takes, RECORD + Next Take).
 */
class PCAPTOOL_API SPCAPOperatorConsole : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPOperatorConsole) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // Current navigation (drives the DB's active selection at record time).
    FString SelProduction;   // ProjectCode
    FString SelDay;          // DayID
    FString SelSession;      // SessionID
    FString SelShot;         // ShotID

    uint8 LastRecordState = 255;   // for the poll-driven header refresh

    TSharedPtr<SBox> HeaderBox;
    TSharedPtr<SBox> ShotContextBox;
    TSharedPtr<SListView<TSharedPtr<FString>>> ShotListView;
    TArray<TSharedPtr<FString>> ShotItems;   // ShotIDs of the active session

    UMocapDatabase* GetDB() const;
    UPCAPTakeRecorderSubsystem* GetRecorder() const;

    void RebuildHeader();
    void RebuildShotList();
    void RebuildContext();

    // Header pickers (SComboButton menus).
    TSharedRef<SWidget> BuildProductionPicker();
    TSharedRef<SWidget> BuildDayPicker();
    TSharedRef<SWidget> BuildSessionPicker();

    // Shot list.
    TSharedRef<class ITableRow> OnGenerateShotRow(TSharedPtr<FString> ShotID, const TSharedRef<class STableViewBase>& Owner);
    void OnShotSelected(TSharedPtr<FString> ShotID, ESelectInfo::Type);

    // Record.
    void PushSelectionToDB();      // copies Sel* into the DB's Active* fields
    FReply OnRecordClicked();
    FReply OnStopClicked();
    FReply OnNextTakeClicked();

    EActiveTimerReturnType PollRecordState(double InCurrentTime, float InDeltaTime);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FLinearColor ColRed   = FLinearColor(0.878f, 0.251f, 0.251f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
    const FLinearColor ColText3 = FLinearColor(0.290f, 0.345f, 0.314f);
};
