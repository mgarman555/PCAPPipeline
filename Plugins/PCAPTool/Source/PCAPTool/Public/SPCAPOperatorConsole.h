#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "PCAPToolTypes.h"   // ETakeLabel / EStreamStatus / FShot — the review card, the health strip and the talent rows read them

class UMocapDatabase;
class UPCAPTakeRecorderSubsystem;
class UPropRosterEntry;
class SBox;

/**
 * Operator Console — the unified solo-operator surface (Tool 4). One screen to
 * navigate shots, see the active shot's full context (talent + streams + takes),
 * and run takes. Reads the UMocapDatabase hierarchy, drives the Phase 2 record
 * backend (UPCAPTakeRecorderSubsystem), and reflects READY/CAPTURING/REVIEWING.
 *
 * Layout: header (Production/Day/Session pickers + transport/deferral readout +
 * record state + STOP) / left shot-list spine (status + take counts) / right shot
 * context (talent + streams + driven targets, props, takes, RECORD + Next Take +
 * Send to Mocap Manager) / bottom operator strip (Body · Face · Audio · VCam).
 *
 * REVIEWING replaces the context pane with the post-take card (label + director /
 * commentator notes); its Done is the only non-error way back to READY, and
 * navigation is locked until it is pressed.
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

    // Fingerprint of everything the operator strip and the RECORD gate draw from. Stream
    // health moves without the record state moving — a lav drops during setup — so the
    // same poll follows it, but only redraws when this actually changes rather than
    // rebuilding the context four times a second.
    uint32 LastHealthSignature = 0;

    // Post-take review draft (REVIEWING). Held on the console, not written straight to the
    // FTake, so a rebuild between keystrokes never loses what the director just said; Done
    // commits all three at once and re-publishes the take record.
    FString ReviewTakeID;                        // the take the card is labelling
    ETakeLabel ReviewLabel = ETakeLabel::Best;   // Best preselected, per the comfort design note
    FString ReviewDirectorNotes;
    FString ReviewCommentatorNotes;

    // Shots already sent to the Mocap Manager this editor session (key = CurrentShotKey()).
    // Only drives the button's wording/tooltip — a re-send is allowed, but never silent.
    TSet<FString> SentShotKeys;

    TSharedPtr<SBox> HeaderBox;
    TSharedPtr<SBox> ShotContextBox;
    TSharedPtr<SBox> HealthStripBox;
    TSharedPtr<SListView<TSharedPtr<FString>>> ShotListView;
    TArray<TSharedPtr<FString>> ShotItems;   // ShotIDs of the active session

    UMocapDatabase* GetDB() const;
    UPCAPTakeRecorderSubsystem* GetRecorder() const;

    void RebuildHeader();
    void RebuildShotList();
    void RebuildContext();
    void RebuildHealthStrip();

    // Header pickers (SComboButton menus).
    TSharedRef<SWidget> BuildProductionPicker();
    TSharedRef<SWidget> BuildDayPicker();
    TSharedRef<SWidget> BuildSessionPicker();

    // Which controller owns the single Take Recorder transport, and what PCAPTool does when
    // the Mocap Manager has it (UE 5.8 deferral policy). Empty note = nothing to say.
    TSharedRef<SWidget> BuildDeferralPicker();
    FText TransportNoteText() const;

    // Shot list.
    TSharedRef<class ITableRow> OnGenerateShotRow(TSharedPtr<FString> ShotID, const TSharedRef<class STableViewBase>& Owner);
    void OnShotSelected(TSharedPtr<FString> ShotID, ESelectInfo::Type);

    // Re-generate the spine's rows in place — the take count and the ○/✓/★ coverage glyph
    // are computed per row, so a finished take leaves them stale until this runs.
    void RefreshShotRows();

    // Shot context — one talent row (streams + driven target), one prop row.
    TSharedRef<SWidget> BuildSubjectRow(const FShotSubject& Subj, bool bNavLocked);
    TSharedRef<SWidget> BuildPropRow(const FPropEntry& Prop) const;

    // "body ●" — one labelled stream dot. Grey label + grey dot when the shot does not
    // carry that stream at all, so "not called" never reads as "down".
    TSharedRef<SWidget> MakeStreamDot(const FText& Label, bool bCarried, EStreamStatus Status, const FText& ToolTip) const;

    // The selected shot's subject with this ActorID, re-resolved on every use — FShotSubject
    // lives by value inside FShot::Subjects, so a held pointer dangles on any Add.
    FShotSubject* FindSelectedSubject(const FString& ActorID) const;

    // Record.
    void PushSelectionToDB();      // copies Sel* into the DB's Active* fields
    FReply OnRecordClicked();
    FReply OnStopClicked();
    FReply OnNextTakeClicked();

    // Is RECORD / Next take available? False + OutReason with the operator-facing
    // explanation (used verbatim as the disabled button's tooltip). OutReason is left
    // untouched when the record is available.
    bool CanRecordNow(FText& OutReason) const;

    // READY lets the operator move between shots; CAPTURING and REVIEWING do not.
    bool IsNavigationLocked() const;

    // What a locked navigation control says on hover. Empty while nothing is locked.
    FText NavLockTip() const;

    // Post-take review card (REVIEWING) — label chips + director / commentator notes.
    TSharedRef<SWidget> BuildReviewCard(const FShot& Shot);
    FTake* FindReviewTake(const FString& TakeID) const;
    FReply OnFinishReviewClicked();

    // Health rollup fingerprint for the poll (see LastHealthSignature).
    uint32 ComputeHealthSignature() const;

    // Take labels/notes and DrivenTarget live in the master DB asset, and DB edits only
    // MarkPackageDirty — same save guard the Call Sheet and the Take Browser use.
    void SaveDB();

    // Mocap Manager (UE 5.8 Performance Capture) — project the called shot onto
    // Epic's ACapturePerformer / prop actors via UPCAPMocapBridge::SpawnShotToStage.
    FReply OnSendShotToMocapManagerClicked();

    // Is the send available? Returns false and fills OutReason with the operator-facing
    // explanation (used verbatim as the disabled button's tooltip). OutReason is left
    // untouched when the send is available.
    bool CanSendShotToMocapManager(FText& OutReason) const;

    // Every UPropRosterEntry asset — SpawnShotToStage matches called props to their
    // permanent record by PropID. Same Asset Registry enumeration the Call Sheet uses.
    TArray<UPropRosterEntry*> GatherPropRoster() const;

    // Identity of the current selection, for the already-sent bookkeeping.
    FString CurrentShotKey() const;

    // Editor toast — the plugin's standard operator feedback (see SPCAPCallSheetPanel).
    static void NotifyOperator(const FText& Message);

    EActiveTimerReturnType PollRecordState(double InCurrentTime, float InDeltaTime);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FLinearColor ColRed   = FLinearColor(0.878f, 0.251f, 0.251f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
    const FLinearColor ColText3 = FLinearColor(0.290f, 0.345f, 0.314f);
    // --inactive from the handoff's palette: "this shot does not carry that stream" is its
    // own state, distinct from text and from a red disconnect.
    const FLinearColor ColInactive = FLinearColor(0.227f, 0.267f, 0.251f);
};
