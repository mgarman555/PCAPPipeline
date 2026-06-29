#include "PCAPMocapData.h"

#include "PCAPPerformerExtension.h"
#include "ActorRosterEntry.h"   // migration source

#include "LiveLinkTypes.h"                       // FLiveLinkSubjectName
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "UObject/UnrealType.h"                   // FNameProperty / FStructProperty / FSoftObjectProperty
#include "UObject/TopLevelAssetPath.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"

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

    // ── Reflected field writers ──

    void WriteName(UObject* Obj, const TCHAR* PropName, FName Value)
    {
        if (FNameProperty* P = FindFProperty<FNameProperty>(Obj->GetClass(), PropName))
        {
            P->SetPropertyValue_InContainer(Obj, Value);
        }
    }

    void WriteSubjectName(UObject* Obj, const TCHAR* PropName, FName Value)
    {
        if (FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropName))
        {
            if (P->Struct == FLiveLinkSubjectName::StaticStruct())
            {
                if (FLiveLinkSubjectName* V = P->ContainerPtrToValuePtr<FLiveLinkSubjectName>(Obj))
                {
                    V->Name = Value;
                }
            }
        }
    }

    // Returns the asset's AssetUID, generating + assigning one if it was invalid.
    FGuid EnsureGuid(UObject* Obj, const TCHAR* PropName, bool& bOutAssigned)
    {
        bOutAssigned = false;
        if (FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                if (FGuid* V = P->ContainerPtrToValuePtr<FGuid>(Obj))
                {
                    if (!V->IsValid()) { *V = FGuid::NewGuid(); bOutAssigned = true; }
                    return *V;
                }
            }
        }
        return FGuid();
    }

    void SavePackageFor(UObject* Obj)
    {
        if (Obj)
        {
            if (UPackage* Pkg = Obj->GetPackage())
            {
                Pkg->MarkPackageDirty();
                FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
            }
        }
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

FGuid UPCAPMocapData::GetAssetUID(const UObject* PCapAsset)
{
    return PCapAsset ? ReadGuid(PCapAsset, TEXT("AssetUID")) : FGuid();
}

UObject* UPCAPMocapData::CreatePerformerAsset(const FString& PackagePath, FName PerformerName, FName LiveLinkSubject)
{
#if WITH_PCAP_WORKFLOW
    UClass* PerformerClass = FindObject<UClass>(nullptr, GPCapPerformerClassPath);
    if (!PerformerClass) { return nullptr; }

    const FString AssetName   = PerformerName.IsNone() ? TEXT("Performer") : PerformerName.ToString();
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
    if (FPackageName::DoesPackageExist(PackageName)) { return nullptr; }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) { return nullptr; }

    UObject* Asset = NewObject<UObject>(Package, PerformerClass, FName(*AssetName),
                                        RF_Public | RF_Standalone | RF_Transactional);
    if (!Asset) { return nullptr; }

    WriteName(Asset, TEXT("PerformerName"), PerformerName);
    if (!LiveLinkSubject.IsNone())
    {
        WriteSubjectName(Asset, TEXT("LiveLinkSubject"), LiveLinkSubject);
    }
    bool bAssigned = false;
    EnsureGuid(Asset, TEXT("AssetUID"), bAssigned);

    FAssetRegistryModule::AssetCreated(Asset);
    SavePackageFor(Asset);
    return Asset;
#else
    return nullptr;
#endif
}

UPCAPPerformerExtension* UPCAPMocapData::EnsurePerformerExtension(UObject* PerformerAsset)
{
    if (!PerformerAsset) { return nullptr; }

    bool bAssigned = false;
    const FGuid UID = EnsureGuid(PerformerAsset, TEXT("AssetUID"), bAssigned);
    if (bAssigned) { SavePackageFor(PerformerAsset); }   // persist a freshly-minted UID
    if (!UID.IsValid()) { return nullptr; }

    if (UPCAPPerformerExtension* Existing = FindPerformerExtension(UID))
    {
        return Existing;
    }

    // Create the sidecar next to the performer asset: "<package>_Ext".
    const FString ExtPackageName = PerformerAsset->GetPackage()->GetName() + TEXT("_Ext");
    const FString ExtAssetName   = PerformerAsset->GetName() + TEXT("_Ext");

    UPackage* Package = CreatePackage(*ExtPackageName);
    if (!Package) { return nullptr; }

    UPCAPPerformerExtension* Ext = NewObject<UPCAPPerformerExtension>(
        Package, FName(*ExtAssetName), RF_Public | RF_Standalone | RF_Transactional);
    Ext->PCapPerformerUID = UID;
    Ext->PerformerAsset   = PerformerAsset;

    FAssetRegistryModule::AssetCreated(Ext);
    SavePackageFor(Ext);
    return Ext;
}

int32 UPCAPMocapData::MigrateRosterToPCap(const FString& PackagePath)
{
    int32 Created = 0;

#if WITH_PCAP_WORKFLOW
    IAssetRegistry* AR = GetAssetRegistry();
    if (!AR) { return 0; }

    TArray<FAssetData> Found;
    AR->GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);

    for (const FAssetData& AssetData : Found)
    {
        UActorRosterEntry* Roster = Cast<UActorRosterEntry>(AssetData.GetAsset());
        if (!Roster || Roster->ActorID.IsEmpty()) { continue; }

        UObject* Performer = CreatePerformerAsset(
            PackagePath, FName(*Roster->ActorID), Roster->DefaultBodyStream.LiveLinkSubjectName);
        if (!Performer) { continue; }   // already exists, or creation failed — skip

        if (UPCAPPerformerExtension* Ext = EnsurePerformerExtension(Performer))
        {
            Ext->FaceStream             = Roster->DefaultFaceStream;
            Ext->AudioStreams           = Roster->DefaultAudioStreams;
            Ext->FaceScan               = Roster->FaceScan;
            Ext->bUseFaceScanOnMetaHuman= Roster->bUseFaceScanOnMetaHuman;
            Ext->Headshot               = Roster->Headshot;
            Ext->ProductionHistory      = Roster->ProductionHistory;
            Ext->Notes                  = Roster->Notes;
            SavePackageFor(Ext);
        }
        ++Created;
    }
#endif

    return Created;
}
