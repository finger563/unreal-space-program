// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketSectionMotion.h"

void FRocketSectionMotion::Integrate(float DeltaSeconds, const FRocketSectionForces& Forces)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Clamp the step so a hitch or a breakpoint cannot launch a section across the map. The
	// cord spring below is stiff, and stiff springs go unstable with large steps.
	DeltaSeconds = FMath::Min(DeltaSeconds, 1.0f / 30.0f);

	// Settling acceleration: what pulls the section down the cord once it is loose.
	Velocity += Forces.SettleDir * Forces.SettleAccel * DeltaSeconds;

	// Buffeting from the airflow, already perpendicular to it when it arrives here.
	Velocity += Forces.BuffetAccel * DeltaSeconds;

	// Cord constraint. Slack cord does nothing; taut cord pulls back hard and bleeds off the
	// radial component of velocity, which is what produces the characteristic snap-and-swing.
	const FVector FromAnchor = Position - Forces.Anchor;
	const float Distance = FromAnchor.Size();
	const bool bTaut = Forces.CordLength > 0.0f && Distance > Forces.CordLength && Distance > KINDA_SMALL_NUMBER;

	if (bTaut)
	{
		const FVector Radial = FromAnchor / Distance;
		const float Overshoot = Distance - Forces.CordLength;

		// Stiff spring back toward the cord's reach.
		const float SpringAccel = Overshoot * 60.0f;
		Velocity -= Radial * SpringAccel * DeltaSeconds;

		// Kill outward velocity so the section does not saw back and forth through the limit.
		const float RadialSpeed = FVector::DotProduct(Velocity, Radial);
		if (RadialSpeed > 0.0f)
		{
			Velocity -= Radial * RadialSpeed * 0.85f;
		}

		// A cord snap also spins the section, but only in proportion to how hard it snapped.
		// Scaled well below the old value: a hard-coded kick every frame the cord was taut was
		// what made the sections jitter permanently instead of settling.
		const float SnapSpin = FMath::Min(Overshoot, 40.0f) * 0.15f;
		AngularVelocity += FVector(
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f)) * SnapSpin;
	}

	// Exponential damping, framerate independent.
	const float LinearRetain = FMath::Pow(FMath::Clamp(1.0f - Forces.LinearDamping, 0.0f, 1.0f), DeltaSeconds);
	const float AngularRetain = FMath::Pow(FMath::Clamp(1.0f - Forces.AngularDamping, 0.0f, 1.0f), DeltaSeconds);

	Velocity *= LinearRetain;
	AngularVelocity *= AngularRetain;

	Position += Velocity * DeltaSeconds;

	// Hard backstop on the cord. The spring above is soft by design (it produces the swing and
	// the snap), but a strong gust or swing can overwhelm it - measured overshoots past 60% of
	// the cord length - and since the cable's rendered length tracks its endpoint distance,
	// that shows up as a visibly over-stretched tether. A cord physically cannot exceed its
	// length, so clamp it outright.
	if (Forces.CordLength > 0.0f)
	{
		const FVector Offset = Position - Forces.Anchor;
		const float Stretch = Offset.Size();
		if (Stretch > Forces.CordLength && Stretch > KINDA_SMALL_NUMBER)
		{
			Position = Forces.Anchor + Offset * (Forces.CordLength / Stretch);
		}
	}

	Rotation += FRotator(
		AngularVelocity.Y * DeltaSeconds,
		AngularVelocity.Z * DeltaSeconds,
		AngularVelocity.X * DeltaSeconds);
	Rotation.Normalize();

	// Once the tumble has bled off, swing the section round to hang along its cord: its own +X
	// (nose) points back up toward the anchor it is dangling from. Blending toward the target
	// only as the spin decays keeps the initial separation tumble intact.
	if (Forces.AlignStrength > 0.0f && Distance > KINDA_SMALL_NUMBER)
	{
		// AlignAxisSign picks which end of the section is the one on the tether: +1 hangs by the
		// nose, -1 hangs by the tail (and therefore points tip-down).
		const FVector ToAnchor = (-FromAnchor / Distance) * FMath::Sign(Forces.AlignAxisSign);
		const FRotator Hanging = FRotationMatrix::MakeFromX(ToAnchor).Rotator();

		// Fade the alignment in as the tumble slows, so a fast-spinning section is not dragged
		// upright mid-spin.
		const float SpinFactor = FMath::Clamp(1.0f - AngularVelocity.Size() / 180.0f, 0.0f, 1.0f);
		const float Alpha = FMath::Clamp(Forces.AlignStrength * SpinFactor * DeltaSeconds, 0.0f, 1.0f);

		Rotation = FMath::RInterpTo(Rotation, Hanging, 1.0f, Alpha);
		Rotation.Normalize();
	}
}
