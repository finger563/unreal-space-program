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
	// origin, NOSE at +X 277 cm. With the actor pitched +90 deg the rocket stands on its tail
	// at the actor origin, so "place the actor origin on the pad" is physically correct.
	// StructuralFrameOrigin below maps JSBSim's structural frame (origin at the nose, X aft)
	// onto this same span - keep them in sync or the physics contacts will not be where the
	// mesh is. RocketMesh itself stays empty by default: the per-section visuals below are the
	// default look, and assigning a mesh here switches to single-body mode (sections hidden).

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// Booster section [0..110]: tube + 4 fins near the tail.
	BoosterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BoosterRoot"));
	BoosterRoot->SetupAttachment(Root);

	BoosterPlaceholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoosterPlaceholder"));
	BoosterPlaceholder->SetupAttachment(BoosterRoot);
	BoosterPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderMesh.Succeeded())
	{
		BoosterPlaceholder->SetStaticMesh(CylinderMesh.Object);
		BoosterPlaceholder->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); // cylinder Z -> +X
		BoosterPlaceholder->SetRelativeScale3D(FVector(0.152f, 0.152f, 1.10f));
		BoosterPlaceholder->SetRelativeLocation(FVector(55.0f, 0.0f, 0.0f));
	}

	for (int32 i = 0; i < 4; i++)
	{
		UStaticMeshComponent* Fin = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("FinPlaceholder%d"), i));
		Fin->SetupAttachment(BoosterRoot);
		Fin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (CubeMesh.Succeeded())
		{
			Fin->SetStaticMesh(CubeMesh.Object);
			const FRotator FinRoll(0.0f, 0.0f, 90.0f * i);
			Fin->SetRelativeRotation(FinRoll);
			Fin->SetRelativeLocation(FinRoll.RotateVector(FVector(22.0f, 0.0f, 15.5f))); // chord center 22, radial center 15.5
			Fin->SetRelativeScale3D(FVector(0.45f, 0.012f, 0.17f)); // 45cm chord, 1.2cm thick, 17cm span
		}
		FinPlaceholders.Add(Fin);
	}

	// Upper airframe [110..215].
	UpperRoot = CreateDefaultSubobject<USceneComponent>(TEXT("UpperRoot"));
	UpperRoot->SetupAttachment(Root);
	UpperRoot->SetRelativeLocation(FVector(110.0f, 0.0f, 0.0f));

	UpperPlaceholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperPlaceholder"));
	UpperPlaceholder->SetupAttachment(UpperRoot);
	UpperPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderMesh.Succeeded())
	{
		UpperPlaceholder->SetStaticMesh(CylinderMesh.Object);
		UpperPlaceholder->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		UpperPlaceholder->SetRelativeScale3D(FVector(0.152f, 0.152f, 1.05f));
		UpperPlaceholder->SetRelativeLocation(FVector(52.5f, 0.0f, 0.0f));
	}

	// Nose cone [215..277].
	NoseRoot = CreateDefaultSubobject<USceneComponent>(TEXT("NoseRoot"));
	NoseRoot->SetupAttachment(Root);
	NoseRoot->SetRelativeLocation(FVector(215.0f, 0.0f, 0.0f));

	NosePlaceholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NosePlaceholder"));
	NosePlaceholder->SetupAttachment(NoseRoot);
	NosePlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (ConeMesh.Succeeded())
	{
		NosePlaceholder->SetStaticMesh(ConeMesh.Object);
		// Pitch -90 maps local +Z to +X (pitch +90 would map it to -X and point the cone
		// apex INTO the body). The cylinders/spheres don't care, the cone does.
		NosePlaceholder->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // cone apex (local +Z) -> +X
		NosePlaceholder->SetRelativeScale3D(FVector(0.152f, 0.152f, 0.62f));
		NosePlaceholder->SetRelativeLocation(FVector(31.0f, 0.0f, 0.0f));
	}

	// Parachute canopies: flattened spheres, dome facing +X (up during descent). Hidden and
	// scaled to a packed size until deployment; UpdateCanopy() inflates them.
	DrogueCanopyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DrogueCanopyRoot"));
	DrogueCanopyRoot->SetupAttachment(Root);
	DrogueCanopyRoot->SetRelativeLocation(FVector(437.0f, 0.0f, 0.0f)); // ~1.6m of shock cord above the nose
	DrogueCanopyRoot->SetVisibility(false, true);

	DrogueCanopyPlaceholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DrogueCanopyPlaceholder"));
	DrogueCanopyPlaceholder->SetupAttachment(DrogueCanopyRoot);
	DrogueCanopyPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Visibility does NOT propagate from a parent to children attached later, so the
	// placeholder must be hidden explicitly - hiding only the root leaves the mesh visible
	// (including in the editor). UpdateCanopy() re-shows both with propagation on deploy.
	DrogueCanopyPlaceholder->SetVisibility(false);
	if (SphereMesh.Succeeded())
	{
		DrogueCanopyPlaceholder->SetStaticMesh(SphereMesh.Object);
		DrogueCanopyPlaceholder->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); // flatten axis -> +X
		DrogueCanopyPlaceholder->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.45f));   // ~0.9m drogue dome
	}

	MainCanopyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MainCanopyRoot"));
	MainCanopyRoot->SetupAttachment(Root);
	MainCanopyRoot->SetRelativeLocation(FVector(530.0f, 0.0f, 0.0f));
	MainCanopyRoot->SetVisibility(false, true);

	MainCanopyPlaceholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainCanopyPlaceholder"));
	MainCanopyPlaceholder->SetupAttachment(MainCanopyRoot);
	MainCanopyPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MainCanopyPlaceholder->SetVisibility(false); // see drogue placeholder comment
	if (SphereMesh.Succeeded())
	{
		MainCanopyPlaceholder->SetStaticMesh(SphereMesh.Object);
		MainCanopyPlaceholder->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		MainCanopyPlaceholder->SetRelativeScale3D(FVector(2.4f, 2.4f, 1.2f)); // ~2.4m main dome
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

void ARocketPawn::BeginPlay()
{
	Super::BeginPlay();

	// Single-mesh mode: a user-assigned RocketMesh replaces the per-section visuals.
	if (RocketMesh && RocketMesh->GetStaticMesh() != nullptr)
	{
		if (BoosterRoot) { BoosterRoot->SetVisibility(false, true); }
		if (UpperRoot) { UpperRoot->SetVisibility(false, true); }
		if (NoseRoot) { NoseRoot->SetVisibility(false, true); }
	}
}

void ARocketPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bUseChaseCamera)
	{
		UpdateChaseCamera();
	}

	UpdateRecoveryVisuals(DeltaSeconds);
}

void ARocketPawn::UpdateRecoveryVisuals(float DeltaSeconds)
{
	if (!FlightController)
	{
		return;
	}

	// The JSBSim body still represents the whole tethered recovery train; these offsets only
	// arrange the visual sections the way the real train hangs. After separation the chute at
	// the nose holds the body nose-up (+X up in actor space), so "down the shock cord" is -X.
	const bool bSeparated = FlightController->bAirframeSeparated;
	const bool bMainOut = FlightController->bMainDeployed;

	if (BoosterRoot)
	{
		// The booster slides down the shock cord below the upper airframe after separation.
		const FVector Target = bSeparated ? FVector(-160.0f, 0.0f, 0.0f) : FVector::ZeroVector;
		BoosterRoot->SetRelativeLocation(FMath::VInterpTo(BoosterRoot->GetRelativeLocation(), Target, DeltaSeconds, SeparationAnimSpeed));
	}

	if (NoseRoot)
	{
		// At main deploy the nose cone pops off the payload bay and dangles beside it.
		const FVector Target = bMainOut ? FVector(160.0f, 45.0f, 0.0f) : FVector(215.0f, 0.0f, 0.0f);
		NoseRoot->SetRelativeLocation(FMath::VInterpTo(NoseRoot->GetRelativeLocation(), Target, DeltaSeconds, SeparationAnimSpeed));
	}

	UpdateCanopy(DrogueCanopyRoot, FlightController->bDrogueDeployed, DeltaSeconds);
	UpdateCanopy(MainCanopyRoot, bMainOut, DeltaSeconds);
}

void ARocketPawn::UpdateCanopy(USceneComponent* CanopyRoot, bool bDeployed, float DeltaSeconds)
{
	if (!CanopyRoot)
	{
		return;
	}

	if (bDeployed)
	{
		if (!CanopyRoot->IsVisible())
		{
			CanopyRoot->SetVisibility(true, true);
			CanopyRoot->SetRelativeScale3D(FVector(0.1f)); // start packed, inflate below
		}
		const FVector Scale = FMath::VInterpTo(CanopyRoot->GetRelativeScale3D(), FVector::OneVector, DeltaSeconds, CanopyInflateSpeed);
		CanopyRoot->SetRelativeScale3D(Scale);
	}
	else if (CanopyRoot->IsVisible())
	{
		CanopyRoot->SetVisibility(false, true); // e.g. after ResetFlight
		CanopyRoot->SetRelativeScale3D(FVector(0.1f));
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

	// Chase camera: wheel (or Z/C) zooms, right-mouse drag (or Q/E) orbits.
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ARocketPawn::HandleZoomWheel);
	PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ARocketPawn::HandleZoomIn);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARocketPawn::HandleZoomOut);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ARocketPawn::HandleOrbitStart);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ARocketPawn::HandleOrbitStop);
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &ARocketPawn::HandleOrbitMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &ARocketPawn::HandleOrbitMouseY);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ARocketPawn::HandleOrbitLeft);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ARocketPawn::HandleOrbitRight);
}

void ARocketPawn::ApplyZoom(float Factor)
{
	CameraDistanceMeters = FMath::Clamp(CameraDistanceMeters * Factor,
		(float)CameraDistanceRangeMeters.X, (float)CameraDistanceRangeMeters.Y);
	// Keep the framing angle roughly constant while zooming.
	CameraHeightMeters *= Factor;
}

void ARocketPawn::HandleZoomWheel(float AxisValue)
{
	if (AxisValue > 0.0f)
	{
		ApplyZoom(1.0f / CameraZoomStep);
	}
	else if (AxisValue < 0.0f)
	{
		ApplyZoom(CameraZoomStep);
	}
}

void ARocketPawn::HandleZoomIn() { ApplyZoom(1.0f / CameraZoomStep); }
void ARocketPawn::HandleZoomOut() { ApplyZoom(CameraZoomStep); }
void ARocketPawn::HandleOrbitStart() { bOrbiting = true; }
void ARocketPawn::HandleOrbitStop() { bOrbiting = false; }

void ARocketPawn::HandleOrbitMouseX(float AxisValue)
{
	if (bOrbiting && AxisValue != 0.0f)
	{
		CameraAzimuthDeg = FMath::Fmod(CameraAzimuthDeg + AxisValue * CameraOrbitSpeed * 4.0f, 360.0f);
	}
}

void ARocketPawn::HandleOrbitMouseY(float AxisValue)
{
	if (bOrbiting && AxisValue != 0.0f)
	{
		// Drag up = raise the camera. Scale with distance so the motion feels consistent.
		CameraHeightMeters = FMath::Clamp(
			CameraHeightMeters + AxisValue * CameraOrbitSpeed * 0.06f * CameraDistanceMeters,
			-0.5f * CameraDistanceMeters, 3.0f * CameraDistanceMeters);
	}
}

void ARocketPawn::HandleOrbitLeft() { CameraAzimuthDeg = FMath::Fmod(CameraAzimuthDeg - 15.0f, 360.0f); }
void ARocketPawn::HandleOrbitRight() { CameraAzimuthDeg = FMath::Fmod(CameraAzimuthDeg + 15.0f, 360.0f); }

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
