// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketPawn.h"
#include "RocketFlightController.h"
#include "RocketTelemetryWidget.h"
#include "RocketUdpBridge.h"
#include "JSBSimMovementComponent.h"

#include "Blueprint/UserWidget.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ARocketPawn::ARocketPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root the pawn on a bare scene component. The JSBSim movement component drives THIS actor's
	// transform directly (it calls SetActorLocationAndRotation), so we keep the root simple.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	// --- Visual mesh (swappable) ---
	RocketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RocketMesh"));
	RocketMesh->SetupAttachment(Root);
	RocketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // JSBSim, not UE physics, owns collision response.

	// Frame convention: the rocket occupies actor-local X in [0 .. 277] cm - TAIL at the actor
	// origin, NOSE at +X 277 cm (the generated rocket.glb is authored this way). With the actor
	// pitched +90 deg the rocket stands on its tail at the actor origin, so "place the actor
	// origin on the pad" is physically correct. StructuralFrameOrigin below maps JSBSim's
	// structural frame (origin at the nose, X aft) onto this same span - keep them in sync or
	// the physics contacts will not be where the mesh is.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		RocketMesh->SetStaticMesh(CylinderMesh.Object);
		RocketMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); // cylinder axis (local Z) -> actor +X
		RocketMesh->SetRelativeScale3D(FVector(0.152f, 0.152f, 2.77f)); // ~6" diameter, ~109" long (meters)
		RocketMesh->SetRelativeLocation(FVector(138.5f, 0.0f, 0.0f));  // center the 277cm body on [0..277]
	}

	// --- JSBSim flight dynamics, configured for the rocket ---
	JSBSim = CreateDefaultSubobject<UJSBSimMovementComponent>(TEXT("JSBSimMovement"));
	JSBSim->AircraftModel = TEXT("rocket");
	// Structural origin (the NOSE, structural X pointing aft) sits at actor +X 277 cm, so
	// structural x maps to actor X = 277 - x: nose -> 277, tail (9.08 ft) -> 0. This aligns the
	// physics (CG, thruster, ground contacts) exactly onto the visual span above.
	JSBSim->StructuralFrameOrigin = FVector(276.9f, 0.0f, 0.0f); // 9.08 ft = 276.9 cm
	JSBSim->StartOnGround = true;
	JSBSim->bTrimOnStart = false;         // rockets have no aerodynamic trim to solve
	JSBSim->bStartWithEngineRunning = false; // stay inert until commanded to ignite
	JSBSim->bStartWithGearDown = false;
	JSBSim->DrawDebug = false;            // the flight controller draws its own concise HUD

	// --- Launch/recovery state machine ---
	FlightController = CreateDefaultSubobject<URocketFlightController>(TEXT("FlightController"));
	FlightController->bShowFlightHUD = false; // superseded by the UMG telemetry widget

	// --- HIL bridge (inert unless bAutoStart is ticked or StartBridge() is called) ---
	UdpBridge = CreateDefaultSubobject<URocketUdpBridge>(TEXT("UdpBridge"));

	// --- Chase camera (world transform overridden each tick so it never tumbles) ---
	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(Root);
	ChaseCamera->bUsePawnControlRotation = false;

	// Just press Play: possess automatically so the chase camera and input are live.
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ARocketPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (bShowTelemetryWidget && !TelemetryWidget)
	{
		if (APlayerController* PC = Cast<APlayerController>(NewController))
		{
			const TSubclassOf<URocketTelemetryWidget> WidgetClass =
				TelemetryWidgetClass ? TelemetryWidgetClass : TSubclassOf<URocketTelemetryWidget>(URocketTelemetryWidget::StaticClass());
			TelemetryWidget = CreateWidget<URocketTelemetryWidget>(PC, WidgetClass);
			if (TelemetryWidget)
			{
				TelemetryWidget->SetFlightController(FlightController);
				TelemetryWidget->AddToViewport(10);
			}
		}
	}
}

void ARocketPawn::UnPossessed()
{
	Super::UnPossessed();

	if (TelemetryWidget)
	{
		TelemetryWidget->RemoveFromParent();
		TelemetryWidget = nullptr;
	}
}

void ARocketPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bUseChaseCamera)
	{
		UpdateChaseCamera();
	}
}

void ARocketPawn::UpdateChaseCamera()
{
	if (!ChaseCamera)
	{
		return;
	}

	// Frame the rocket from a fixed world-space offset and always look at it. Because the offset
	// and aim are computed in world space (not relative to the spinning/pitching rocket body),
	// the view stays stable through the vertical launch and tumbling descent.
	const FVector RocketLocation = GetActorLocation();

	const float DistCm = CameraDistanceMeters * 100.0f;
	const float HeightCm = CameraHeightMeters * 100.0f;
	const FVector HorizontalDir = FRotator(0.0f, CameraAzimuthDeg, 0.0f).Vector();

	const FVector CameraLocation = RocketLocation + HorizontalDir * DistCm + FVector(0.0f, 0.0f, HeightCm);
	const FRotator LookAt = (RocketLocation - CameraLocation).Rotation();

	ChaseCamera->SetWorldLocationAndRotation(CameraLocation, LookAt);
}

void ARocketPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	// Direct key bindings (independent of the project's Enhanced Input mappings) so the rocket is
	// controllable out of the box. These are the manual overrides / HIL-style triggers.
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ARocketPawn::HandleIgnite);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &ARocketPawn::HandleDeployDrogue);
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ARocketPawn::HandleDeployMain);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARocketPawn::HandleResetFlight);
}

void ARocketPawn::HandleIgnite()
{
	if (FlightController)
	{
		FlightController->Ignite();
	}
}

void ARocketPawn::HandleDeployDrogue()
{
	if (FlightController)
	{
		FlightController->DeployDrogue();
	}
}

void ARocketPawn::HandleDeployMain()
{
	if (FlightController)
	{
		FlightController->DeployMain();
	}
}

void ARocketPawn::HandleResetFlight()
{
	if (FlightController)
	{
		FlightController->ResetFlight();
	}
}
