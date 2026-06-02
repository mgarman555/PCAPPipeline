using UnrealBuildTool;

public class PCAPPipelineEditorTarget : TargetRules
{
    public PCAPPipelineEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PCAPPipeline");
    }
}
