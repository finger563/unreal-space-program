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

	/** Visual mesh for the rocket. Assign your own static mesh here, or import rocket.glb. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket")
	TObjectPtr<UStaticMeshComponent> RocketMesh;

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

	/** Horizontal distance of the camera from the rocket, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera", meta = (ClampMin = "1.0"))
	float CameraDistanceMeters = 45.0f;

	/** Height of the camera above the rocket, in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	float CameraHeightMeters = 12.0f;

	/** Compass direction (deg) the camera sits from the rocket (0 = to the north of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	float CameraAzimuthDeg = 135.0f;

	/** If true, the chase camera position/aim is updated every frame to track the rocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Camera")
	bool bUseChaseCamera = true;

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

	void UpdateChaseCamera();
};
