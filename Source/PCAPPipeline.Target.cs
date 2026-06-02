using UnrealBuildTool;

public class PCAPPipelineTarget : TargetRules
{
    public PCAPPipelineTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PCAPPipeline");
    }
}
