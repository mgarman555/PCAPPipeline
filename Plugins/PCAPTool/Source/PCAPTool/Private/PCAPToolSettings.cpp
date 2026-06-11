#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"

#include "UObject/Package.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#endif

UPCAPToolSettings* UPCAPToolSettings::Get()
{
    return GetMutableDefault<UPCAPToolSettings>();
}

UMocapDatabase* UPCAPToolSettings::GetDatabase() const
{
    if (DatabaseAsset.IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] DatabaseAsset not set — go to Edit -> Project Settings -> PCAP Tool"));
        return nullptr;
    }
    UMocapDatabase* DB = DatabaseAsset.LoadSynchronous();
    if (!DB)
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] Failed to load DatabaseAsset: %s"), *DatabaseAsset.ToString());
    return DB;
}

UMocapDatabase* UPCAPToolSettings::GetOrCreateDatabase()
{
    if (UMocapDatabase* Existing = GetDatabase())
    {
        return Existing;
    }

#if WITH_EDITOR
    // None assigned — create one in the PCAP tool area and self-assign so the
    // tool is zero-setup. Mirrors the roster panels' create pattern.
    const FString PackageName = PCAPPaths::MasterDatabase();   // /Game/PCAPTool/Databases/MocapDatabase

    UMocapDatabase* DB = nullptr;
    if (FPackageName::DoesPackageExist(PackageName))
    {
        DB = LoadObject<UMocapDatabase>(nullptr, *PackageName);
    }
    else if (UPackage* Package = CreatePackage(*PackageName))
    {
        DB = NewObject<UMocapDatabase>(Package, FName(TEXT("MasterPCAPDatabase")), RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(DB);
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }

    if (DB)
    {
        DatabaseAsset = DB;
        TryUpdateDefaultConfigFile();   // persist the assignment to DefaultGame.ini
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] Created + assigned database at %s"), *PackageName);
    }
    return DB;
#else
    return nullptr;
#endif
}
