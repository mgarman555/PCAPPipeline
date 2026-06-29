#include "PCAPMocapData.h"

#include "PCAPPerformerExtension.h"

#include "LiveLinkTypes.h"                       // FLiveLinkSubjectName
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "UObject/UnrealType.h"                   // FNameProperty / FStructProperty / FSoftObjectProperty
#include "UObject/TopLevelAssetPath.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// Epic's performer class lives in the Workflow plugin's private module — resolve
// it (and read it) only by path/reflection, never by include.
static const TCHAR* GPCapPerformerClassPath = TEXT("/Script/PerformanceCaptureWorkflow.PCapPerformerDataAsset");

namespace
{
    // ── Reflected field readers (return defaults if the field is absent) ──

    FName ReadName(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FNameProperty* P = FindFProperty<FNameProperty>(Obj->GetClass(), PropName))
        {
            return P->GetPropertyValue_InContainer(Obj);
        }
        return NAME_None;
    }

    FName ReadSubjectName(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropName))
        {
            if (P->Struct == FLiveLinkSubjectName::StaticStruct())
            {
                if (const FLiveLinkSubjectName* V = P->ContainerPtrToValuePtr<FLiveLinkSubjectName>(Obj))
                {
                    return V->Name;
                }
            }
        }
        return NAME_None;
    }

    FGuid ReadGuid(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                if (const FGuid* V = P->ContainerPtrToValuePtr<FGuid>(Obj))
                {
                    return *V;
                }
            }
        }
        return FGuid();
    }

    FSoftObjectPath ReadSoftPath(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FSoftObjectProperty* P = FindFProperty<FSoftObjectProperty>(Obj->GetClass(), PropName))
        {
            return P->GetPropertyValue_InContainer(Obj).ToSoftObjectPath();
        }
        return FSoftObjectPath();
    }

    IAssetRegistry* GetAssetRegistry()
    {
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        return &ARM.Get();
    }
}

bool UPCAPMocapData::IsWorkflowAvailable()
{
#if WITH_PCAP_WORKFLOW
    return FindObject<UClass>(nullptr, GPCapPerformerClassPath) != nullptr;
#else
    return false;
#endif
}

TArray<FPCAPPerformerInfo> UPCAPMocapData::GetAllPerformers()
{
    TArray<FPCAPPerformerInfo> Result;

#if WITH_PCAP_WORKFLOW
    IAssetRegistry* AR = GetAssetRegistry();
    if (!AR) { return Result; }

    TArray<FAssetData> Found;
    const FTopLevelAssetPath ClassPath(TEXT("/Script/PerformanceCaptureWorkflow"), TEXT("PCapPerformerDataAsset"));
    AR->GetAssetsByClass(ClassPath, Found, /*bSearchSubClasses*/ true);

    Result.Reserve(Found.Num());
    for (const FAssetData& AssetData : Found)
    {
        UObject* Asset = AssetData.GetAsset();
        if (!Asset) { continue; }

        FPCAPPerformerInfo Info;
        Info.Asset            = Asset;
        Info.PerformerName    = ReadName(Asset, TEXT("PerformerName"));
        Info.LiveLinkSubject  = ReadSubjectName(Asset, TEXT("LiveLinkSubject"));
        Info.AssetUID         = ReadGuid(Asset, TEXT("AssetUID"));
        Info.BaseSkeletalMesh = TSoftObjectPtr<USkeletalMesh>(ReadSoftPath(Asset, TEXT("BaseSkeletalMesh")));
        Result.Add(MoveTemp(Info));
    }
#endif

    return Result;
}

TArray<FPCAPPropInfo> UPCAPMocapData::GetAllProps()
{
    TArray<FPCAPPropInfo> Result;

#if WITH_PCAP_WORKFLOW
    IAssetRegistry* AR = GetAssetRegistry();
    if (!AR) { return Result; }

    TArray<FAssetData> Found;
    const FTopLevelAssetPath ClassPath(TEXT("/Script/PerformanceCaptureWorkflow"), TEXT("PCapPropDataAsset"));
    AR->GetAssetsByClass(ClassPath, Found, /*bSearchSubClasses*/ true);

    Result.Reserve(Found.Num());
    for (const FAssetData& AssetData : Found)
    {
        UObject* Asset = AssetData.GetAsset();
        if (!Asset) { continue; }

        FPCAPPropInfo Info;
        Info.Asset            = Asset;
        Info.PropName         = ReadName(Asset, TEXT("PropName"));
        Info.LiveLinkSubject  = ReadSubjectName(Asset, TEXT("LiveLinkSubject"));
        Info.AssetUID         = ReadGuid(Asset, TEXT("AssetUID"));
        Info.PropStaticMesh   = TSoftObjectPtr<UStaticMesh>(ReadSoftPath(Asset, TEXT("PropStaticMesh")));
        Info.PropSkeletalMesh = TSoftObjectPtr<USkeletalMesh>(ReadSoftPath(Asset, TEXT("PropSkeletalMesh")));
        Result.Add(MoveTemp(Info));
    }
#endif

    return Result;
}

TArray<FPCAPCharacterInfo> UPCAPMocapData::GetAllCharacters()
{
    TArray<FPCAPCharacterInfo> Result;

#if WITH_PCAP_WORKFLOW
    IAssetRegistry* AR = GetAssetRegistry();
    if (!AR) { return Result; }

    TArray<FAssetData> Found;
    const FTopLevelAssetPath ClassPath(TEXT("/Script/PerformanceCaptureWorkflow"), TEXT("PCapCharacterDataAsset"));
    AR->GetAssetsByClass(ClassPath, Found, /*bSearchSubClasses*/ true);

    Result.Reserve(Found.Num());
    for (const FAssetData& AssetData : Found)
    {
        UObject* Asset = AssetData.GetAsset();
        if (!Asset) { continue; }

        FPCAPCharacterInfo Info;
        Info.Asset                = Asset;
        Info.CharacterName        = ReadName(Asset, TEXT("CharacterName"));
        Info.AssetUID             = ReadGuid(Asset, TEXT("AssetUID"));
        Info.SkeletalMesh         = TSoftObjectPtr<USkeletalMesh>(ReadSoftPath(Asset, TEXT("SkeletalMesh")));
        Info.SourcePerformerAsset = TSoftObjectPtr<UObject>(ReadSoftPath(Asset, TEXT("SourcePerformerAsset")));
        Info.Retargeter           = TSoftObjectPtr<UObject>(ReadSoftPath(Asset, TEXT("Retargeter")));
        Result.Add(MoveTemp(Info));
    }
#endif

    return Result;
}

UPCAPPerformerExtension* UPCAPMocapData::FindPerformerExtension(const FGuid& PerformerUID)
{
    if (!PerformerUID.IsValid()) { return nullptr; }

    IAssetRegistry* AR = GetAssetRegistry();
    if (!AR) { return nullptr; }

    TArray<FAssetData> Found;
    AR->GetAssetsByClass(UPCAPPerformerExtension::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);

    for (const FAssetData& AssetData : Found)
    {
        if (UPCAPPerformerExtension* Ext = Cast<UPCAPPerformerExtension>(AssetData.GetAsset()))
        {
            if (Ext->PCapPerformerUID == PerformerUID)
            {
                return Ext;
            }
        }
    }
    return nullptr;
}
