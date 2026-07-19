// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketGeometry.h"

// ---------------------------------------------------------------------------------------------
// FRocketMeshData
// ---------------------------------------------------------------------------------------------

void FRocketMeshData::Reset()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();
	Tangents.Reset();
}

void FRocketMeshData::Append(const FRocketMeshData& Other)
{
	const int32 BaseIndex = Vertices.Num();

	Vertices.Append(Other.Vertices);
	Normals.Append(Other.Normals);
	UVs.Append(Other.UVs);
	Tangents.Append(Other.Tangents);

	Triangles.Reserve(Triangles.Num() + Other.Triangles.Num());
	for (const int32 Index : Other.Triangles)
	{
		Triangles.Add(Index + BaseIndex);
	}
}

void FRocketMeshData::Apply(UProceduralMeshComponent* Target, int32 SectionIndex, bool bCreateCollision) const
{
	if (!Target || IsEmpty())
	{
		return;
	}

	Target->CreateMeshSection(
		SectionIndex,
		Vertices,
		Triangles,
		Normals,
		UVs,
		TArray<FColor>(),
		Tangents,
		bCreateCollision);
}

namespace
{
	/**
	 * Sweep a profile (a polyline in the axial/radial plane) around the +X axis.
	 *
	 * Profile points are FVector2D(AxialX, Radius) ordered aft-to-fore. Normals are derived
	 * analytically from the profile tangent, so surfaces of revolution come out smooth-shaded
	 * without a normal-averaging pass.
	 *
	 * The seam is duplicated (RadialSegments + 1 columns) so UVs run 0..1 without a wrapped
	 * texture smear along the seam column.
	 */
	void BuildRevolution(
		FRocketMeshData& Out,
		const TArray<FVector2D>& Profile,
		int32 RadialSegments,
		bool bCapAft,
		bool bCapFore)
	{
		const int32 RingCount = Profile.Num();
		if (RingCount < 2 || RadialSegments < 3)
		{
			return;
		}

		const int32 Columns = RadialSegments + 1;
		const int32 FirstVertex = Out.Vertices.Num();

		// Total profile arc length, so the V coordinate is proportional to distance along the
		// surface rather than to segment index. Keeps texel density even on curved profiles.
		TArray<float> ArcLength;
		ArcLength.SetNumUninitialized(RingCount);
		ArcLength[0] = 0.0f;
		for (int32 i = 1; i < RingCount; i++)
		{
			ArcLength[i] = ArcLength[i - 1] + FVector2D::Distance(Profile[i - 1], Profile[i]);
		}
		const float TotalArc = FMath::Max(ArcLength.Last(), KINDA_SMALL_NUMBER);

		for (int32 Ring = 0; Ring < RingCount; Ring++)
		{
			const float X = Profile[Ring].X;
			const float R = Profile[Ring].Y;

			// Central difference on the profile gives a stable tangent at interior rings.
			const int32 Prev = FMath::Max(Ring - 1, 0);
			const int32 Next = FMath::Min(Ring + 1, RingCount - 1);
			FVector2D Tangent2D = Profile[Next] - Profile[Prev];
			if (Tangent2D.IsNearlyZero())
			{
				Tangent2D = FVector2D(1.0f, 0.0f);
			}
			Tangent2D.Normalize();

			// Outward normal in the axial/radial plane: rotate the tangent -90 degrees.
			const FVector2D Normal2D(-Tangent2D.Y, Tangent2D.X);
			const float V = ArcLength[Ring] / TotalArc;

			for (int32 Col = 0; Col < Columns; Col++)
			{
				const float Angle = (2.0f * PI * Col) / RadialSegments;
				const float CosA = FMath::Cos(Angle);
				const float SinA = FMath::Sin(Angle);

				Out.Vertices.Add(FVector(X, R * CosA, R * SinA));
				Out.Normals.Add(FVector(Normal2D.X, Normal2D.Y * CosA, Normal2D.Y * SinA).GetSafeNormal());
				Out.UVs.Add(FVector2D(static_cast<float>(Col) / RadialSegments, V));
				// Tangent runs around the circumference (the U direction).
				Out.Tangents.Add(FProcMeshTangent(FVector(0.0f, -SinA, CosA), false));
			}
		}

		for (int32 Ring = 0; Ring < RingCount - 1; Ring++)
		{
			for (int32 Col = 0; Col < RadialSegments; Col++)
			{
				const int32 A = FirstVertex + Ring * Columns + Col;
				const int32 B = A + Columns;

				// Wound so the outward normal faces out with Unreal's clockwise front face.
				Out.Triangles.Add(A);
				Out.Triangles.Add(B);
				Out.Triangles.Add(A + 1);

				Out.Triangles.Add(A + 1);
				Out.Triangles.Add(B);
				Out.Triangles.Add(B + 1);
			}
		}

		// End caps are separate fans: they need their own axial normals, so they cannot share
		// vertices with the rim above.
		auto AddCap = [&Out, RadialSegments](float X, float R, bool bFacingFore)
		{
			if (R <= KINDA_SMALL_NUMBER)
			{
				return; // Degenerate rim (e.g. a cone apex) needs no cap.
			}

			const int32 CenterIndex = Out.Vertices.Num();
			const FVector Normal = bFacingFore ? FVector::XAxisVector : -FVector::XAxisVector;

			Out.Vertices.Add(FVector(X, 0.0f, 0.0f));
			Out.Normals.Add(Normal);
			Out.UVs.Add(FVector2D(0.5f, 0.5f));
			Out.Tangents.Add(FProcMeshTangent(FVector::YAxisVector, false));

			for (int32 Col = 0; Col <= RadialSegments; Col++)
			{
				const float Angle = (2.0f * PI * Col) / RadialSegments;
				const float CosA = FMath::Cos(Angle);
				const float SinA = FMath::Sin(Angle);

				Out.Vertices.Add(FVector(X, R * CosA, R * SinA));
				Out.Normals.Add(Normal);
				Out.UVs.Add(FVector2D(0.5f + 0.5f * CosA, 0.5f + 0.5f * SinA));
				Out.Tangents.Add(FProcMeshTangent(FVector::YAxisVector, false));
			}

			for (int32 Col = 0; Col < RadialSegments; Col++)
			{
				const int32 A = CenterIndex + 1 + Col;
				Out.Triangles.Add(CenterIndex);
				if (bFacingFore)
				{
					Out.Triangles.Add(A);
					Out.Triangles.Add(A + 1);
				}
				else
				{
					Out.Triangles.Add(A + 1);
					Out.Triangles.Add(A);
				}
			}
		};

		if (bCapAft)
		{
			AddCap(Profile[0].X, Profile[0].Y, false);
		}
		if (bCapFore)
		{
			AddCap(Profile.Last().X, Profile.Last().Y, true);
		}
	}

	/** Add a triangle-soup quad with a shared flat normal (used by the fin's faceted surfaces). */
	void AddQuad(
		FRocketMeshData& Out,
		const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3,
		const FVector2D& UV0, const FVector2D& UV1, const FVector2D& UV2, const FVector2D& UV3)
	{
		const int32 Base = Out.Vertices.Num();
		const FVector Normal = FVector::CrossProduct(P1 - P0, P3 - P0).GetSafeNormal();
		const FVector Tangent = (P1 - P0).GetSafeNormal();

		Out.Vertices.Add(P0);
		Out.Vertices.Add(P1);
		Out.Vertices.Add(P2);
		Out.Vertices.Add(P3);

		Out.UVs.Add(UV0);
		Out.UVs.Add(UV1);
		Out.UVs.Add(UV2);
		Out.UVs.Add(UV3);

		for (int32 i = 0; i < 4; i++)
		{
			Out.Normals.Add(Normal);
			Out.Tangents.Add(FProcMeshTangent(Tangent, false));
		}

		Out.Triangles.Add(Base + 0);
		Out.Triangles.Add(Base + 1);
		Out.Triangles.Add(Base + 2);

		Out.Triangles.Add(Base + 0);
		Out.Triangles.Add(Base + 2);
		Out.Triangles.Add(Base + 3);
	}
}

// ---------------------------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------------------------

void RocketGeometry::BuildTangentOgive(
	FRocketMeshData& Out,
	float BaseRadius,
	float Length,
	int32 RadialSegments,
	int32 AxialSegments,
	bool bCapBase)
{
	if (BaseRadius <= 0.0f || Length <= 0.0f)
	{
		return;
	}

	AxialSegments = FMath::Max(AxialSegments, 2);

	// Tangent-ogive radius: the circle that meets the body tube tangentially at the base, so
	// there is no visible crease at the nose/airframe joint.
	const float Rho = (BaseRadius * BaseRadius + Length * Length) / (2.0f * BaseRadius);

	TArray<FVector2D> Profile;
	Profile.Reserve(AxialSegments + 1);

	// Walk from the base (X = 0, r = BaseRadius) forward to the apex (X = Length, r = 0).
	for (int32 i = 0; i <= AxialSegments; i++)
	{
		const float X = (Length * i) / AxialSegments;
		// Distance from the apex, which is how the ogive equation is posed.
		const float FromApex = Length - X;
		const float Inner = FMath::Max(Rho * Rho - FromApex * FromApex, 0.0f);
		const float R = FMath::Max(FMath::Sqrt(Inner) + BaseRadius - Rho, 0.0f);
		Profile.Add(FVector2D(X, R));
	}

	BuildRevolution(Out, Profile, RadialSegments, bCapBase, false);
}

void RocketGeometry::BuildTube(
	FRocketMeshData& Out,
	float AftRadius,
	float ForeRadius,
	float Length,
	int32 RadialSegments,
	bool bCapAft,
	bool bCapFore)
{
	if (Length <= 0.0f || (AftRadius <= 0.0f && ForeRadius <= 0.0f))
	{
		return;
	}

	TArray<FVector2D> Profile;
	Profile.Add(FVector2D(0.0f, AftRadius));
	Profile.Add(FVector2D(Length, ForeRadius));

	BuildRevolution(Out, Profile, RadialSegments, bCapAft, bCapFore);
}

void RocketGeometry::BuildFin(
	FRocketMeshData& Out,
	float RootChord,
	float TipChord,
	float Span,
	float SweepDistance,
	float MaxThickness,
	float BodyRadius)
{
	if (RootChord <= 0.0f || Span <= 0.0f)
	{
		return;
	}

	// Fin planform in the XZ plane. Root sits on the body surface, tip Span further out. The
	// airfoil is a simple symmetric diamond: zero thickness at leading and trailing edges,
	// MaxThickness at mid-chord, thinning outboard.
	const float RootZ = BodyRadius;
	const float TipZ = BodyRadius + Span;

	const float RootLE = RootChord;                       // root leading edge (forward, +X)
	const float RootTE = 0.0f;                            // root trailing edge
	const float TipLE = RootChord - SweepDistance;        // swept back
	const float TipTE = TipLE - TipChord;

	const float RootHalfT = MaxThickness * 0.5f;
	const float TipHalfT = RootHalfT * 0.6f;              // thinner at the tip

	// Mid-chord spine points, where the airfoil reaches full thickness.
	const float RootMid = (RootLE + RootTE) * 0.5f;
	const float TipMid = (TipLE + TipTE) * 0.5f;

	const FVector RootLEPt(RootLE, 0.0f, RootZ);
	const FVector RootTEPt(RootTE, 0.0f, RootZ);
	const FVector TipLEPt(TipLE, 0.0f, TipZ);
	const FVector TipTEPt(TipTE, 0.0f, TipZ);

	// Two surfaces per side (leading half and trailing half), mirrored across Y.
	for (int32 Side = 0; Side < 2; Side++)
	{
		const float Sign = (Side == 0) ? 1.0f : -1.0f;

		const FVector RootMidPt(RootMid, Sign * RootHalfT, RootZ);
		const FVector TipMidPt(TipMid, Sign * TipHalfT, TipZ);

		if (Side == 0)
		{
			// Leading half, then trailing half, wound for the +Y face.
			AddQuad(Out, RootLEPt, TipLEPt, TipMidPt, RootMidPt,
				FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 0.5f), FVector2D(0, 0.5f));
			AddQuad(Out, RootMidPt, TipMidPt, TipTEPt, RootTEPt,
				FVector2D(0, 0.5f), FVector2D(1, 0.5f), FVector2D(1, 1), FVector2D(0, 1));
		}
		else
		{
			// Reversed winding so the -Y face also points outward.
			AddQuad(Out, RootMidPt, TipMidPt, TipLEPt, RootLEPt,
				FVector2D(0, 0.5f), FVector2D(1, 0.5f), FVector2D(1, 0), FVector2D(0, 0));
			AddQuad(Out, RootTEPt, TipTEPt, TipMidPt, RootMidPt,
				FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0.5f), FVector2D(0, 0.5f));
		}
	}

	// Close the outboard tip so the fin is not visibly hollow when seen edge-on.
	const FVector TipTop(TipMid, TipHalfT, TipZ);
	const FVector TipBot(TipMid, -TipHalfT, TipZ);
	AddQuad(Out, TipLEPt, TipTop, TipTEPt, TipBot,
		FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1));
}

void RocketGeometry::BuildLaunchLug(
	FRocketMeshData& Out,
	float OuterRadius,
	float Length,
	float BodyRadius,
	int32 RadialSegments)
{
	if (OuterRadius <= 0.0f || Length <= 0.0f)
	{
		return;
	}

	FRocketMeshData Lug;
	RocketGeometry::BuildTube(Lug, OuterRadius, OuterRadius, Length, RadialSegments, true, true);

	// Slide the lug off-axis so it rides on the body surface.
	const float Offset = BodyRadius + OuterRadius * 0.85f;
	for (FVector& Vertex : Lug.Vertices)
	{
		Vertex.Z += Offset;
	}

	Out.Append(Lug);
}

void RocketGeometry::BuildGoredCanopy(
	FRocketMeshData& Out,
	float Radius,
	float Depth,
	int32 GoreCount,
	int32 RadialSegments,
	float ScallopDepth,
	float SpillHoleRadius)
{
	if (Radius <= 0.0f || Depth <= 0.0f)
	{
		return;
	}

	GoreCount = FMath::Max(GoreCount, 3);
	RadialSegments = FMath::Max(RadialSegments, 2);

	// Columns follow gore boundaries: several columns per gore so the fabric can bulge between
	// the load-bearing seams. Duplicate the seam column for clean UVs.
	const int32 ColumnsPerGore = 4;
	const int32 RadialColumns = GoreCount * ColumnsPerGore;
	const int32 Columns = RadialColumns + 1;

	const float VentRadius = Radius * FMath::Clamp(SpillHoleRadius, 0.0f, 0.5f);
	const int32 FirstVertex = Out.Vertices.Num();

	for (int32 Ring = 0; Ring <= RadialSegments; Ring++)
	{
		// T runs 0 at the apex vent to 1 at the skirt.
		const float T = static_cast<float>(Ring) / RadialSegments;

		for (int32 Col = 0; Col < Columns; Col++)
		{
			const float Angle = (2.0f * PI * Col) / RadialColumns;

			// Phase within the current gore, 0 at one seam, 1 at the next.
			const float GorePhase = FMath::Fmod(Angle * GoreCount / (2.0f * PI), 1.0f);
			// Fabric bulges outward mid-gore and is pulled in at the seams.
			const float Bulge = FMath::Sin(GorePhase * PI);

			// Scalloped hem: the skirt is pulled up between line attachment points, and the
			// effect fades to nothing toward the apex.
			const float Scallop = ScallopDepth * Bulge * T * T;

			const float BaseR = FMath::Lerp(VentRadius, Radius, FMath::Sin(T * HALF_PI));
			const float R = BaseR * (1.0f - Scallop * 0.5f);

			// Dome profile, lifted slightly mid-gore so the bulge is visible in silhouette.
			const float BaseX = Depth * FMath::Cos(T * HALF_PI);
			const float X = BaseX + Scallop * Depth * 0.6f + Bulge * (1.0f - T) * Depth * 0.04f;

			Out.Vertices.Add(FVector(X, R * FMath::Cos(Angle), R * FMath::Sin(Angle)));
			Out.UVs.Add(FVector2D(static_cast<float>(Col) / RadialColumns, T));
			// Normals are refined below once neighbours exist.
			Out.Normals.Add(FVector::XAxisVector);
			Out.Tangents.Add(FProcMeshTangent(FVector::YAxisVector, false));
		}
	}

	for (int32 Ring = 0; Ring < RadialSegments; Ring++)
	{
		for (int32 Col = 0; Col < RadialColumns; Col++)
		{
			const int32 A = FirstVertex + Ring * Columns + Col;
			const int32 B = A + Columns;

			// The canopy is viewed from below as often as above, so it is emitted double-sided
			// by the caller's material rather than by duplicating geometry here.
			Out.Triangles.Add(A);
			Out.Triangles.Add(B);
			Out.Triangles.Add(A + 1);

			Out.Triangles.Add(A + 1);
			Out.Triangles.Add(B);
			Out.Triangles.Add(B + 1);
		}
	}

	// Smooth normals by accumulating face normals over the ring/column grid. The analytic route
	// used for surfaces of revolution does not apply once the gore bulge deforms the surface.
	const int32 VertexCount = (RadialSegments + 1) * Columns;
	TArray<FVector> Accumulated;
	Accumulated.SetNumZeroed(VertexCount);

	for (int32 Ring = 0; Ring < RadialSegments; Ring++)
	{
		for (int32 Col = 0; Col < RadialColumns; Col++)
		{
			const int32 A = Ring * Columns + Col;
			const int32 B = A + Columns;
			const int32 C = A + 1;

			const FVector FaceNormal = FVector::CrossProduct(
				Out.Vertices[FirstVertex + B] - Out.Vertices[FirstVertex + A],
				Out.Vertices[FirstVertex + C] - Out.Vertices[FirstVertex + A]).GetSafeNormal();

			Accumulated[A] += FaceNormal;
			Accumulated[B] += FaceNormal;
			Accumulated[C] += FaceNormal;
			Accumulated[B + 1] += FaceNormal;
		}
	}

	for (int32 i = 0; i < VertexCount; i++)
	{
		const FVector Normal = Accumulated[i].GetSafeNormal();
		Out.Normals[FirstVertex + i] = Normal.IsNearlyZero() ? FVector::XAxisVector : Normal;
	}
}

void RocketGeometry::BuildPlume(
	FRocketMeshData& Out,
	float ThroatRadius,
	float Length,
	float BulgeFactor,
	int32 RadialSegments,
	int32 AxialSegments)
{
	if (ThroatRadius <= 0.0f || Length <= 0.0f)
	{
		return;
	}

	AxialSegments = FMath::Max(AxialSegments, 2);

	TArray<FVector2D> Profile;
	Profile.Reserve(AxialSegments + 1);

	// Plume runs aft along -X: wide flare just past the throat, tapering to a point.
	for (int32 i = 0; i <= AxialSegments; i++)
	{
		const float T = static_cast<float>(i) / AxialSegments;
		const float X = -Length * T;

		// Flare quickly, then taper: sin gives the initial expansion, the falloff closes the tip.
		const float Flare = FMath::Lerp(1.0f, BulgeFactor, FMath::Sin(FMath::Min(T * 3.0f, 1.0f) * HALF_PI));
		const float Taper = FMath::Pow(1.0f - T, 0.7f);
		Profile.Add(FVector2D(X, ThroatRadius * Flare * Taper));
	}

	BuildRevolution(Out, Profile, RadialSegments, false, false);
}
