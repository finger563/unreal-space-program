// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketFlightController.h"
#include "JSBSimMovementComponent.h"
#include "Engine/Engine.h"

URocketFlightController::URocketFlightController()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URocketFlightController::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the movement component up-front and make sure the rocket starts inert. The
	// JSBSim component may (harmlessly) have defaulted some engine command values; force the
	// motor off so nothing fires until we (or the user) command ignition.
	if (UJSBSimMovementComponent* Move = GetMovement())
	{
		if (Move->EngineCommands.IsValidIndex(EngineIndex))
		{
			Move->EngineCommands[EngineIndex].Throttle = 0.0;
			Move->EngineCommands[EngineIndex].Running = false;
			Move->EngineCommands[EngineIndex].Starter = false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URocketFlightController on '%s' could not find a UJSBSimMovementComponent - the rocket will not simulate."), *GetNameSafe(GetOwner()));
	}

	SetPhase(ERocketFlightPhase::Prelaunch);
}

UJSBSimMovementComponent* URocketFlightController::GetMovement()
{
	if (MovementComponent)
	{
		return MovementComponent;
	}
	if (AActor* Owner = GetOwner())
	{
		MovementComponent = Owner->FindComponentByClass<UJSBSimMovementComponent>();
	}
	return MovementComponent;
}

void URocketFlightController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MissionTimeSeconds += DeltaTime;
	if (bMotorIgnited)
	{
		TimeSinceIgnitionSeconds += DeltaTime;
	}

	UpdateTelemetry();

	// --- Auto ignition ---
	// Wait for the countdown AND for the rocket to be settled on the pad. Igniting while the
	// vehicle is still falling/bouncing (bad placement, ground raycast issues) would cascade
	// into instant false liftoff/apogee detections - surface that instead of masking it.
	if (Mode == ERocketFlightMode::Auto && !bMotorIgnited && !bLanded &&
		MissionTimeSeconds >= IgnitionDelaySeconds)
	{
		if (FMath::Abs(VerticalSpeedFps) <= MaxPadSpeedForAutoIgnitionFps)
		{
			Ignite();
		}
		else if (!bWarnedWaitingForSettle && MissionTimeSeconds > IgnitionDelaySeconds + 5.0f)
		{
			bWarnedWaitingForSettle = true;
			UE_LOG(LogTemp, Warning, TEXT("Rocket auto-ignition is holding: vehicle is not settled on the pad (VS %.1f ft/s, AGL %.1f ft). Check actor placement (origin on the pad, pitch +90) and that the terrain under it has collision."),
				VerticalSpeedFps, AltitudeAGLFt);
		}
	}

	const float AltitudeGainFt = AltitudeAGLFt - IgnitionAltitudeAGLFt;
	const float MaxAltitudeGainFt = MaxAltitudeAGLFt - IgnitionAltitudeAGLFt;

	// --- Liftoff detection: must be genuinely climbing AND above the ignition point ---
	if (bMotorIgnited && !bLiftedOff &&
		VerticalSpeedFps > LiftoffVerticalSpeedFps && AltitudeGainFt > LiftoffAltitudeGainFt)
	{
		bLiftedOff = true;
		OnLiftoff.Broadcast(AltitudeAGLFt);
		UE_LOG(LogTemp, Display, TEXT("Rocket liftoff at T+%.2fs (alt %.1f ft AGL, VS %.1f ft/s)."), TimeSinceIgnitionSeconds, AltitudeAGLFt, VerticalSpeedFps);
	}

	// --- Track apogee ---
	if (bLiftedOff && AltitudeAGLFt > MaxAltitudeAGLFt)
	{
		MaxAltitudeAGLFt = AltitudeAGLFt;
	}

	// Powered ascent transitions to coast once the motor stops producing thrust.
	if (bLiftedOff && !bReachedApogee && Phase == ERocketFlightPhase::PoweredAscent && MotorThrustLbf <= 0.01f)
	{
		SetPhase(ERocketFlightPhase::CoastAscent);
	}

	// --- Apogee detection: real descent after a real ascent (mirrors the reference sim) ---
	if (bLiftedOff && !bReachedApogee &&
		MaxAltitudeGainFt > MinApogeeAltitudeGainFt &&
		VerticalSpeedFps < ApogeeVerticalSpeedThresholdFps)
	{
		bReachedApogee = true;
		ApogeeAltitudeAGLFt = MaxAltitudeAGLFt;
		OnApogee.Broadcast(ApogeeAltitudeAGLFt);
		UE_LOG(LogTemp, Display, TEXT("Rocket apogee: %.1f ft AGL at T+%.2fs."), ApogeeAltitudeAGLFt, TimeSinceIgnitionSeconds);

		if (bAutoRecovery && !bDrogueDeployed)
		{
			DeployDrogue();
		}
	}

	// --- Main chute auto-deploy ---
	if (bReachedApogee && bAutoRecovery && !bMainDeployed && AltitudeAGLFt < MainDeployAltitudeAGLFt)
	{
		DeployMain();
	}

	// --- Landing detection ---
	if (bLiftedOff && !bLanded && bReachedApogee &&
		AltitudeAGLFt < LandingAltitudeAGLFt && FMath::Abs(VerticalSpeedFps) < LandingSpeedThresholdFps)
	{
		bLanded = true;
		SetPhase(ERocketFlightPhase::Landed);
		OnLanded.Broadcast(AltitudeAGLFt);
		UE_LOG(LogTemp, Display, TEXT("Rocket landed at T+%.2fs. Apogee was %.1f ft AGL."), TimeSinceIgnitionSeconds, ApogeeAltitudeAGLFt);
	}

	if (bShowFlightHUD)
	{
		DrawHUD();
	}

	if (bLogTelemetry)
	{
		TelemetryLogAccumulator += DeltaTime;
		if (TelemetryLogAccumulator >= TelemetryLogInterval)
		{
			TelemetryLogAccumulator = 0.0f;
			LogTelemetry();
		}
	}
}

void URocketFlightController::LogTelemetry()
{
	static const TCHAR* PhaseNames[] = {
		TEXT("PRELAUNCH"), TEXT("POWERED"), TEXT("COAST"), TEXT("DROGUE"), TEXT("MAIN"), TEXT("LANDED")
	};
	const int32 PhaseIdx = FMath::Clamp((int32)Phase, 0, 5);

	UE_LOG(LogTemp, Display, TEXT("RKT MET=%7.2f %-9s AGL=%9.1fft ASL=%9.1fft VS=%+8.1ffps Thrust=%7.1flbf ign=%d lo=%d apo=%d dro=%d main=%d"),
		MissionTimeSeconds, PhaseNames[PhaseIdx], AltitudeAGLFt, AltitudeASLFt, VerticalSpeedFps, MotorThrustLbf,
		bMotorIgnited ? 1 : 0, bLiftedOff ? 1 : 0, bReachedApogee ? 1 : 0, bDrogueDeployed ? 1 : 0, bMainDeployed ? 1 : 0);
}

void URocketFlightController::UpdateTelemetry()
{
	UJSBSimMovementComponent* Move = GetMovement();
	if (!Move)
	{
		return;
	}

	AltitudeAGLFt = (float)Move->AircraftState.AltitudeAGLFt;
	AltitudeASLFt = (float)Move->AircraftState.AltitudeASLFt;
	VerticalSpeedFps = (float)Move->AircraftState.AltitudeRateFtps;

	if (Move->EngineStates.IsValidIndex(EngineIndex))
	{
		MotorThrustLbf = (float)Move->EngineStates[EngineIndex].Thrust;
	}
}

void URocketFlightController::Ignite()
{
	if (bMotorIgnited)
	{
		return;
	}

	UJSBSimMovementComponent* Move = GetMovement();
	if (!Move || !Move->EngineCommands.IsValidIndex(EngineIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("URocketFlightController::Ignite - no valid engine at index %d."), EngineIndex);
		return;
	}

	// Solid-motor ignition: full throttle + running. Mirrors the reference sim, which sets
	// throttle to 1.0 and marks the engine running. The JSBSim FGRocket then follows its own
	// thrust curve and consumes propellant until burnout.
	FEngineCommand& Cmd = Move->EngineCommands[EngineIndex];
	Cmd.Throttle = 1.0;
	Cmd.Mixture = 1.0;
	Cmd.Starter = true;
	Cmd.Running = true;

	bMotorIgnited = true;
	TimeSinceIgnitionSeconds = 0.0f;
	IgnitionAltitudeAGLFt = AltitudeAGLFt;
	MaxAltitudeAGLFt = AltitudeAGLFt;
	SetPhase(ERocketFlightPhase::PoweredAscent);
	OnIgnition.Broadcast(AltitudeAGLFt);
	UE_LOG(LogTemp, Display, TEXT("Rocket ignition at T+%.2fs (alt %.1f ft AGL, VS %.1f ft/s)."), MissionTimeSeconds, AltitudeAGLFt, VerticalSpeedFps);
}

void URocketFlightController::ShutdownMotor()
{
	UJSBSimMovementComponent* Move = GetMovement();
	if (Move && Move->EngineCommands.IsValidIndex(EngineIndex))
	{
		FEngineCommand& Cmd = Move->EngineCommands[EngineIndex];
		Cmd.Throttle = 0.0;
		Cmd.Running = false;
		Cmd.Starter = false;
	}
}

void URocketFlightController::SetThrottle(float Throttle01)
{
	UJSBSimMovementComponent* Move = GetMovement();
	if (Move && Move->EngineCommands.IsValidIndex(EngineIndex))
	{
		Move->EngineCommands[EngineIndex].Throttle = FMath::Clamp(Throttle01, 0.0f, 1.0f);
	}
}

void URocketFlightController::SetChuteAreaProperty(const FString& PropertyPath, float AreaSqFt)
{
	UJSBSimMovementComponent* Move = GetMovement();
	if (!Move)
	{
		return;
	}
	FString OutValue;
	Move->CommandConsole(PropertyPath, FString::SanitizeFloat(AreaSqFt), OutValue);
}

void URocketFlightController::SetDrogueDragArea(float AreaSqFt)
{
	SetChuteAreaProperty(TEXT("external_reactions/drogue_chute/drag_area"), AreaSqFt);
}

void URocketFlightController::SetMainDragArea(float AreaSqFt)
{
	SetChuteAreaProperty(TEXT("external_reactions/main_chute/drag_area"), AreaSqFt);
}

void URocketFlightController::DeployDrogue()
{
	if (bDrogueDeployed)
	{
		return;
	}
	SetDrogueDragArea(DrogueDragAreaSqFt);
	bDrogueDeployed = true;
	if (!bMainDeployed)
	{
		SetPhase(ERocketFlightPhase::DrogueDescent);
	}
	OnDrogueDeployed.Broadcast(AltitudeAGLFt);
	UE_LOG(LogTemp, Display, TEXT("Drogue chute deployed at %.1f ft AGL (%.1f ft^2)."), AltitudeAGLFt, DrogueDragAreaSqFt);
}

void URocketFlightController::DeployMain()
{
	if (bMainDeployed)
	{
		return;
	}
	SetMainDragArea(MainDragAreaSqFt);
	bMainDeployed = true;
	SetPhase(ERocketFlightPhase::MainDescent);
	OnMainDeployed.Broadcast(AltitudeAGLFt);
	UE_LOG(LogTemp, Display, TEXT("Main chute deployed at %.1f ft AGL (%.1f ft^2)."), AltitudeAGLFt, MainDragAreaSqFt);
}

void URocketFlightController::ResetFlight()
{
	bMotorIgnited = false;
	bLiftedOff = false;
	bReachedApogee = false;
	bDrogueDeployed = false;
	bMainDeployed = false;
	bLanded = false;
	MissionTimeSeconds = 0.0f;
	TimeSinceIgnitionSeconds = 0.0f;
	MaxAltitudeAGLFt = 0.0f;
	ApogeeAltitudeAGLFt = 0.0f;
	IgnitionAltitudeAGLFt = 0.0f;
	TelemetryLogAccumulator = 0.0f;
	bWarnedWaitingForSettle = false;

	ShutdownMotor();
	SetDrogueDragArea(0.0f);
	SetMainDragArea(0.0f);
	SetPhase(ERocketFlightPhase::Prelaunch);
}

void URocketFlightController::SetPhase(ERocketFlightPhase NewPhase)
{
	if (Phase == NewPhase)
	{
		return;
	}
	Phase = NewPhase;
	OnPhaseChanged.Broadcast(NewPhase);
}

void URocketFlightController::DrawHUD()
{
	if (!GEngine)
	{
		return;
	}

	static const TCHAR* PhaseNames[] = {
		TEXT("PRELAUNCH"), TEXT("POWERED ASCENT"), TEXT("COAST"),
		TEXT("DROGUE DESCENT"), TEXT("MAIN DESCENT"), TEXT("LANDED")
	};
	const int32 PhaseIdx = (int32)Phase;
	const FString PhaseStr = PhaseNames[FMath::Clamp(PhaseIdx, 0, 5)];

	const FString Msg = FString::Printf(
		TEXT("ROCKET  |  MET %6.1fs  |  %s\n")
		TEXT("  Alt AGL %8.1f ft   Alt ASL %8.1f ft\n")
		TEXT("  V.Speed %8.1f ft/s  Apogee  %8.1f ft\n")
		TEXT("  Thrust  %8.1f lbf   Ignited %s  Drogue %s  Main %s"),
		MissionTimeSeconds, *PhaseStr,
		AltitudeAGLFt, AltitudeASLFt,
		VerticalSpeedFps, (bReachedApogee ? ApogeeAltitudeAGLFt : MaxAltitudeAGLFt),
		MotorThrustLbf,
		bMotorIgnited ? TEXT("Y") : TEXT("-"),
		bDrogueDeployed ? TEXT("Y") : TEXT("-"),
		bMainDeployed ? TEXT("Y") : TEXT("-"));

	// Key 2 so it does not collide with the movement component's on-screen debug (key 1).
	GEngine->AddOnScreenDebugMessage(2, 0.0f, FColor::Yellow, Msg, false, FVector2D(1.1f, 1.1f));
}
