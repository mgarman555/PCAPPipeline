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
            "DeveloperSettings",  // UDeveloperSettings
            "LevelSequence",      // ULevelSequence in PCAPToolTypes.h
            "HTTP",               // FHttpModule — HMCMonitorComponent
            "Json",               // FJsonObject parsing
            "JsonUtilities",      // TJsonReader helpers
            "UMG",                // UUserWidget base — PCAPToolEditorWidget
            "Blutility",          // UEditorUtilityWidget
            "ImageWrapper",       // JPEG decode for HMC video frames
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",           // FEditorFileUtils::PromptForCheckoutAndSave
            // IKRig is Private so UHT does not scan IKRIG_API macros when
            // processing PCAPToolTypes.h. Types are forward-declared in the header;
            // any .cpp resolving soft refs must include IKRig headers directly.
            "IKRig",
        });
    }
}
