
using System;
using System.IO;
using UnrealBuildTool;
using System.Security.Cryptography;
using System.Text;

public class ViconDataStreamSDK : ModuleRules
{
    private string ThirdPartyPath
    {
        get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "")); }
    }

    public ViconDataStreamSDK( ReadOnlyTargetRules Target) : base( Target )
    {
        Type = ModuleType.External;
        string LibrariesPath = Path.Combine(ThirdPartyPath, "ViconDataStreamSDK");
        PublicIncludePaths.Add(LibrariesPath);
        PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "ViconDataStreamSDK_CPP.lib"));
        PublicDelayLoadDLLs.Add(Path.Combine(LibrariesPath, "ViconDataStreamSDK_CPP.dll"));
    }
}
