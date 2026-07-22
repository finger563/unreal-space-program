// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealSpaceProgram : ModuleRules
{
	public UnrealSpaceProgram(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] {
			"JSBSimFlightDynamicsModel",
			// Rocket telemetry widget (UMG built in code)
			"UMG", "Slate", "SlateCore",
			// Rocket HIL UDP bridge
			"Sockets", "Networking",
			// Rocket visuals: procedurally generated airframe/canopy geometry, simulated
			// shock cord + shroud lines, and optional Niagara FX overrides. All three
			// plugins are EnabledByDefault in the engine, so the .uproject needs no changes.
			"ProceduralMeshComponent", "CableComponent", "Niagara",
			// Wind arrow: transforms the wind's NED vector into UE world using the same
			// georeferencing frame the JSBSim plugin uses (the project already enables this).
			"GeoReferencing"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
