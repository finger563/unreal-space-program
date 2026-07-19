// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketSectionMotion.h"

void FRocketSectionMotion::Integrate(
	float DeltaSeconds,
	const FVector& Anchor,
	float CordLength,
	const FVector& SettleDir,
	float SettleAccel,
	float LinearDamping,
	float AngularDamping)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Clamp the step so a hitch or a breakpoint cannot launch a section across the map. The
	// cord spring below is stiff, and stiff springs go unstable with large steps.
	DeltaSeconds = FMath::Min(DeltaSeconds, 1.0f / 30.0f);

	// Settling acceleration: what pulls the section down the cord once it is loose.
	Velocity += SettleDir * SettleAccel * DeltaSeconds;

	// Cord constraint. Slack cord does nothing; taut cord pulls back hard and bleeds off the
	// radial component of velocity, which is what produces the characteristic snap-and-swing.
	const FVector FromAnchor = Position - Anchor;
	const float Distance = FromAnchor.Size();
	if (CordLength > 0.0f && Distance > CordLength && Distance > KINDA_SMALL_NUMBER)
	{
		const FVector Radial = FromAnchor / Distance;
		const float Overshoot = Distance - CordLength;

		// Stiff spring back toward the cord's reach.
		const float SpringAccel = Overshoot * 60.0f;
		Velocity -= Radial * SpringAccel * DeltaSeconds;

		// Kill outward velocity so the section does not saw back and forth through the limit.
		const float RadialSpeed = FVector::DotProduct(Velocity, Radial);
		if (RadialSpeed > 0.0f)
		{
			Velocity -= Radial * RadialSpeed * 0.85f;
		}

		// A cord snap also spins the section.
		AngularVelocity += FVector(
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f)) * Overshoot * 0.6f;
	}

	// Exponential damping, framerate independent.
	const float LinearRetain = FMath::Pow(FMath::Clamp(1.0f - LinearDamping, 0.0f, 1.0f), DeltaSeconds);
	const float AngularRetain = FMath::Pow(FMath::Clamp(1.0f - AngularDamping, 0.0f, 1.0f), DeltaSeconds);

	Velocity *= LinearRetain;
	AngularVelocity *= AngularRetain;

	Position += Velocity * DeltaSeconds;

	Rotation += FRotator(
		AngularVelocity.Y * DeltaSeconds,
		AngularVelocity.Z * DeltaSeconds,
		AngularVelocity.X * DeltaSeconds);
	Rotation.Normalize();
}
