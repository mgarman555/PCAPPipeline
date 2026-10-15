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
    // Raw capture files live here, outside Content/ (HOME_REBUILD_HANDOFF §5 step 5).
    CaptureRoot.Path = TEXT("D:/PCAPCapture");

    // Defaults (Madi, 2026-10-15). DefaultGame.ini overrides these once edited in Project Settings.
    MocapSystems = {
        FPCAPSystemOption(TEXT("Markerless UE")),
        FPCAPSystemOption(TEXT("Cortex")),
        FPCAPSystemOption(TEXT("Shogun Live"), { TEXT("1.13"), TEXT("1.19") }),
        FPCAPSystemOption(TEXT("Giant")),
    };
    // Live Link requirements per system (docs/devices/mocap-livelink.md). None verified here yet.
    {
        FPCAPLiveLinkRequirements& L = MocapSystems[0].LiveLink;
        L.SourceType     = TEXT("Offline solve (no Live Link): recorded video -> MetaHuman Animator markerless solve");
        L.RequiredPlugin = TEXT("MetaHuman Animator Markerless Motion Capture (Fab, experimental, Windows, UE 5.8+) + MetaHuman Animator + Capture Manager Editor + Live Link Hub");
        L.Compatibility  = TEXT("No realtime body stream in 5.8; body animation arrives after the take");
        L.SourceUrl      = TEXT("https://dev.epicgames.com/documentation/metahuman/metahuman-animation-from-mono-video-capture-in-unreal-engine");
    }
    {
        FPCAPLiveLinkRequirements& L = MocapSystems[1].LiveLink;
        L.SourceType     = TEXT("UNVERIFIED: no public Cortex Live Link plugin found. Likely routes: Cortex -> MotionBuilder -> MotionBuilder Live Link, or Cortex's VRPN server");
        L.RequiredPlugin = TEXT("Ask Motion Analysis which Unreal plugin / SDK applies to your Cortex version");
        L.SourceUrl      = TEXT("https://www.motionanalysis.com/software/cortex-software/");
    }
    {
        FPCAPLiveLinkRequirements& L = MocapSystems[2].LiveLink;
        L.SourceType     = TEXT("Vicon DataStream (Live Link source)");
        L.RequiredPlugin = TEXT("Vicon Live Link plugin (LiveLinkViconDataStream; in this repo, its SDK DLL is missing)");
        L.DefaultHost    = TEXT("localhost");   // Vicon Server Name: IP or computer name of the Shogun machine
        L.DefaultPort    = 801;                 // 801 = Vicon default; 804 = Shogun "Always Live" / low-latency output
        L.SourceSettings = { TEXT("SubjectFilter="), TEXT("bIsRetimed=false"), TEXT("bUsePreFetch=false"), TEXT("bIsScaled=true"),
                             TEXT("bLogOutput=false"), TEXT("Offset=0.0"), TEXT("EnableLightweight"), TEXT("StreamMarkerData"),
                             TEXT("StreamUnlabeledMarkerData") };
        L.Compatibility  = TEXT("Vicon UE 5.8 plugin (1.13) is listed for Shogun Live 1.14+: Shogun Live 1.13 may not connect. Auto-discovery on UE 5.7+ uses Windows DNS-SD (Shogun Live 1.14+); manual IP always available");
        L.SourceUrl      = TEXT("https://help.vicon.com/space/Unreal5Plugin111/337740484/Install+and+set+up+Live+Link");
    }
    {
        FPCAPLiveLinkRequirements& L = MocapSystems[3].LiveLink;
        L.SourceType     = TEXT("UNVERIFIED: no public Giant Unreal Live Link plugin found. Likely route: Giant solve -> MotionBuilder -> MotionBuilder Live Link");
        L.RequiredPlugin = TEXT("MotionBuilder Live Link plugin (Epic) if routed through MotionBuilder; confirm with the Giant-licensed stage");
        L.SourceUrl      = TEXT("https://dev.epicgames.com/documentation/unreal-engine/live-link-stream-motionbuilder-to-unreal-engine");
    }
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

const FPCAPSystemOption* UPCAPToolSettings::FindOption(const TArray<FPCAPSystemOption>& Options, const FString& Selection)
{
    const FString Sel = Selection.TrimStartAndEnd();
    if (Sel.IsEmpty()) return nullptr;
    for (const FPCAPSystemOption& O : Options)
    {
        const FString Name = O.Name.TrimStartAndEnd();
        if (Name.Equals(Sel, ESearchCase::IgnoreCase)) return &O;
        for (const FString& V : O.Versions)
            if (FString::Printf(TEXT("%s %s"), *Name, *V.TrimStartAndEnd()).Equals(Sel, ESearchCase::IgnoreCase)) return &O;
    }
    return nullptr;
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
