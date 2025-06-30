// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSpaceProgramEditorTarget : TargetRules
{
	public UnrealSpaceProgramEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		// CppStandard = CppStandardVersion.Cpp20;
		// IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

		ExtraModuleNames.AddRange( new string[] { "UnrealSpaceProgram" } );
	}
}
