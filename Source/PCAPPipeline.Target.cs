using UnrealBuildTool;

public class PCAPPipelineTarget : TargetRules
{
    public PCAPPipelineTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        // Pinned to 5.7 include order for the 5.8 upgrade so existing modules keep
        // compiling under the IWYU rules they were written for. Relax to Latest once
        // the project builds clean on 5.8.
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("PCAPPipeline");
    }
}
