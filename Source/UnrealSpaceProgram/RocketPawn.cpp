// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketPawn.h"
#include "RocketFlightController.h"
#include "RocketTelemetryWidget.h"
#include "RocketUdpBridge.h"
#include "RocketGeometry.h"
#include "JSBSimMovementComponent.h"

#include "Blueprint/UserWidget.h"

#include "CableComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Engine materials used as bases for the generated meshes. BasicShapeMaterial and
	// EmissiveMeshMaterial both live under /Engine and are always cooked, so they are safe to
	// depend on in a packaged build (unlike anything under /Engine/EngineDebugMaterials).
	const TCHAR* const BasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* const EmissiveMaterialPath = TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial");

	/**
	 * Tint a MID without knowing which parameter name its base material actually uses. Setting
	 * a parameter that does not exist on a MID is a no-op in Unreal, not an error, so trying
	 * the common names is safe and keeps this working if the engine materials change.
	 */
	void SetMaterialColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color)
	{
		if (!Material)
		{
			return;
		}
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
		Material->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
	}

	void SetMaterialOpacity(UMaterialInstanceDynamic* Material, float Opacity)
	{
		if (!Material)
		{
			return;
		}
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Material->SetScalarParameterValue(TEXT("Alpha"), Opacity);
	}

	/** Create a MID over a base material loaded from Path, and tint it. */
	UMaterialInstanceDynamic* MakeTintedMaterial(UObject* Outer, const TCHAR* Path, const FLinearColor& Color)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!Base)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Outer);
		SetMaterialColor(Material, Color);
		return Material;
	}

	/**
	 * Append a thin "cross quad" line between two points: two perpendicular quads sharing the
	 * segment axis. Far cheaper than a tube and visually indistinguishable at shroud-line
	 * thickness, while still reading correctly from any viewing angle.
	 */
	void AppendLine(FRocketMeshData& Out, const FVector& Start, const FVector& End, float Width)
	{
		const FVector Axis = End - Start;
		if (Axis.IsNearlyZero())
		{
			return;
		}

		// Any two vectors perpendicular to the segment will do.
		FVector Perp1 = FVector::CrossProduct(Axis, FVector::XAxisVector);
		if (Perp1.IsNearlyZero())
		{
			Perp1 = FVector::CrossProduct(Axis, FVector::YAxisVector);
		}
		Perp1 = Perp1.GetSafeNormal() * (Width * 0.5f);
		const FVector Perp2 = FVector::CrossProduct(Axis, Perp1).GetSafeNormal() * (Width * 0.5f);

		for (int32 Plane = 0; Plane < 2; Plane++)
		{
			const FVector Offset = (Plane == 0) ? Perp1 : Perp2;
			const FVector Normal = ((Plane == 0) ? Perp2 : Perp1).GetSafeNormal();
			const int32 Base = Out.Vertices.Num();

			Out.Vertices.Add(Start - Offset);
			Out.Vertices.Add(Start + Offset);
			Out.Vertices.Add(End + Offset);
			Out.Vertices.Add(End - Offset);

			Out.UVs.Add(FVector2D(0, 0));
			Out.UVs.Add(FVector2D(1, 0));
			Out.UVs.Add(FVector2D(1, 1));
			Out.UVs.Add(FVector2D(0, 1));

			for (int32 i = 0; i < 4; i++)
			{
				Out.Normals.Add(Normal);
				Out.Tangents.Add(FProcMeshTangent(Axis.GetSafeNormal(), false));
			}

			Out.Triangles.Add(Base + 0);
			Out.Triangles.Add(Base + 1);
			Out.Triangles.Add(Base + 2);
			Out.Triangles.Add(Base + 0);
			Out.Triangles.Add(Base + 2);
			Out.Triangles.Add(Base + 3);
		}
	}

	/** A unit vector inside a cone about Axis, for randomizing ejection directions. */
	FVector RandomDirectionInCone(const FVector& Axis, float ConeAngleDeg)
	{
		if (ConeAngleDeg <= 0.0f)
		{
			return Axis.GetSafeNormal();
		}
		return FMath::VRandCone(Axis.GetSafeNormal(), FMath::DegreesToRadians(ConeAngleDeg));
	}
}

ARocketPawn::ARocketPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root the pawn on a bare scene component. The JSBSim movement component drives THIS actor's
	// transform directly (it calls SetActorLocationAndRotation), so we keep the root simple.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	// --- Visual mesh (swappable single-body override) ---
	RocketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RocketMesh"));
	RocketMesh->SetupAttachment(Root);
	RocketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // JSBSim, not UE physics, owns collision response.

	// Frame convention: the rocket occupies actor-local X in [0 .. 277] cm - TAIL at the actor
	// origin, NOSE at +X 277 cm. With the actor pitched +90 deg the rocket stands on its tail
	// at the actor origin, so "place the actor origin on the pad" is physically correct.
	// StructuralFrameOrigin below maps JSBSim's structural frame (origin at the nose, X aft)
	// onto this same span - keep them in sync or the physics contacts will not be where the
	// mesh is. RocketMesh itself stays empty by default: the generated sections below are the
	// default look, and assigning a mesh here switches to single-body mode (sections hidden).

	auto MakeSectionMesh = [this](const TCHAR* Name, USceneComponent* Parent) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->bUseAsyncCooking = true;
		// Generated every construction; nothing here is worth saving into the map.
		Mesh->SetFlags(RF_Transient);
		return Mesh;
	};

	// Booster section [0..110]: tube + boat tail + fins + launch lug.
	BoosterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BoosterRoot"));
	BoosterRoot->SetupAttachment(Root);
	BoosterMesh = MakeSectionMesh(TEXT("BoosterMesh"), BoosterRoot);

	// Upper airframe [110..215].
	UpperRoot = CreateDefaultSubobject<USceneComponent>(TEXT("UpperRoot"));
	UpperRoot->SetupAttachment(Root);
	UpperRoot->SetRelativeLocation(FVector(BoosterLengthCm, 0.0f, 0.0f));
	UpperMesh = MakeSectionMesh(TEXT("UpperMesh"), UpperRoot);

	// Nose cone [215..277].
	NoseRoot = CreateDefaultSubobject<USceneComponent>(TEXT("NoseRoot"));
	NoseRoot->SetupAttachment(Root);
	NoseRoot->SetRelativeLocation(FVector(BoosterLengthCm + UpperLengthCm, 0.0f, 0.0f));
	NoseMesh = MakeSectionMesh(TEXT("NoseMesh"), NoseRoot);

	// --- Parachutes ---
	// Each canopy root sits at the harness point on top of the payload bay and swings as a
	// pendulum; the dome hangs LineLength further along +X with shroud lines between.
	DrogueCanopyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DrogueCanopyRoot"));
	DrogueCanopyRoot->SetupAttachment(UpperRoot);
	DrogueCanopyRoot->SetRelativeLocation(FVector(UpperLengthCm, 0.0f, 0.0f));
	DrogueCanopyRoot->SetVisibility(false, true);

	DrogueCanopyMesh = MakeSectionMesh(TEXT("DrogueCanopyMesh"), DrogueCanopyRoot);
	DrogueShroudMesh = MakeSectionMesh(TEXT("DrogueShroudMesh"), DrogueCanopyRoot);

	MainCanopyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MainCanopyRoot"));
	MainCanopyRoot->SetupAttachment(UpperRoot);
	MainCanopyRoot->SetRelativeLocation(FVector(UpperLengthCm, 0.0f, 0.0f));
	MainCanopyRoot->SetVisibility(false, true);

	MainCanopyMesh = MakeSectionMesh(TEXT("MainCanopyMesh"), MainCanopyRoot);
	MainShroudMesh = MakeSectionMesh(TEXT("MainShroudMesh"), MainCanopyRoot);

	// Visibility does NOT propagate to children attached after the parent was hidden, so each
	// generated mesh is hidden explicitly here. UpdateCanopies() re-shows them with propagation.
	DrogueCanopyMesh->SetVisibility(false);
	DrogueShroudMesh->SetVisibility(false);
	MainCanopyMesh->SetVisibility(false);
	MainShroudMesh->SetVisibility(false);

	// --- Tethers ---
	ShockCord = CreateDefaultSubobject<UCableComponent>(TEXT("ShockCord"));
	ShockCord->SetupAttachment(UpperRoot);
	ShockCord->SetVisibility(false);
	ShockCord->CableLength = ShockCordLengthCm;
	ShockCord->NumSegments = 12;
	ShockCord->SolverIterations = 8;
	ShockCord->CableWidth = CableWidthCm;
	ShockCord->NumSides = 4;
	ShockCord->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NoseTether = CreateDefaultSubobject<UCableComponent>(TEXT("NoseTether"));
	NoseTether->SetupAttachment(UpperRoot);
	NoseTether->SetVisibility(false);
	NoseTether->CableLength = NoseTetherLengthCm;
	NoseTether->NumSegments = 10;
	NoseTether->SolverIterations = 8;
	NoseTether->CableWidth = CableWidthCm;
	NoseTether->NumSides = 4;
	NoseTether->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// --- Effects ---
	ExhaustFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ExhaustFX"));
	ExhaustFX->SetupAttachment(BoosterRoot);
	// Plume points aft (-X). Niagara systems are authored emitting along +X, so face it aft.
	ExhaustFX->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	ExhaustFX->bAutoActivate = false;

	ExhaustPlumeMesh = MakeSectionMesh(TEXT("ExhaustPlumeMesh"), BoosterRoot);
	ExhaustPlumeMesh->SetVisibility(false);
	ExhaustPlumeMesh->SetCastShadow(false);

	SmokePuffs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SmokePuffs"));
	SmokePuffs->SetupAttachment(Root);
	SmokePuffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SmokePuffs->SetCastShadow(false);
	// Puffs must hang in the air where they were emitted rather than ride along with the
	// rocket, so the component ignores its parent's transform and instances are placed in
	// world coordinates.
	SmokePuffs->SetUsingAbsoluteLocation(true);
	SmokePuffs->SetUsingAbsoluteRotation(true);
	SmokePuffs->SetUsingAbsoluteScale(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		SmokePuffs->SetStaticMesh(SphereMesh.Object);
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

bool ARocketPawn::IsSingleMeshMode() const
{
	return RocketMesh && RocketMesh->GetStaticMesh() != nullptr;
}

// ---------------------------------------------------------------------------------------------
// Construction / geometry
// ---------------------------------------------------------------------------------------------

void ARocketPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Keep the section roots consistent with the (editable) section lengths.
	if (UpperRoot)
	{
		UpperRoot->SetRelativeLocation(FVector(BoosterLengthCm, 0.0f, 0.0f));
	}
	if (NoseRoot)
	{
		NoseRoot->SetRelativeLocation(FVector(BoosterLengthCm + UpperLengthCm, 0.0f, 0.0f));
	}
	if (DrogueCanopyRoot)
	{
		DrogueCanopyRoot->SetRelativeLocation(FVector(UpperLengthCm, 0.0f, 0.0f));
	}
	if (MainCanopyRoot)
	{
		MainCanopyRoot->SetRelativeLocation(FVector(UpperLengthCm, 0.0f, 0.0f));
	}

	BuildAirframeGeometry();
	ApplySectionMaterials();
}

void ARocketPawn::BuildAirframeGeometry()
{
	// --- Booster: body tube + boat tail + fins + launch lug, welded into one mesh so the whole
	//     section is a single draw call.
	if (BoosterMesh)
	{
		FRocketMeshData Booster;

		const float TailLength = FMath::Clamp(BoatTailLengthCm, 0.0f, BoosterLengthCm * 0.5f);
		if (TailLength > 0.0f)
		{
			FRocketMeshData BoatTail;
			RocketGeometry::BuildTube(BoatTail, BoatTailRadiusCm, BodyRadiusCm, TailLength, 24, true, false);
			Booster.Append(BoatTail);
		}

		FRocketMeshData Tube;
		RocketGeometry::BuildTube(Tube, BodyRadiusCm, BodyRadiusCm, BoosterLengthCm - TailLength, 24, TailLength <= 0.0f, false);
		for (FVector& Vertex : Tube.Vertices)
		{
			Vertex.X += TailLength;
		}
		Booster.Append(Tube);

		// Fins sit just forward of the boat tail, spaced evenly around the body.
		for (int32 i = 0; i < FMath::Max(FinCount, 3); i++)
		{
			FRocketMeshData Fin;
			RocketGeometry::BuildFin(Fin, FinRootChordCm, FinTipChordCm, FinSpanCm, FinSweepCm, FinThicknessCm, BodyRadiusCm);

			const FRotator Roll(0.0f, 0.0f, (360.0f * i) / FMath::Max(FinCount, 3));
			for (int32 v = 0; v < Fin.Vertices.Num(); v++)
			{
				Fin.Vertices[v] = Roll.RotateVector(Fin.Vertices[v]) + FVector(TailLength, 0.0f, 0.0f);
				Fin.Normals[v] = Roll.RotateVector(Fin.Normals[v]);
			}
			Booster.Append(Fin);
		}

		// Launch lug at roughly the section's mid-point.
		FRocketMeshData Lug;
		RocketGeometry::BuildLaunchLug(Lug, 1.1f, 14.0f, BodyRadiusCm);
		for (FVector& Vertex : Lug.Vertices)
		{
			Vertex.X += BoosterLengthCm * 0.55f;
		}
		Booster.Append(Lug);

		BoosterMesh->ClearAllMeshSections();
		Booster.Apply(BoosterMesh);
	}

	// --- Upper airframe: plain tube, capped at both ends so it reads as an open bay after the
	//     nose cone leaves.
	if (UpperMesh)
	{
		FRocketMeshData Upper;
		RocketGeometry::BuildTube(Upper, BodyRadiusCm, BodyRadiusCm, UpperLengthCm, 24, true, true);
		UpperMesh->ClearAllMeshSections();
		Upper.Apply(UpperMesh);
	}

	// --- Nose cone: tangent ogive.
	if (NoseMesh)
	{
		FRocketMeshData Nose;
		RocketGeometry::BuildTangentOgive(Nose, BodyRadiusCm, NoseLengthCm, 24, 20, true);
		NoseMesh->ClearAllMeshSections();
		Nose.Apply(NoseMesh);
	}

	// --- Exhaust plume (code-built fallback).
	if (ExhaustPlumeMesh)
	{
		FRocketMeshData Plume;
		RocketGeometry::BuildPlume(Plume, BoatTailRadiusCm * 0.55f, PlumeLengthCm);
		ExhaustPlumeMesh->ClearAllMeshSections();
		Plume.Apply(ExhaustPlumeMesh);
	}

	// Canopies start packed; UpdateCanopies() rebuilds them as they inflate.
	BuildCanopyGeometry(DrogueCanopyMesh, DrogueShroudMesh, DrogueCanopyRadiusCm, DrogueLineLengthCm, DrogueGoreCount, 0.0f);
	BuildCanopyGeometry(MainCanopyMesh, MainShroudMesh, MainCanopyRadiusCm, MainLineLengthCm, MainGoreCount, 0.0f);
	DrogueBuiltInflation = 0.0f;
	MainBuiltInflation = 0.0f;
}

void ARocketPawn::BuildCanopyGeometry(
	UProceduralMeshComponent* CanopyMesh,
	UProceduralMeshComponent* ShroudMesh,
	float Radius,
	float LineLength,
	int32 GoreCount,
	float Inflation)
{
	GoreCount = FMath::Max(GoreCount, 3);
	Inflation = FMath::Max(Inflation, 0.0f);

	// A packed canopy is a narrow bundle at the end of its lines; an inflated one is a wide,
	// shallow dome. Interpolating both radius and depth (rather than just scaling) is what makes
	// the opening read as fabric filling with air instead of a balloon growing.
	const float PackedRadius = Radius * 0.06f;
	const float CurrentRadius = FMath::Lerp(PackedRadius, Radius, FMath::Min(Inflation, 1.5f));
	// Deep and narrow when packed, shallow and wide when open.
	const float CurrentDepth = FMath::Lerp(Radius * 0.5f, Radius * 0.62f, FMath::Min(Inflation, 1.0f));

	// Lines shorten slightly as the canopy spreads, since the skirt moves outward.
	const float CurrentLineLength = LineLength * FMath::Lerp(1.0f, 0.94f, FMath::Min(Inflation, 1.0f));

	if (CanopyMesh)
	{
		FRocketMeshData Canopy;
		RocketGeometry::BuildGoredCanopy(
			Canopy,
			CurrentRadius,
			CurrentDepth,
			GoreCount,
			10,
			/*ScallopDepth=*/0.12f * FMath::Min(Inflation, 1.0f),
			/*SpillHoleRadius=*/0.08f);

		// Lift the dome to the top of the shroud lines.
		for (FVector& Vertex : Canopy.Vertices)
		{
			Vertex.X += CurrentLineLength;
		}

		CanopyMesh->ClearAllMeshSections();
		Canopy.Apply(CanopyMesh);
	}

	if (ShroudMesh)
	{
		FRocketMeshData Shrouds;

		// One line per gore, running from the confluence point at the harness (origin) out to
		// each skirt attachment. Taut lines, so straight segments are correct here - the slack
		// tethers between airframe sections are the ones that need real cable simulation.
		for (int32 i = 0; i < GoreCount; i++)
		{
			const float Angle = (2.0f * PI * i) / GoreCount;
			const FVector SkirtPoint(
				CurrentLineLength,
				CurrentRadius * FMath::Cos(Angle),
				CurrentRadius * FMath::Sin(Angle));

			AppendLine(Shrouds, FVector::ZeroVector, SkirtPoint, CableWidthCm * 0.6f);
		}

		ShroudMesh->ClearAllMeshSections();
		Shrouds.Apply(ShroudMesh);
	}
}

void ARocketPawn::ApplySectionMaterials()
{
	auto Tint = [this](UProceduralMeshComponent* Mesh, const FLinearColor& Color)
	{
		if (!Mesh)
		{
			return;
		}
		if (UMaterialInstanceDynamic* Material = MakeTintedMaterial(this, BasicMaterialPath, Color))
		{
			Mesh->SetMaterial(0, Material);
		}
	};

	Tint(BoosterMesh, BoosterColor);
	Tint(UpperMesh, UpperColor);
	Tint(NoseMesh, NoseColor);
	Tint(DrogueCanopyMesh, DrogueColor);
	Tint(MainCanopyMesh, MainColor);
	Tint(DrogueShroudMesh, CableColor);
	Tint(MainShroudMesh, CableColor);

	// Cables get the same treatment so they do not render as untextured white.
	if (UMaterialInstanceDynamic* CordMaterial = MakeTintedMaterial(this, BasicMaterialPath, CableColor))
	{
		if (ShockCord) { ShockCord->SetMaterial(0, CordMaterial); }
		if (NoseTether) { NoseTether->SetMaterial(0, CordMaterial); }
	}

	// Plume: emissive so it glows against the sky. Falls back to the basic material if the
	// emissive one is unavailable for any reason.
	PlumeMaterial = MakeTintedMaterial(this, EmissiveMaterialPath, PlumeColor);
	if (!PlumeMaterial)
	{
		PlumeMaterial = MakeTintedMaterial(this, BasicMaterialPath, PlumeColor);
	}
	if (ExhaustPlumeMesh && PlumeMaterial)
	{
		ExhaustPlumeMesh->SetMaterial(0, PlumeMaterial);
	}

	SmokeMaterial = MakeTintedMaterial(this, BasicMaterialPath, FLinearColor(0.55f, 0.55f, 0.57f));
	if (SmokePuffs && SmokeMaterial)
	{
		SmokePuffs->SetMaterial(0, SmokeMaterial);
	}
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void ARocketPawn::BeginPlay()
{
	Super::BeginPlay();

	// Single-mesh mode: a user-assigned RocketMesh replaces the generated section visuals.
	if (IsSingleMeshMode())
	{
		if (BoosterRoot) { BoosterRoot->SetVisibility(false, true); }
		if (UpperRoot) { UpperRoot->SetVisibility(false, true); }
		if (NoseRoot) { NoseRoot->SetVisibility(false, true); }
	}

	// Cable endpoints can only be resolved once the components exist in a live world, so they
	// are wired here rather than in the constructor.
	if (ShockCord && BoosterRoot)
	{
		ShockCord->SetAttachEndToComponent(BoosterRoot, NAME_None);
		ShockCord->EndLocation = FVector(BoosterLengthCm * 0.9f, 0.0f, 0.0f);
		ShockCord->CableLength = ShockCordLengthCm;
		ShockCord->CableWidth = CableWidthCm;
	}
	if (NoseTether && NoseRoot)
	{
		NoseTether->SetAttachEndToComponent(NoseRoot, NAME_None);
		NoseTether->EndLocation = FVector::ZeroVector;
		NoseTether->CableLength = NoseTetherLengthCm;
		NoseTether->CableWidth = CableWidthCm;
	}

	// Assign any Niagara overrides. Slots left unset keep the code-built fallbacks.
	if (ExhaustFX && ExhaustEffect)
	{
		ExhaustFX->SetAsset(ExhaustEffect);
	}

	SmokePuffSpawnTimes.Init(-1.0f, FMath::Max(MaxSmokePuffs, 0));
	SmokePuffWorldLocations.Init(FVector::ZeroVector, FMath::Max(MaxSmokePuffs, 0));

	ResetVisuals();
}

void ARocketPawn::ResetVisuals()
{
	BoosterMotion.Reset();
	NoseMotion.Reset();

	DrogueInflation = 0.0f;
	MainInflation = 0.0f;
	DrogueDeployTime = -1.0f;
	MainDeployTime = -1.0f;

	bWasSeparated = false;
	bWasMainDeployed = false;
	bWasLanded = false;

	if (BoosterRoot) { BoosterRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator); }
	if (NoseRoot)
	{
		NoseRoot->SetRelativeLocationAndRotation(
			FVector(BoosterLengthCm + UpperLengthCm, 0.0f, 0.0f), FRotator::ZeroRotator);
	}

	if (DrogueCanopyRoot) { DrogueCanopyRoot->SetVisibility(false, true); }
	if (MainCanopyRoot) { MainCanopyRoot->SetVisibility(false, true); }
	if (ShockCord) { ShockCord->SetVisibility(false); }
	if (NoseTether) { NoseTether->SetVisibility(false); }
	if (ExhaustPlumeMesh) { ExhaustPlumeMesh->SetVisibility(false); }

	if (SmokePuffs)
	{
		SmokePuffs->ClearInstances();
	}
	NextSmokePuff = 0;
	SmokePuffTimer = 0.0f;
	for (float& SpawnTime : SmokePuffSpawnTimes)
	{
		SpawnTime = -1.0f;
	}
}

void ARocketPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bUseChaseCamera)
	{
		UpdateChaseCamera();
	}

	if (!IsSingleMeshMode())
	{
		UpdateSeparation(DeltaSeconds);
		UpdateCanopies(DeltaSeconds);
		UpdateCables();
	}

	UpdateExhaust(DeltaSeconds);
	UpdateSmoke(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------
// Separation dynamics
// ---------------------------------------------------------------------------------------------

void ARocketPawn::UpdateSeparation(float DeltaSeconds)
{
	if (!FlightController)
	{
		return;
	}

	const bool bSeparated = FlightController->bAirframeSeparated;
	const bool bMainOut = FlightController->bMainDeployed;

	// ResetFlight() clears the controller's state; mirror that on the visuals.
	if (bWasSeparated && !bSeparated)
	{
		ResetVisuals();
		return;
	}

	// --- Booster release: kicked aft down the shock cord at separation. ---
	if (BoosterRoot)
	{
		if (bSeparated && !BoosterMotion.bReleased)
		{
			BoosterMotion.bReleased = true;
			// Aft is -X in the pawn frame; spread it so runs are not identical.
			const FVector Direction = RandomDirectionInCone(-FVector::XAxisVector, SeparationConeAngleDeg);
			BoosterMotion.Velocity = Direction * BoosterEjectSpeed;
			BoosterMotion.AngularVelocity = FVector(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)) * SeparationTumbleRate;

			if (SeparationEffect)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					this, SeparationEffect, UpperRoot->GetComponentLocation(), UpperRoot->GetComponentRotation());
			}
		}

		if (BoosterMotion.bReleased)
		{
			// Anchored at the payload bay, settling aft down the cord.
			BoosterMotion.Integrate(
				DeltaSeconds,
				/*Anchor=*/FVector::ZeroVector,
				/*CordLength=*/ShockCordLengthCm,
				/*SettleDir=*/-FVector::XAxisVector,
				SectionSettleAccel,
				SectionLinearDamping,
				SectionAngularDamping);

			BoosterRoot->SetRelativeLocationAndRotation(BoosterMotion.Position, BoosterMotion.Rotation);
		}
	}

	// --- Nose cone release: blown off by the ejection charge at main deployment. ---
	if (NoseRoot)
	{
		const FVector StowedNose(BoosterLengthCm + UpperLengthCm, 0.0f, 0.0f);

		if (bMainOut && !NoseMotion.bReleased)
		{
			NoseMotion.bReleased = true;
			// Forward and off to one side, the way an ejection charge actually throws it.
			const FVector Direction = RandomDirectionInCone(FVector::XAxisVector, SeparationConeAngleDeg * 1.8f);
			NoseMotion.Velocity = Direction * NoseEjectSpeed;
			NoseMotion.AngularVelocity = FVector(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)) * SeparationTumbleRate * 1.4f;
		}

		if (NoseMotion.bReleased)
		{
			// The nose dangles from its tether off the top of the payload bay. Both the anchor
			// and the settle direction are expressed relative to the stowed position, which is
			// what NoseMotion.Position offsets from.
			NoseMotion.Integrate(
				DeltaSeconds,
				/*Anchor=*/FVector::ZeroVector,
				/*CordLength=*/NoseTetherLengthCm,
				/*SettleDir=*/-FVector::XAxisVector,
				SectionSettleAccel * 0.8f,
				SectionLinearDamping,
				SectionAngularDamping);

			NoseRoot->SetRelativeLocationAndRotation(StowedNose + NoseMotion.Position, NoseMotion.Rotation);
		}
	}

	// --- One-shot landing effect. ---
	if (FlightController->bLanded && !bWasLanded)
	{
		bWasLanded = true;
		if (LandingEffect)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this, LandingEffect, GetActorLocation(), FRotator::ZeroRotator);
		}
	}

	bWasSeparated = bSeparated;
	bWasMainDeployed = bMainOut;
}

// ---------------------------------------------------------------------------------------------
// Canopies
// ---------------------------------------------------------------------------------------------

void ARocketPawn::UpdateCanopies(float DeltaSeconds)
{
	if (!FlightController)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// One canopy's worth of update, shared by drogue and main.
	auto Update = [this, Now, DeltaSeconds](
		USceneComponent* CanopyRoot,
		UProceduralMeshComponent* CanopyMesh,
		UProceduralMeshComponent* ShroudMesh,
		bool bDeployed,
		float Radius,
		float LineLength,
		int32 GoreCount,
		float& Inflation,
		float& BuiltInflation,
		float& DeployTime)
	{
		if (!CanopyRoot)
		{
			return;
		}

		if (!bDeployed)
		{
			if (CanopyRoot->IsVisible())
			{
				CanopyRoot->SetVisibility(false, true);
				Inflation = 0.0f;
				DeployTime = -1.0f;
			}
			return;
		}

		if (DeployTime < 0.0f)
		{
			DeployTime = Now;
			CanopyRoot->SetVisibility(true, true);
		}

		const float SinceDeploy = Now - DeployTime;

		// Opening shock: overshoot past full size, then settle back. This is the single most
		// recognizable part of a parachute opening, so it is worth modelling explicitly rather
		// than just lerping to 1.
		const float T = FMath::Clamp(SinceDeploy / FMath::Max(CanopyInflateSeconds, 0.01f), 0.0f, 1.0f);
		const float Eased = 1.0f - FMath::Pow(1.0f - T, 3.0f); // fast open, gentle finish
		const float Overshoot = CanopyOpeningOvershoot * FMath::Sin(T * PI) * (1.0f - T * 0.4f);
		Inflation = Eased + Overshoot;

		// Rebuilding the mesh is not free, so only regenerate when the shape has actually moved.
		if (FMath::Abs(Inflation - BuiltInflation) > 0.02f)
		{
			BuildCanopyGeometry(CanopyMesh, ShroudMesh, Radius, LineLength, GoreCount, Inflation);
			BuiltInflation = Inflation;
		}

		// Pendulum swing about the harness point, decaying as the descent stabilizes.
		const float Decay = FMath::Exp(-SinceDeploy * 0.12f);
		const float Phase = (2.0f * PI * SinceDeploy) / FMath::Max(CanopySwingPeriodSeconds, 0.2f);
		const float Swing = CanopySwingAngleDeg * Decay;

		// Two axes slightly out of phase gives a lazy conical swing rather than a flat pendulum.
		CanopyRoot->SetRelativeRotation(FRotator(
			Swing * FMath::Sin(Phase),
			0.0f,
			Swing * FMath::Sin(Phase * 0.85f + 1.1f)));
	};

	Update(DrogueCanopyRoot, DrogueCanopyMesh, DrogueShroudMesh, FlightController->bDrogueDeployed,
		DrogueCanopyRadiusCm, DrogueLineLengthCm, DrogueGoreCount,
		DrogueInflation, DrogueBuiltInflation, DrogueDeployTime);

	Update(MainCanopyRoot, MainCanopyMesh, MainShroudMesh, FlightController->bMainDeployed,
		MainCanopyRadiusCm, MainLineLengthCm, MainGoreCount,
		MainInflation, MainBuiltInflation, MainDeployTime);
}

void ARocketPawn::UpdateCables()
{
	if (!FlightController)
	{
		return;
	}

	// Cables only exist once the pieces they connect have actually come apart.
	if (ShockCord)
	{
		ShockCord->SetVisibility(FlightController->bAirframeSeparated);
	}
	if (NoseTether)
	{
		NoseTether->SetVisibility(FlightController->bMainDeployed);
	}
}

// ---------------------------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------------------------

void ARocketPawn::UpdateExhaust(float DeltaSeconds)
{
	if (!FlightController)
	{
		return;
	}

	// Thrust drives everything, so the plume dies away with the motor instead of cutting out.
	const float Thrust = FlightController->MotorThrustLbf;
	const float ThrustFraction = FMath::Clamp(Thrust / FMath::Max(PlumeReferenceThrustLbf, 1.0f), 0.0f, 1.5f);
	const bool bBurning = Thrust > 0.5f;

	// Niagara override, when one is assigned.
	if (ExhaustFX && ExhaustEffect)
	{
		if (bBurning && !ExhaustFX->IsActive())
		{
			ExhaustFX->Activate();
		}
		else if (!bBurning && ExhaustFX->IsActive())
		{
			ExhaustFX->Deactivate();
		}

		if (bBurning)
		{
			// Conventional parameter names; harmless if the assigned system does not define them.
			ExhaustFX->SetFloatParameter(TEXT("ThrustFraction"), ThrustFraction);
			ExhaustFX->SetFloatParameter(TEXT("Thrust"), Thrust);
		}
	}

	// Code-built fallback plume, used only when no Niagara system is assigned.
	if (ExhaustPlumeMesh)
	{
		const bool bUseFallback = (ExhaustEffect == nullptr) && bBurning && !IsSingleMeshMode();
		ExhaustPlumeMesh->SetVisibility(bUseFallback);

		if (bUseFallback)
		{
			// Flicker so the plume is not a static cone. Scaled along X only, so the plume
			// lengthens with thrust while its throat stays welded to the nozzle.
			const float Flicker = 1.0f + 0.08f * FMath::Sin(GetWorld() ? GetWorld()->GetTimeSeconds() * 47.0f : 0.0f);
			ExhaustPlumeMesh->SetRelativeScale3D(FVector(
				ThrustFraction * Flicker,
				FMath::Sqrt(ThrustFraction) * Flicker,
				FMath::Sqrt(ThrustFraction) * Flicker));

			// Hotter and whiter at full thrust, dropping to a dull orange as it tails off.
			if (PlumeMaterial)
			{
				const FLinearColor Hot = FLinearColor::White * 2.5f;
				SetMaterialColor(PlumeMaterial, FMath::Lerp(PlumeColor, Hot, FMath::Clamp(ThrustFraction - 0.6f, 0.0f, 1.0f)));
			}
		}
	}
}

void ARocketPawn::UpdateSmoke(float DeltaSeconds)
{
	if (!SmokePuffs || !FlightController || MaxSmokePuffs <= 0)
	{
		return;
	}

	// The smoke trail is part of the code-built fallback: an assigned Niagara exhaust system is
	// expected to emit its own smoke.
	if (ExhaustEffect != nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const bool bBurning = FlightController->MotorThrustLbf > 0.5f;

	// --- Emit ---
	if (bBurning)
	{
		SmokePuffTimer -= DeltaSeconds;
		if (SmokePuffTimer <= 0.0f)
		{
			SmokePuffTimer = SmokePuffInterval;

			// Emit just aft of the nozzle, with a little scatter.
			const FVector NozzleWorld = BoosterRoot
				? BoosterRoot->GetComponentTransform().TransformPosition(FVector(-10.0f, 0.0f, 0.0f))
				: GetActorLocation();
			const FVector Jitter = FMath::VRand() * FMath::FRandRange(0.0f, 8.0f);
			const FVector SpawnLocation = NozzleWorld + Jitter;

			const int32 Index = NextSmokePuff % MaxSmokePuffs;
			NextSmokePuff++;

			SmokePuffSpawnTimes[Index] = Now;
			SmokePuffWorldLocations[Index] = SpawnLocation;

			const FTransform PuffTransform(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f), SpawnLocation, FVector(0.05f));

			// Grow the instance array on demand, then recycle in a ring.
			if (SmokePuffs->GetInstanceCount() <= Index)
			{
				SmokePuffs->AddInstance(PuffTransform, /*bWorldSpace=*/true);
			}
			else
			{
				SmokePuffs->UpdateInstanceTransform(Index, PuffTransform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false);
			}
		}
	}

	// --- Age ---
	// Without an authored material there is no per-instance opacity to fade, so puffs expand
	// and then shrink back to nothing. Reads acceptably as dissipating smoke and needs no assets.
	const int32 LiveCount = SmokePuffs->GetInstanceCount();
	bool bDirty = false;

	for (int32 i = 0; i < LiveCount && i < SmokePuffSpawnTimes.Num(); i++)
	{
		const float SpawnTime = SmokePuffSpawnTimes[i];
		if (SpawnTime < 0.0f)
		{
			continue;
		}

		const float Age = (Now - SpawnTime) / FMath::Max(SmokePuffLifetime, 0.1f);
		if (Age >= 1.0f)
		{
			// Retired: collapse to zero scale so it disappears without shuffling the array.
			SmokePuffSpawnTimes[i] = -1.0f;
			SmokePuffs->UpdateInstanceTransform(
				i, FTransform(FQuat::Identity, SmokePuffWorldLocations[i], FVector::ZeroVector),
				true, false);
			bDirty = true;
			continue;
		}

		// Billow out quickly, then shrink away over the tail of the lifetime.
		const float Growth = FMath::Sqrt(Age) * 60.0f + 4.0f;
		const float Fade = FMath::Clamp(1.0f - FMath::Pow(Age, 2.5f), 0.0f, 1.0f);
		const float Scale = (Growth * Fade) / 50.0f; // engine sphere is 50 cm radius at scale 1

		// Puffs drift slowly upward and outward as they dissipate.
		const FVector Drift = FVector(0.0f, 0.0f, Age * 40.0f);

		SmokePuffs->UpdateInstanceTransform(
			i,
			FTransform(FQuat::Identity, SmokePuffWorldLocations[i] + Drift, FVector(FMath::Max(Scale, 0.0f))),
			true,
			false);
		bDirty = true;
	}

	if (bDirty)
	{
		// One render-state flush per frame rather than one per instance.
		SmokePuffs->MarkRenderStateDirty();
	}
}

// ---------------------------------------------------------------------------------------------
// Possession / camera / input (unchanged behaviour)
// ---------------------------------------------------------------------------------------------

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

		if (IgnitionEffect && BoosterRoot)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this, IgnitionEffect, BoosterRoot->GetComponentLocation(), BoosterRoot->GetComponentRotation());
		}
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
	ResetVisuals();
}
