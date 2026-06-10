using UnrealBuildTool;

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
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",             // FEditorFileUtils, tab manager
            "IKRig",                // Forward-declared in PCAPToolTypes.h
            "TakeRecorder",         // UTakeRecorderBlueprintLibrary, FTakeRecorderParameters, UTakeRecorderSubsystem
            "TakesCore",            // UTakeRecorderSources, UTakeMetaData
            "MovieScene",           // FFrameNumber / movie-scene types used by Take Recorder params
            "AssetRegistry",        // list roster DataAssets (Actor/Prop/Stage databases)
        });
    }
}
