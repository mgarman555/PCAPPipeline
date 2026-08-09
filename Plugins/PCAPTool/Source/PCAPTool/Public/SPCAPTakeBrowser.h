#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "PCAPToolTypes.h"   // ETakeLabel / EProcessingStatus / EShotType — carried on every row

class UMocapDatabase;
class UPCAPTakeRecorderSubsystem;
class UPCAPTakeProcessingQueue;
class STableViewBase;
class ITableRow;
class SBox;

/**
 * One take, flattened for the browser.
 *
 * FTake lives by value inside FShot::Takes, four arrays deep (Productions →
 * Days → Sessions → Shots → Takes), so any Add anywhere in that hierarchy —
 * the recorder appends one on every take — reallocates and dangles an FTake*.
 * Rows therefore carry the full five-part coordinate plus a display snapshot,
 * and every read-for-detail or write re-resolves the live take from that key.
 *
 * ProjectCode is only knowable during the hierarchy walk: FTake carries DayID,
 * SessionID, ShotID and TakeID, but no production field.
 */
struct FPCAPTakeBrowserRow
{
    // Identity — the full coordinate. UMocapDatabase::GetTake() omits SessionID and
    // scans every session in the day, so it resolves the wrong take once two sessions
    // share a shot slot (slot IDs are 3-digit and reused). This panel never uses it.
    FString ProjectCode;
    FString DayID;
    FString SessionID;
    FString ShotID;
    FString TakeID;

    // Display snapshot — refreshed wholesale by ReloadTakes(), never held as a pointer.
    FString SessionLabel;      // FSession::Label — the human-facing session name
    FString ShotDescription;
    ETakeLabel Label = ETakeLabel::Captured;
    EProcessingStatus Status = EProcessingStatus::Pending;
    EShotType ShotType = EShotType::Production;
    FDateTime RecordedAt = FDateTime(0);
    float DurationSeconds = 0.f;
    int32 NumSubjects = 0;
    int32 NumProps = 0;
};

/**
 * Take Browser — post-take management (Layer 5). One screen to find any take in
 * the database, read its full record-time manifest, label it, and see where it
 * stands in the processing queue.
 *
 * Layout: header (day / session / shot / label filters + search) / queue bar
 * ("Process All Queued", which states its payload before it runs) / left take
 * list (ID, label, recorded, duration, status) / right take detail (manifest +
 * per-step processing state).
 *
 * Honesty note, which the UI states as plainly as this comment does: the editor
 * performs none of the five processing steps. Body solve cleanup happens in
 * Shogun/Motive and HMC solve in MetaHuman Animator — Unreal cannot do either.
 * Body retarget, audio sync/trim and merge-to-sequencer are in-engine in
 * principle but nothing executes them today. Every step is therefore an
 * operator-attested transition, and "Process All Queued" builds a worklist; it
 * does not solve anything.
 */
class PCAPTOOL_API SPCAPTakeBrowser : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPTakeBrowser) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    using FTakeRowPtr = TSharedPtr<FPCAPTakeBrowserRow>;

    // ── Filters ───────────────────────────────────────────────────────────────
    // Day, session and shot are the locked filter axes. Production is not one, but
    // day IDs repeat across productions, so the day picker sets both and the pair
    // is what scopes everything downstream.
    FString SelProject;                  // set with SelDay; empty = all
    FString SelDay;                      // empty = all days
    FString SelSession;                  // empty = all sessions
    FString SelShot;                     // empty = all shots
    int32   SelLabel = INDEX_NONE;       // INDEX_NONE = all labels, else (int32)ETakeLabel
    FString FilterText;

    // ── Rows ──────────────────────────────────────────────────────────────────
    TArray<FTakeRowPtr> AllTakes;
    TArray<FTakeRowPtr> FilteredTakes;   // bound once to the view — never SetItemsSource
    FTakeRowPtr SelectedTake;

    // Roster display names, resolved once per reload. Never load an asset inside a
    // row generator — a production's take list grows without bound.
    TMap<FString, FString> ActorNameByID;
    TMap<FString, FString> PropNameByID;

    // Change guards for the poll. UMocapDatabase has no change delegate, so the panel
    // watches the recorder (a take just landed) and the queue's change serial (someone
    // else wrote queue or attestation state) and rebuilds only when one of them moves.
    uint8 LastRecordState  = 255;
    int32 LastQueueSerial  = -1;

    TSharedPtr<SBox> HeaderBox;
    TSharedPtr<SBox> QueueBarBox;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<SListView<FTakeRowPtr>> TakeListView;

    UMocapDatabase* GetDB() const;
    UPCAPTakeRecorderSubsystem* GetRecorder() const;
    UPCAPTakeProcessingQueue* GetQueue() const;

    void RebuildHeader();
    void RebuildQueueBar();
    void RebuildDetail();

    // Re-read everything and re-render the parts that depend on take state. Called after
    // every mutation and whenever a change guard moves; resyncs the queue serial so the
    // panel's own writes do not make the next poll rebuild a second time.
    void RefreshAll();

    void ReloadTakes();                  // re-walk the database into AllTakes
    void ApplyFilter();                  // refill FilteredTakes, refresh the view
    void SaveDB();                       // persist DB edits — MarkPackageDirty alone never reaches disk

    // Re-resolve the live take for a row. Always via GetShot() + a TakeID search over
    // its Takes, never via GetTake(), whose signature drops SessionID.
    FTake* ResolveTake(const FPCAPTakeBrowserRow& Row) const;

    // ── Header filters (SComboButton menus) ───────────────────────────────────
    TSharedRef<SWidget> BuildDayPicker();
    TSharedRef<SWidget> BuildSessionPicker();
    TSharedRef<SWidget> BuildShotPicker();
    TSharedRef<SWidget> BuildLabelPicker();
    void OnFilterChanged(const FText& Text);

    // ── Take list ─────────────────────────────────────────────────────────────
    // One layout function feeds both the column strip and every row, so the two
    // cannot drift apart. There is no SHeaderRow anywhere in this plugin.
    TSharedRef<SHorizontalBox> MakeTakeColumns(TSharedRef<SWidget> Lead,
                                               TSharedRef<SWidget> IdCell,
                                               TSharedRef<SWidget> LabelCell,
                                               TSharedRef<SWidget> RecordedCell,
                                               TSharedRef<SWidget> DurationCell,
                                               TSharedRef<SWidget> StatusCell) const;
    TSharedRef<SWidget> BuildColumnStrip();
    TSharedRef<ITableRow> OnGenerateTakeRow(FTakeRowPtr Item, const TSharedRef<STableViewBase>& Owner);
    void OnTakeSelected(FTakeRowPtr Item, ESelectInfo::Type);

    // ── Detail ────────────────────────────────────────────────────────────────
    TSharedRef<SWidget> BuildDetailFor(FTakeRowPtr Item);
    TSharedRef<SWidget> BuildLabelRow(const FPCAPTakeBrowserRow& Row, const FTake& Take);
    TSharedRef<SWidget> BuildManifestSection(const FTake& Take);
    TSharedRef<SWidget> BuildProcessingSection(const FPCAPTakeBrowserRow& Row, const FTake& Take);

    // ── Actions ───────────────────────────────────────────────────────────────
    void SetLabelForRow(const FPCAPTakeBrowserRow& Row, ETakeLabel NewLabel);

    // Is "Process All Queued" available? Fills OutSummary with what the click would do —
    // how many takes and which steps — and on a false return fills OutReason with the
    // operator-facing explanation, shown verbatim as visible text AND as the disabled
    // button's tooltip. The button is never a silent no-op.
    bool CanProcessAllQueued(FText& OutReason, FString& OutSummary) const;
    FReply OnProcessAllQueuedClicked();

    // ── Formatting / palette ──────────────────────────────────────────────────
    static FString FormatRecordedAt(const FDateTime& When);
    static FString FormatDuration(float Seconds);
    static FString LabelName(ETakeLabel Label);
    static FString StatusName(EProcessingStatus Status);
    FLinearColor LabelColor(ETakeLabel Label) const;
    FLinearColor StatusColor(EProcessingStatus Status) const;

    // Editor toast — the plugin's standard operator feedback (see SPCAPCallSheetPanel).
    static void NotifyOperator(const FText& Message);

    EActiveTimerReturnType PollRecordState(double InCurrentTime, float InDeltaTime);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColGold  = FLinearColor(0.878f, 0.741f, 0.278f);   // locked "Best" label colour
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FLinearColor ColRed   = FLinearColor(0.878f, 0.251f, 0.251f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
    const FLinearColor ColText3 = FLinearColor(0.290f, 0.345f, 0.314f);
};
