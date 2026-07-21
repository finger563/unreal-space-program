// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RocketFlightController.generated.h"

class UJSBSimMovementComponent;

/**
 * How the flight sequence is driven.
 */
UENUM(BlueprintType)
enum class ERocketFlightMode : uint8
{
	/** The full launch sequence runs automatically on Play (auto-ignite after a countdown). */
	Auto	UMETA(DisplayName = "Auto (sequence runs on Play)"),
	/** Nothing happens automatically. Ignition is driven by input / Blueprint / an external HIL script. */
	Manual	UMETA(DisplayName = "Manual (input / external driven)")
};

/**
 * The current phase of flight. Useful for HUDs, camera logic and animations.
 */
UENUM(BlueprintType)
enum class ERocketFlightPhase : uint8
{
	Prelaunch		UMETA(DisplayName = "Prelaunch (on pad)"),
	PoweredAscent	UMETA(DisplayName = "Powered Ascent (motor burning)"),
	CoastAscent		UMETA(DisplayName = "Coast Ascent (motor burned out)"),
	DrogueDescent	UMETA(DisplayName = "Drogue Descent"),
	MainDescent		UMETA(DisplayName = "Main Descent"),
	Landed			UMETA(DisplayName = "Landed")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRocketAltitudeEvent, float, AltitudeAGLFt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRocketPhaseChanged, ERocketFlightPhase, NewPhase);

/**
 * Drives an amateur suborbital rocket simulated by a sibling UJSBSimMovementComponent.
 *
 * This reproduces the state machine from the standalone jsbsim-rocket-test reference program:
 *   ignition -> liftoff -> powered ascent -> coast -> apogee -> drogue -> main -> landing.
 *
 * It works in two modes:
 *   - Auto:   press Play and the whole flight runs hands-off (like watching a real launch).
 *   - Manual: ignition/recovery are triggered by input, Blueprint, or an external script
 *             (e.g. hardware-in-the-loop), via the BlueprintCallable command functions below.
 *
 * Recovery (drogue + main chute) can be automatic in either mode via bAutoRecovery, or left
 * fully manual for testing / external control.
 *
 * The component reads state from, and issues commands to, the JSBSim model only through the
 * public surface of UJSBSimMovementComponent (EngineCommands + CommandConsole), so it does not
 * need the plugin's private internals.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNREALSPACEPROGRAM_API URocketFlightController : public UActorComponent
{
	GENERATED_BODY()

public:
	URocketFlightController();

	// ---------------------------------------------------------------------------------------
	// Configuration
	// ---------------------------------------------------------------------------------------

	/** Auto = runs on Play; Manual = driven by input / Blueprint / external HIL scripts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Config")
	ERocketFlightMode Mode = ERocketFlightMode::Auto;

	/** In Auto mode, seconds after BeginPlay before the motor is automatically ignited. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Config", meta = (ClampMin = "0.0", EditCondition = "Mode == ERocketFlightMode::Auto"))
	float IgnitionDelaySeconds = 2.0f;

	/** Index of the JSBSim engine to ignite (0 for a single-motor rocket). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Config")
	int32 EngineIndex = 0;

	/**
	 * If true, drogue + main chutes deploy automatically (at apogee / at MainDeployAltitudeAGLFt)
	 * regardless of Mode. Set false for fully-manual recovery (deploy via DeployDrogue/DeployMain).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Config")
	bool bAutoRecovery = true;

	/** Effective drag area (Cd*A, ft^2) applied when the drogue chute deploys. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Recovery")
	float DrogueDragAreaSqFt = 8.0f;

	/** Effective drag area (Cd*A, ft^2) applied when the main chute deploys. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Recovery")
	float MainDragAreaSqFt = 50.0f;

	/** Altitude above ground (ft) at which the main chute auto-deploys during descent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Recovery", meta = (ClampMin = "0.0"))
	float MainDeployAltitudeAGLFt = 500.0f;

	// ---------------------------------------------------------------------------------------
	// Wind & turbulence
	// ---------------------------------------------------------------------------------------
	// Applied to JSBSim's atmosphere every tick via the property interface. IMPORTANT: the
	// plugin's own per-frame wind path (UJSBSimMovementComponent::CopyToJSBSim) is commented
	// out, and its WindIntensityKts is initial-condition only - so without this, the sim runs
	// in dead-calm air regardless of any wind setting. Driving the atmosphere/wind-*-fps
	// properties here is what actually blows the rocket, on both ascent and descent, and it
	// flows into the airflow the recovery visuals read (ARocketPawn::SampleAirflow).

	/** Master switch. When false, wind and turbulence are explicitly zeroed in the FDM. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind")
	bool bEnableWind = true;

	/** Steady wind speed (knots) AT the reference altitude below. The altitude profile scales
	 *  this up higher and down lower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "0.0"))
	float WindSpeedKts = 12.0f;

	/** Compass heading (deg) the wind blows FROM - meteorological convention. 270 = a westerly
	 *  (out of the west, pushing the rocket east). 0 = N, 90 = E, 180 = S, 270 = W. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float WindHeadingDeg = 270.0f;

	/** Altitude AGL (ft) at which WindSpeedKts applies. The power-law profile is anchored here. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "1.0"))
	float WindReferenceAltitudeFt = 1000.0f;

	/** Wind-shear exponent for the altitude profile: speed = WindSpeedKts * (AGL/ref)^exponent.
	 *  0 = uniform wind at all altitudes; ~0.14 is typical open terrain; higher = more shear. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindShearExponent = 0.14f;

	/** Floor on the altitude profile so the surface wind never drops to zero, as a fraction of
	 *  WindSpeedKts. Also the wind the rocket feels on the pad. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindSurfaceFraction = 0.25f;

	/** Turbulence intensity, 0 = calm, 1 = strong gusts. Layered on top of the steady wind via
	 *  JSBSim's Culp turbulence model. ~0.2 reads as a light, gusty breeze. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TurbulenceIntensity = 0.2f;

	// --- Detection thresholds (sensible defaults; rarely need changing) ---

	/** In Auto mode, ignition waits until the rocket is settled on the pad (|vertical speed|
	 *  below this, ft/s). Prevents igniting a rocket that is still falling/bouncing after
	 *  being placed. Manual Ignite() calls are never gated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float MaxPadSpeedForAutoIgnitionFps = 3.0f;

	/** Liftoff requires climbing at least this fast (ft/s)... */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float LiftoffVerticalSpeedFps = 5.0f;

	/** ...AND having gained this much altitude (ft) above the ignition point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float LiftoffAltitudeGainFt = 10.0f;

	/** Apogee can only be declared after gaining at least this much altitude (ft) above the
	 *  ignition point (mirrors the reference sim's conservative gate). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float MinApogeeAltitudeGainFt = 200.0f;

	/** After liftoff, apogee is declared once vertical speed drops below this (ft/s, negative =
	 *  descending). -20 mirrors the reference sim's conservative threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection")
	float ApogeeVerticalSpeedThresholdFps = -20.0f;

	/** Below this AGL altitude (ft) and slow enough, the rocket is considered landed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float LandingAltitudeAGLFt = 4.0f;

	/** Below this vertical speed magnitude (ft/s) near the ground, the rocket is considered landed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Detection", meta = (ClampMin = "0.0"))
	float LandingSpeedThresholdFps = 4.0f;

	/** Draw a small on-screen flight-status readout during play. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Debug")
	bool bShowFlightHUD = true;

	/** Log a one-line telemetry snapshot (phase, altitude, VS, thrust) to the Output Log at a
	 *  fixed interval - invaluable for post-run debugging from Saved/Logs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Debug")
	bool bLogTelemetry = true;

	/** Seconds between telemetry log lines. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Debug", meta = (ClampMin = "0.1"))
	float TelemetryLogInterval = 1.0f;

	/**
	 * Optional explicit reference to the JSBSim movement component to drive. If left null, the
	 * first UJSBSimMovementComponent found on the owning actor is used.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Rocket|Config")
	TObjectPtr<UJSBSimMovementComponent> MovementComponent = nullptr;

	// ---------------------------------------------------------------------------------------
	// Live state (read-only telemetry)
	// ---------------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	ERocketFlightPhase Phase = ERocketFlightPhase::Prelaunch;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bMotorIgnited = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bLiftedOff = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bReachedApogee = false;

	/** True once the airframe has separated into its tethered recovery sections. Happens
	 *  automatically with drogue deployment, or via SeparateAirframe(). While separated, the
	 *  fin stability moments in the JSBSim model are disabled (systems/fins-effective = 0). */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bAirframeSeparated = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bDrogueDeployed = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bMainDeployed = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	bool bLanded = false;

	/** Seconds since BeginPlay. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float MissionTimeSeconds = 0.0f;

	/** Seconds since ignition (0 until ignited). */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float TimeSinceIgnitionSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float AltitudeAGLFt = 0.0f;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float AltitudeASLFt = 0.0f;

	/** Vertical speed, ft/s, positive = climbing. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float VerticalSpeedFps = 0.0f;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float MaxAltitudeAGLFt = 0.0f;

	/** Apogee AGL altitude captured at the moment apogee was detected. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float ApogeeAltitudeAGLFt = 0.0f;

	/** AGL altitude (ft) at the moment of ignition - liftoff/apogee gates are relative to this. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float IgnitionAltitudeAGLFt = 0.0f;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Rocket|State")
	float MotorThrustLbf = 0.0f;

	// ---------------------------------------------------------------------------------------
	// Events (bind from Blueprint / HUD / camera logic)
	// ---------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnIgnition;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnLiftoff;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnApogee;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnDrogueDeployed;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnMainDeployed;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketAltitudeEvent OnLanded;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FRocketPhaseChanged OnPhaseChanged;

	// ---------------------------------------------------------------------------------------
	// Command API - callable from Blueprint, input handlers, and external / HIL scripts
	// ---------------------------------------------------------------------------------------

	/** Ignite the motor (throttle to full, engine running). Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void Ignite();

	/** Cut the motor (throttle 0, engine not running). */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void ShutdownMotor();

	/** Set the motor throttle directly, 0..1 (useful for throttleable/liquid motors or HIL). */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void SetThrottle(float Throttle01);

	/** Separate the airframe into its tethered recovery sections. Disables the fin stability
	 *  moments in the FDM (the broken stack no longer weathercocks). Called automatically by
	 *  DeployDrogue; exposed separately for HIL / manual sequencing. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void SeparateAirframe();

	/** Deploy the drogue chute now (applies DrogueDragAreaSqFt). Separates the airframe first. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void DeployDrogue();

	/** Deploy the main chute now (applies MainDragAreaSqFt). */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void DeployMain();

	/** Directly set the drogue chute effective drag area (ft^2). 0 = stowed. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void SetDrogueDragArea(float AreaSqFt);

	/** Directly set the main chute effective drag area (ft^2). 0 = stowed. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void SetMainDragArea(float AreaSqFt);

	/** Reset the flight state machine back to Prelaunch (does not re-initialize JSBSim). */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Commands")
	void ResetFlight();

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Resolve MovementComponent (explicit or found on the owner). Returns null if unavailable. */
	UJSBSimMovementComponent* GetMovement();

	/** Push a drag area value to a JSBSim external_reactions parachute property. */
	void SetChuteAreaProperty(const FString& PropertyPath, float AreaSqFt);

	/** Configure JSBSim's turbulence model from TurbulenceIntensity. Called once, and whenever
	 *  wind is (re)initialized, since the turbulence TYPE only needs setting on change. */
	void ConfigureTurbulence();

	/** Push the steady wind vector for the current altitude into the FDM. Called every tick so
	 *  the altitude profile tracks the climb and descent. */
	void ApplyWind();

	/** True once turbulence has been configured this run (reset by ResetFlight). */
	bool bTurbulenceConfigured = false;

	void SetPhase(ERocketFlightPhase NewPhase);
	void UpdateTelemetry();
	void DrawHUD();
	void LogTelemetry();

	float TelemetryLogAccumulator = 0.0f;
	bool bWarnedWaitingForSettle = false;
};
