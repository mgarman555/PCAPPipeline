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
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
        });

        // IKRig types (UIKRigDefinition, UIKRetargeter) are referenced via
        // TSoftObjectPtr with forward declarations — no hard link needed at this
        // stage. Add "IKRig" here when runtime loading of those assets is required.
    }
}
