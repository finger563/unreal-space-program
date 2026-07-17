// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Templates/SubclassOf.h"
#include "RocketPawn.generated.h"

class UJSBSimMovementComponent;
class URocketFlightController;
class URocketTelemetryWidget;
class URocketUdpBridge;
class UStaticMeshComponent;
class UCameraComponent;

/**
 * A ready-to-fly amateur suborbital rocket pawn.
 *
 * Assembles the three pieces needed to launch, fly and recover a rocket on the Cesium globe:
 *   - a swappable visual mesh (defaults to a placeholder; assign your own static mesh or the
 *     generated rocket.glb),
 *   - a UJSBSimMovementComponent pre-configured for the "rocket" JSBSim model (no trim, motor
 *     off, starts on the pad), which does the ECEF <-> Unreal transform, and
 *   - a URocketFlightController that runs the launch/apogee/recovery state machine.
 *
 * It also provides a world-stabilized chase camera so the flight is watchable when possessed,
 * and (in Manual mode) input bindings to ignite and deploy chutes.
 *
 * Place one in the level, rotate it Pitch = +90 so the nose points at the sky, put it over
 * ground that has collision, then press Play.
 */
UCLASS()
class UNREALSPACEPROGRAM_API ARocketPawn : public APawn
{
	GENERATED_BODY()

public:
	ARocketPawn();

	/**
	 * Optional single-mesh override for the whole rocket (e.g. an imported rocket.glb or your
	 * own model, nose along +X spanning 0..277 cm). When a mesh is assigned here, the
	 * per-section visuals below are hidden and no separation animation plays.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<UStaticMeshComponent> RocketMesh;

	// --- Airframe sections ---
	// The rocket is visualized as three sections that separate during recovery:
	//   booster (with fins)  actor X [0..110] cm  - tail/motor section
	//   upper airframe       actor X [110..215]   - payload/recovery bay
	//   nose cone            actor X [215..277]
	// Each section has a Root scene component at its aft joint (attach custom meshes there,
	// authored tail-at-origin / nose along +X, and hide the placeholders), plus engine-basic-
	// shape placeholder meshes so everything is visible with zero imports.

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<USceneComponent> BoosterRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<UStaticMeshComponent> BoosterPlaceholder;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TArray<TObjectPtr<UStaticMeshComponent>> FinPlaceholders;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<USceneComponent> UpperRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<UStaticMeshComponent> UpperPlaceholder;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<USceneComponent> NoseRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Sections")
	TObjectPtr<UStaticMeshComponent> NosePlaceholder;

	// --- Parachute canopies (hidden until deployed, inflate on deploy) ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Recovery")
	TObjectPtr<USceneComponent> DrogueCanopyRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Recovery")
	TObjectPtr<UStaticMeshComponent> DrogueCanopyPlaceholder;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Recovery")
	TObjectPtr<USceneComponent> MainCanopyRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|Recovery")
	TObjectPtr<UStaticMeshComponent> MainCanopyPlaceholder;

	/** How fast the sections drift to their separated positions (VInterpTo speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Recovery", meta = (ClampMin = "0.1"))
	float SeparationAnimSpeed = 1.5f;

	/** How fast a canopy inflates after deployment (SInterpTo-style speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Recovery", meta = (ClampMin = "0.1"))
	float CanopyInflateSpeed = 3.0f;

	/** The JSBSim flight dynamics bridge (model = "rocket"). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<UJSBSimMovementComponent> JSBSim;

	/** The launch / apogee / recovery state machine and command surface. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<URocketFlightController> FlightController;

	/** Chase camera used when this pawn is possessed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<UCameraComponent> ChaseCamera;

	/** UDP hardware-in-the-loop bridge (disabled by default - enable bAutoStart to use). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<URocketUdpBridge> UdpBridge;

	// --- Telemetry widget ---

	/** Show the on-screen telemetry widget when this pawn is possessed by a player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Telemetry")
	bool bShowTelemetryWidget = true;

	/** Widget class to spawn. Leave unset to use the default code-built URocketTelemetryWidget;
	 *  set a Blueprint subclass here to use your own designer layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Telemetry")
	TSubclassOf<URocketTelemetryWidget> TelemetryWidgetClass;

	/** The live telemetry widget instance (created on possession). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Rocket|Telemetry")
	TObjectPtr<URocketTelemetryWidget> TelemetryWidget;

	// --- Chase camera framing (world-stabilized so it never tumbles with the rocket) ---
	// In-game controls: mouse wheel zooms; hold the right mouse button and drag to orbit
	// (horizontal = azimuth, vertical = height). Q/E also orbit, Z/C also zoom.

	/** Horizontal distance of the camera from the rocket, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera", meta = (ClampMin = "1.0"))
	float CameraDistanceMeters = 18.0f;

	/** Height of the camera above the rocket, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	float CameraHeightMeters = 5.0f;

	/** Compass direction (deg) the camera sits from the rocket (0 = to the north of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	float CameraAzimuthDeg = 135.0f;

	/** If true, the chase camera position/aim is updated every frame to track the rocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	bool bUseChaseCamera = true;

	/** Zoom factor per mouse-wheel notch (distance is multiplied/divided by this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera", meta = (ClampMin = "1.01"))
	float CameraZoomStep = 1.15f;

	/** Orbit speed, degrees per pixel of right-button mouse drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera", meta = (ClampMin = "0.01"))
	float CameraOrbitSpeed = 0.5f;

	/** Closest / farthest the camera can zoom, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	FVector2D CameraDistanceRangeMeters = FVector2D(3.0f, 300.0f);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

protected:
	// Input handlers (Manual-mode overrides; work any time for testing).
	void HandleIgnite();
	void HandleDeployDrogue();
	void HandleDeployMain();
	void HandleResetFlight();

	// Camera control handlers.
	void HandleZoomWheel(float AxisValue);
	void HandleZoomIn();
	void HandleZoomOut();
	void HandleOrbitStart();
	void HandleOrbitStop();
	void HandleOrbitMouseX(float AxisValue);
	void HandleOrbitMouseY(float AxisValue);
	void HandleOrbitLeft();
	void HandleOrbitRight();

	bool bOrbiting = false;

	void ApplyZoom(float Factor);

	void UpdateChaseCamera();
	void UpdateRecoveryVisuals(float DeltaSeconds);
	void UpdateCanopy(USceneComponent* CanopyRoot, bool bDeployed, float DeltaSeconds);
};
