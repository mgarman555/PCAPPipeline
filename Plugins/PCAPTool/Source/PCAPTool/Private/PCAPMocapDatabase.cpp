#include "PCAPMocapDatabase.h"

#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"

// The settings object lives in the Workflow plugin's private module — reach it
// by reflected class path + GetDefaultObject (it is a config CDO; see
// UPerformanceCaptureSettings::GetPerformanceCaptureSettings == GetMutableDefault).
static const TCHAR* GPCapSettingsClassPath = TEXT("/Script/PerformanceCaptureWorkflow.PerformanceCaptureSettings");

namespace
{
    UObject* GetSettingsCDO()
    {
#if WITH_PCAP_WORKFLOW
        if (UClass* C = FindObject<UClass>(nullptr, GPCapSettingsClassPath))
        {
            return C->GetDefaultObject();
        }
#endif
        return nullptr;
    }

    UDataTable* ReadSoftDataTable(const UObject* Owner, const TCHAR* PropName)
    {
        if (!Owner) { return nullptr; }
        if (const FSoftObjectProperty* P = FindFProperty<FSoftObjectProperty>(Owner->GetClass(), PropName))
        {
            const FSoftObjectPath Path = P->GetPropertyValue_InContainer(Owner).ToSoftObjectPath();
            return Cast<UDataTable>(Path.TryLoad());
        }
        return nullptr;
    }

    // ── Row field readers (operate on a row's raw struct memory) ──

    FName ReadRowName(const UScriptStruct* S, const void* Row, const TCHAR* PropName)
    {
        if (const FNameProperty* P = FindFProperty<FNameProperty>(S, PropName))
        {
            return *P->ContainerPtrToValuePtr<FName>(Row);
        }
        return NAME_None;
    }

    FString ReadRowString(const UScriptStruct* S, const void* Row, const TCHAR* PropName)
    {
        if (const FStrProperty* P = FindFProperty<FStrProperty>(S, PropName))
        {
            return *P->ContainerPtrToValuePtr<FString>(Row);
        }
        return FString();
    }

    FGuid ReadRowGuid(const UScriptStruct* S, const void* Row, const TCHAR* PropName)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(S, PropName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                return *P->ContainerPtrToValuePtr<FGuid>(Row);
            }
        }
        return FGuid();
    }
}

bool UPCAPMocapDatabase::IsAvailable()
{
    return GetSettingsCDO() != nullptr;
}

UDataTable* UPCAPMocapDatabase::GetProductionTable()
{
    return ReadSoftDataTable(GetSettingsCDO(), TEXT("ProductionTable"));
}

UDataTable* UPCAPMocapDatabase::GetSessionTable()
{
    return ReadSoftDataTable(GetSettingsCDO(), TEXT("SessionTable"));
}

TArray<FPCAPProductionInfo> UPCAPMocapDatabase::GetAllProductions()
{
    TArray<FPCAPProductionInfo> Result;

    UDataTable* DT = GetProductionTable();
    if (!DT || !DT->RowStruct) { return Result; }

    const UScriptStruct* S = DT->RowStruct;
    for (const TPair<FName, uint8*>& Row : DT->GetRowMap())
    {
        if (!Row.Value) { continue; }
        FPCAPProductionInfo Info;
        Info.RowName         = Row.Key;
        Info.UID             = ReadRowGuid(S, Row.Value, TEXT("UID"));
        Info.ProductionName  = ReadRowName(S, Row.Value, TEXT("ProductionName"));
        Info.ProductionNotes = ReadRowString(S, Row.Value, TEXT("ProductionNotes"));
        Result.Add(MoveTemp(Info));
    }
    return Result;
}

TArray<FPCAPSessionInfo> UPCAPMocapDatabase::GetAllSessions()
{
    TArray<FPCAPSessionInfo> Result;

    UDataTable* DT = GetSessionTable();
    if (!DT || !DT->RowStruct) { return Result; }

    const UScriptStruct* S = DT->RowStruct;
    for (const TPair<FName, uint8*>& Row : DT->GetRowMap())
    {
        if (!Row.Value) { continue; }
        FPCAPSessionInfo Info;
        Info.RowName       = Row.Key;
        Info.UID           = ReadRowGuid(S, Row.Value, TEXT("UID"));
        Info.SessionName   = ReadRowName(S, Row.Value, TEXT("SessionName"));
        Info.ProductionUID = ReadRowGuid(S, Row.Value, TEXT("ProductionUID"));
        Info.SessionPath   = ReadRowString(S, Row.Value, TEXT("SessionPath"));
        Result.Add(MoveTemp(Info));
    }
    return Result;
}

TArray<FPCAPSessionInfo> UPCAPMocapDatabase::GetSessionsForProduction(const FGuid& ProductionUID)
{
    TArray<FPCAPSessionInfo> All = GetAllSessions();
    All.RemoveAll([&ProductionUID](const FPCAPSessionInfo& S) { return S.ProductionUID != ProductionUID; });
    return All;
}
