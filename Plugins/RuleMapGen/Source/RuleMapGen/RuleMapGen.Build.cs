using UnrealBuildTool;

public class RuleMapGen : ModuleRules
{
    public RuleMapGen(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "InputCore",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "PropertyEditor"
        });
    }
}