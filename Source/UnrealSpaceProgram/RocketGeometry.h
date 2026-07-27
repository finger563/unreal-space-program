// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/**
 * Procedural mesh generators for the rocket's airframe and recovery hardware.
 *
 * Everything here is pure geometry: build an FRocketMeshData, hand it to
 * FRocketMeshData::Apply() to push it into a UProceduralMeshComponent. No assets, no imports,
 * so the rocket renders correctly on a fresh clone with nothing but engine content.
 *
 * Convention shared with ARocketPawn: every generator emits geometry in a local frame whose
 * origin is the part's AFT joint, with the part extending along +X (nose direction). This is
 * the same convention the section Root components use, so a generated mesh can be attached at
 * the identity transform and land in the right place.
 *
 * Units are centimetres (Unreal world units) throughout.
 */

/** Vertex/index buffers for one procedural mesh section, in the layout ProcMesh wants. */
struct FRocketMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	/** Append another mesh's geometry, re-basing its triangle indices. Used to weld multi-part
	 *  bodies (tube + boat tail, canopy + skirt) into a single draw. */
	void Append(const FRocketMeshData& Other);

	/** Push this geometry into SectionIndex of Target. bCreateCollision is off by default:
	 *  the rocket's collision comes from JSBSim's ground-contact model, not these visuals. */
	void Apply(UProceduralMeshComponent* Target, int32 SectionIndex = 0, bool bCreateCollision = false) const;

	void Reset();
	bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() == 0; }
};

namespace RocketGeometry
{
	/**
	 * A tangent-ogive nose cone - the profile amateur high-power nose cones actually use, and a
	 * clear visual upgrade over a straight cone. Origin at the BASE, apex at +X Length.
	 *
	 * @param BaseRadius       Radius where the cone meets the airframe.
	 * @param Length           Base-to-tip length.
	 * @param RadialSegments   Segments around the circumference.
	 * @param AxialSegments    Segments along the axis (the curve needs a good number to read).
	 * @param bCapBase         Emit a disc closing the aft end.
	 */
	void BuildTangentOgive(
		FRocketMeshData& Out,
		float BaseRadius,
		float Length,
		int32 RadialSegments = 24,
		int32 AxialSegments = 16,
		bool bCapBase = true);

	/**
	 * A body tube (optionally tapered, which is how the boat tail is made). Origin at the aft
	 * end, extending to +X Length.
	 *
	 * @param AftRadius        Radius at X = 0.
	 * @param ForeRadius       Radius at X = Length. Equal to AftRadius gives a plain cylinder.
	 */
	void BuildTube(
		FRocketMeshData& Out,
		float AftRadius,
		float ForeRadius,
		float Length,
		int32 RadialSegments = 24,
		bool bCapAft = false,
		bool bCapFore = false);

	/**
	 * A trapezoidal fin with a symmetric airfoil cross-section, swept back from the root.
	 * Built in the XZ plane (chord along X, span along +Z) at the given radial offset, so the
	 * caller rolls it about +X to place all four.
	 *
	 * @param RootChord        Chord where the fin meets the body.
	 * @param TipChord         Chord at the outboard edge.
	 * @param Span             Radial extent beyond BodyRadius.
	 * @param SweepDistance    How far aft the tip leading edge sits relative to the root's.
	 * @param MaxThickness     Peak airfoil thickness at the root (tapers to ~60% at the tip).
	 * @param BodyRadius       Radius the fin root sits on.
	 */
	void BuildFin(
		FRocketMeshData& Out,
		float RootChord,
		float TipChord,
		float Span,
		float SweepDistance,
		float MaxThickness,
		float BodyRadius);

	/** A small launch lug (open tube) lying along +X, offset radially to sit on the body. */
	void BuildLaunchLug(
		FRocketMeshData& Out,
		float OuterRadius,
		float Length,
		float BodyRadius,
		int32 RadialSegments = 10);

	/**
	 * A gored parachute canopy: a shallow dome built from N gores with scalloped hems and a
	 * spill hole at the apex, which is what makes it read as fabric rather than a squashed
	 * sphere. Origin at the canopy's SKIRT plane (where the shroud lines attach), dome bulging
	 * toward +X.
	 *
	 * @param Radius           Inflated skirt radius.
	 * @param Depth            Apex height above the skirt plane.
	 * @param GoreCount        Number of fabric gores (also the number of shroud lines).
	 * @param RadialSegments   Segments along each gore's meridian.
	 * @param ScallopDepth     How deeply the hem scallops between line attachment points,
	 *                         as a fraction of Radius. 0 gives a plain circular hem.
	 * @param SpillHoleRadius  Apex vent radius, as a fraction of Radius. 0 closes the apex.
	 */
	void BuildGoredCanopy(
		FRocketMeshData& Out,
		float Radius,
		float Depth,
		int32 GoreCount = 8,
		int32 RadialSegments = 10,
		float ScallopDepth = 0.12f,
		float SpillHoleRadius = 0.08f);

	/**
	 * The exhaust plume body: a tapering, slightly bulged cone used with an emissive material
	 * as the code-only fallback when no Niagara system is assigned. Origin at the nozzle
	 * throat, plume extending along -X (aft).
	 *
	 * @param ThroatRadius     Radius at the nozzle.
	 * @param Length           Plume length at full scale (the pawn scales this by thrust).
	 * @param BulgeFactor      Peak width relative to the throat, giving the shock-diamond flare.
	 */
	void BuildPlume(
		FRocketMeshData& Out,
		float ThroatRadius,
		float Length,
		float BulgeFactor = 1.6f,
		int32 RadialSegments = 16,
		int32 AxialSegments = 12);
}
