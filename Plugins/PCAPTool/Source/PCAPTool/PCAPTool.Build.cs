using UnrealBuildTool;
using System.IO;

public class PCAPTool : ModuleRules
{
    public PCAPTool(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings",    // UDeveloperSettings
            "LevelSequence",        // ULevelSequence in PCAPToolTypes.h
            "HTTP",                 // FHttpModule — HMC video frame pull
            "Json",                 // FJsonObject parsing
            "JsonUtilities",        // TJsonReader/Writer helpers
            "UMG",                  // UUserWidget — PCAPToolEditorWidget
            "Blutility",            // UEditorUtilityWidget
            "ImageWrapper",         // JPEG decode for HMC video frames
            "Slate",                // Slate UI framework
            "SlateCore",            // Slate core types
            "ToolMenus",            // Window menu registration
            "WorkspaceMenuStructure",// Window menu category
            "InputCore",            // FKey, keyboard input in Slate
            "CinematicCamera",      // ACineCameraActor / UCineCameraComponent — APCAPVCamActor base
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",             // FEditorFileUtils, tab manager
            "IKRig",                // Forward-declared in PCAPToolTypes.h
            "TakeRecorder",         // UTakeRecorderBlueprintLibrary, FTakeRecorderParameters, UTakeRecorderSubsystem
            "TakesCore",            // UTakeRecorderSources, UTakeMetaData
            "MovieScene",           // FFrameNumber / movie-scene types used by Take Recorder params
            "AssetRegistry",        // list roster DataAssets (Actor/Prop/Stage databases)
            "PropertyEditor",       // SObjectPropertyEntryBox — asset-picker slots in DB forms
            "LiveLink",             // FLiveLinkClientReference / plugin module
            "LiveLinkInterface",    // ILiveLinkClient, transform role + frame-data types — TPVCam read
            "Networking",           // FUdpSocketBuilder / FUdpSocketReceiver — WVCAM raw-broadcast listener
            "Sockets",              // FSocket / ISocketSubsystem
        });

        // ── Vicon DataStream SDK (Phase 2 raw markers) ───────────────────────────
        // Reference the SDK bundled in the sibling LiveLinkViconDataStream plugin if it
        // is present; otherwise Phase 2 compiles out (WITH_VICON_SDK=0) and the Live Link
        // stand-in still builds. No cross-plugin module dependency — we point straight at
        // the SDK's include path + import lib + delay-loaded DLL.
        string ViconSdkDir = Path.GetFullPath(Path.Combine(PluginDirectory, "..", "LiveLinkViconDataStream", "Source", "ThirdParty", "ViconDataStreamSDK"));
        string ViconLib = Path.Combine(ViconSdkDir, "ViconDataStreamSDK_CPP.lib");
        if (File.Exists(ViconLib))
        {
            PublicIncludePaths.Add(ViconSdkDir);
            PublicAdditionalLibraries.Add(ViconLib);
            PublicDelayLoadDLLs.Add("ViconDataStreamSDK_CPP.dll");
            PublicDefinitions.Add("WITH_VICON_SDK=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_VICON_SDK=0");
        }
    }
}
