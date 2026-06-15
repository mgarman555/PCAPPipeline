using UnrealBuildTool;
using System;
using System.IO;
using System.Text;
public class LiveLinkDataStreamEditor : ModuleRules
{

  public LiveLinkDataStreamEditor(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LiveLinkDataStream" });
    PrivateDependencyModuleNames.AddRange(
      new string[]
      {
        "Core",
        "UnrealEd",
        "SlateCore",
        "Slate",
        "InputCore",
        "Projects",
        "LiveLink",
        "LiveLinkInterface",
        "TimeManagement",
        // ThirdParty
        "AsioStandalone",
        // TODO: This is only necessary because of the dependency on ViconStreamFrameReader for some constant strings
        // The required dependencies should be factored out to a file that is not dependent on dssdk
        "ViconDataStreamSDK"
      }
    );
    // Required to use Asio in standalone mode.
    PublicDefinitions.Add("ASIO_STANDALONE=1");
    // Due to a bug compiling asio 1.18.1 with MSVC https://github.com/chriskohlhoff/asio/pull/584
    PublicDefinitions.Add("ASIO_DISABLE_CO_AWAIT=1");
    // Exceptions are disabled in unreal engine by default. We use non-throwing versions of asio functions.
    // When ASIO_NO_EXCEPTIONS is defined, asio::detail::throw_exception is declared but not defined
    // (https://github.com/chriskohlhoff/asio/blob/asio-1-18-1/asio/include/asio/detail/throw_exception.hpp)
    // and the application must provide a definition.
    PublicDefinitions.Add("ASIO_NO_EXCEPTIONS=1");

    // If the projects are generated before the ViconDataStreamSDK has been built, this directory will not exist
    // so we handle this failure gracefully to prevent project generation errors.
    var ModuleBinaryPath = Path.Combine(ModuleDirectory, "../../Binaries/Win64/");
    if (Directory.Exists(ModuleBinaryPath))
    {
      foreach (string FilePath in Directory.EnumerateFiles(ModuleBinaryPath, "*.dll", SearchOption.AllDirectories))
      {
        RuntimeDependencies.Add("$(TargetOutputDir)/" + Path.GetFileName(FilePath), FilePath);
      }
    }
  }
}
