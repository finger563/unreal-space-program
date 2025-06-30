// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSpaceProgramTarget : TargetRules
{
	public UnrealSpaceProgramTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		// CppStandard = CppStandardVersion.Cpp20;
		// IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

		ExtraModuleNames.AddRange( new string[] { "UnrealSpaceProgram" } );
	}
}
