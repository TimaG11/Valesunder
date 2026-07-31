using UnrealBuildTool;

public class OtherBios : ModuleRules
{
    public OtherBios(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "UMG",
            "Niagara",
            "Slate",
            "SlateCore",
            "MoviePlayer",
            "RenderCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
        });
    }
}