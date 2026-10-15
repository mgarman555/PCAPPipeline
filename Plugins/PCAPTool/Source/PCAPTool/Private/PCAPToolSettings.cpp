#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"

#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#endif

UPCAPToolSettings::UPCAPToolSettings()
{
    // Defaults (Madi, 2026-10-15). DefaultGame.ini overrides these once edited in Project Settings.
    MocapSystems = {
        FPCAPSystemOption(TEXT("Markerless UE")),
        FPCAPSystemOption(TEXT("Cortex")),
        FPCAPSystemOption(TEXT("Shogun Live"), { TEXT("1.13"), TEXT("1.19") }),
        FPCAPSystemOption(TEXT("Giant")),
    };
    FacialSystems = {
        FPCAPSystemOption(TEXT("OneHMC")),
        FPCAPSystemOption(TEXT("Faceware")),
        FPCAPSystemOption(TEXT("Rokoko iPhone")),
        FPCAPSystemOption(TEXT("Technoprops")),
    };
    AudioSystems = {
        FPCAPSystemOption(TEXT("Internal")),
        FPCAPSystemOption(TEXT("Lavs")),
        FPCAPSystemOption(TEXT("iPhone"), {}, TEXT("Rokoko iPhone")),
    };
}

UPCAPToolSettings* UPCAPToolSettings::Get()
{
    return GetMutableDefault<UPCAPToolSettings>();
}

TArray<FString> UPCAPToolSettings::FlattenOptions(const TArray<FPCAPSystemOption>& Options)
{
    TArray<FString> Out;
    for (const FPCAPSystemOption& O : Options)
    {
        const FString Name = O.Name.TrimStartAndEnd();
        if (Name.IsEmpty()) continue;
        if (O.Versions.Num() == 0) { Out.AddUnique(Name); continue; }
        for (const FString& V : O.Versions)
        {
            const FString Ver = V.TrimStartAndEnd();
            Out.AddUnique(Ver.IsEmpty() ? Name : FString::Printf(TEXT("%s %s"), *Name, *Ver));
        }
    }
    return Out;
}

TArray<FString> UPCAPToolSettings::AudioOptionsFor(const TArray<FPCAPSystemOption>& Audio, const FString& FacialSelection)
{
    TArray<FPCAPSystemOption> Allowed;
    for (const FPCAPSystemOption& O : Audio)
    {
        if (O.OnlyWithFacial.IsEmpty() || O.OnlyWithFacial.Equals(FacialSelection, ESearchCase::IgnoreCase))
            Allowed.Add(O);
    }
    return FlattenOptions(Allowed);
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
    const FString PackageName = PCAPPaths::MasterDatabase();   // /Game/PCAPTool/Databases/MasterPCAPDatabase

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
