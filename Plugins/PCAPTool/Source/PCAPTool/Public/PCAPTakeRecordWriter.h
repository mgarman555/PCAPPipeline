#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPToolTypes.h"          // FTake / ETakeLabel
#include "PCAPTakeRecordWriter.generated.h"

class UDataTable;

// Which Mocap Manager session a take/slate write lands in. Epic's take and slate
// DataTables are owned *per session* (FPCapSessionRecord::TakesDataTable /
// ::SessionSlateTable), so every write resolves its session first — there is no
// project-wide "takes table" to fall back on. Returned by ResolveSessionBinding so
// call sites (and the log) can see exactly where a record went.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPSessionBinding
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid SessionUID;               // FPCapRecordBase.UID of the session row

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName SessionName;              // FPCapSessionRecord.SessionName — operator-facing

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TObjectPtr<UDataTable> TakesTable = nullptr;   // FPCapSessionRecord.TakesDataTable

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TObjectPtr<UDataTable> SlateTable = nullptr;   // FPCapSessionRecord.SessionSlateTable

    bool IsValid() const { return SessionUID.IsValid() && (TakesTable != nullptr || SlateTable != nullptr); }
};

// ---------------------------------------------------------------------------
// Write-back layer over Epic's Performance Capture record model.
//
// UPCAPMocapData reads Epic's data; this is the other half — it publishes a PCAPTool
// FTake as an FPCapTakeRecord row (plus the matching FPCapSlateRecord note) so takes
// recorded through PCAPTool appear in the Mocap Manager's own Review tab.
//
// The record structs live in the Workflow plugin's *private* module, so nothing here
// includes a PCap header: the row UScriptStructs are resolved by /Script path and
// their fields written by FProperty name (same pattern as UPCAPMocapData /
// UPCAPTakeRecorderSubsystem). The DataTables themselves are plain UDataTables
// (UPCapDataTable is a UDataTable subclass in the *editor* Workflow module, which this
// plugin does not link), so they are used through the stock engine API.
//
// Every entry point degrades to a logged no-op — plugin absent (WITH_PCAP_WORKFLOW=0),
// struct/field renamed, no resolvable session, or a table whose row struct does not
// match. Nothing here ever writes a row into a table of the wrong row struct.
//
// Writes are idempotent: the take row key is the take's canonical TakeID, so
// re-publishing a take (e.g. after the operator labels it) updates the row in place
// instead of duplicating it.
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPTakeRecordWriter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // True when the Workflow plugin's take-record struct resolves — i.e. write-back can run.
    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static bool IsAvailable();

    // Find-or-create the FPCapTakeRecord row for Take in its session's TakesDataTable and
    // populate it (RecordedTake / DateTimeCreated / TakeDurationSeconds / TakeStatus /
    // SessionUID). Row key = Take.TakeID, so a re-write updates rather than duplicates.
    UFUNCTION(BlueprintCallable, Category="PCAP|Take Records")
    static bool WriteTakeRecord(const FTake& Take);

    // Find-or-create the FPCapSlateRecord row for Take's shot and merge the take's notes
    // into its SlateNote. One slate row per shot: each take owns one "[TakeID] …" line, so
    // takes on the same slate never overwrite each other.
    UFUNCTION(BlueprintCallable, Category="PCAP|Take Records")
    static bool WriteSlateNote(const FTake& Take);

    // WriteTakeRecord + WriteSlateNote. Returns the take-row result (the slate note is
    // advisory — a failed note never fails the take).
    UFUNCTION(BlueprintCallable, Category="PCAP|Take Records")
    static bool WriteTake(const FTake& Take);

    // Resolve the Mocap Manager session that owns the take/slate tables for SessionID.
    // Match order: pinned UID → SessionName == SessionID (or "Session_<SessionID>") →
    // the single non-archived session in the project. Ambiguous = failure, never a guess.
    UFUNCTION(BlueprintCallable, Category="PCAP|Take Records")
    static bool ResolveSessionBinding(const FString& SessionID, FPCAPSessionBinding& OutBinding);

    // Operator override for when session-name matching cannot pick a session (several
    // candidates, or Epic's session named differently from PCAPTool's). Editor-session
    // scoped; clear it with an invalid FGuid.
    UFUNCTION(BlueprintCallable, Category="PCAP|Take Records")
    static void SetPinnedSessionUID(const FGuid& SessionUID);

    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static FGuid GetPinnedSessionUID();

    // ETakeLabel → EPCapTakeStatus, as the byte the reflected enum property takes.
    // Resolved by enumerator NAME off /Script/PerformanceCaptureWorkflow.EPCapTakeStatus;
    // an unmapped or unresolvable label lands on Neutral rather than asserting. (The
    // literal fallback matters: EPCapTakeStatus is { ThumbsUp=0, ThumbsDown=1, Neutral=2 },
    // so a zero-initialised byte would read as ThumbsUp.)
    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static uint8 TakeStatusValueForLabel(ETakeLabel Label);

    // The EPCapTakeStatus enumerator name a label maps to ("ThumbsUp"/"ThumbsDown"/"Neutral").
    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static FString TakeStatusNameForLabel(ETakeLabel Label);

    // Row keys — the take's canonical ID ("001003_004") and its slate (the ShotID).
    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static FName MakeTakeRowName(const FTake& Take);

    UFUNCTION(BlueprintPure, Category="PCAP|Take Records")
    static FName MakeSlateRowName(const FTake& Take);
};
