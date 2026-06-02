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
            "IKRig",              // UIKRigDefinition, UIKRetargeter in FRetargetConfig
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
        });
    }
}
