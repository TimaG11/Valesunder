using UnrealBuildTool;

public class OTHERBIOSEditor : ModuleRules
{
    public OTHERBIOSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "UnrealEd",
                "PropertyEditor",
                "OtherBios"
            }
        );
    }
}