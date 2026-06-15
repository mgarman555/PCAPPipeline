
using System;
using System.IO;
using UnrealBuildTool;
using System.Text;

public class AsioStandalone : ModuleRules
{
    private string ThirdPartyPath
    {
        get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "")); }
    }

    public AsioStandalone( ReadOnlyTargetRules Target) : base( Target )
    {
        Type = ModuleType.External;
        string LibrariesPath = Path.Combine(ThirdPartyPath, "AsioStandalone/include");
        PublicIncludePaths.Add(LibrariesPath);
    }
}
