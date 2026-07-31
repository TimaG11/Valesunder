// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class OtherBiosEditorTarget : TargetRules
{
    public OtherBiosEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

        ExtraModuleNames.AddRange(new string[]
        {
            "OtherBios",
            "OTHERBIOSEditor"
        });
    }
}