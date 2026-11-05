// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AuraServerTarget : TargetRules
{
	public AuraServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("Aura");
	}
}
