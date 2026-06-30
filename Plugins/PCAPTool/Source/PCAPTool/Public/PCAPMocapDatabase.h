#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPMocapDatabase.generated.h"

class UDataTable;

// Read-out of an Epic FPCapProductionRecord row.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPProductionInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid UID;                  // FPCapRecordBase.UID

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName RowName;              // DataTable row key

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName ProductionName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString ProductionNotes;
};

// Read-out of an Epic FPCapSessionRecord row.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPSessionInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid UID;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName RowName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName SessionName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid ProductionUID;        // owning production

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString SessionPath;        // content-browser folder
};

// ---------------------------------------------------------------------------
// Read layer over Epic's Mocap Manager *database* (the production / session
// DataTables referenced by UPerformanceCaptureSettings).
//
// Like UPCAPMocapData, everything is resolved by reflection (the settings and
// record structs live in the Workflow plugin's private module). Degrades to
// empty when WITH_PCAP_WORKFLOW=0 or the settings/tables are unset.
// Writing rows (session creation from the Call Sheet) lands in a follow-up.
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPMocapDatabase : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // True when UPerformanceCaptureSettings resolves (plugin present).
    UFUNCTION(BlueprintPure, Category="PCAP|Mocap Database")
    static bool IsAvailable();

    // The project's production / session DataTables (from settings), or null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Database")
    static UDataTable* GetProductionTable();

    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Database")
    static UDataTable* GetSessionTable();

    // All productions / sessions read from those tables.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Database")
    static TArray<FPCAPProductionInfo> GetAllProductions();

    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Database")
    static TArray<FPCAPSessionInfo> GetAllSessions();

    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Database")
    static TArray<FPCAPSessionInfo> GetSessionsForProduction(const FGuid& ProductionUID);
};
