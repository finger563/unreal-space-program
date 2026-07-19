// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RocketSectionMotion.generated.h"

/**
 * Free-body motion state for one separated airframe section, integrated in the pawn's LOCAL
 * frame.
 *
 * Why local frame: JSBSim owns the rocket's trajectory and still models the whole tethered
 * recovery train as a single rigid body. These sections are therefore purely cosmetic - they
 * describe how the pieces hang and tumble RELATIVE to that body, not where they are in the
 * world. Integrating locally means the sections inherit the flight path for free and cannot
 * drift away from the physics body.
 *
 * The local frame is non-inertial (it accelerates and rotates with the rocket), so this is an
 * approximation, not a simulation. It is tuned to look right, and the cord constraint keeps it
 * from ever diverging visibly.
 */
USTRUCT()
struct FRocketSectionMotion
{
	GENERATED_BODY()

	/** Current offset from the section's stowed position, in the pawn's local frame (cm). */
	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	/** Local-frame velocity (cm/s). */
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	/** Current rotation away from the stowed orientation. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Tumble rate, degrees/sec about each local axis. */
	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;

	/** True once this section has been kicked loose (so the impulse is applied exactly once). */
	UPROPERTY()
	bool bReleased = false;

	void Reset()
	{
		Position = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
		AngularVelocity = FVector::ZeroVector;
		bReleased = false;
	}

	/**
	 * Advance one step.
	 *
	 * @param DeltaSeconds   Step length.
	 * @param Anchor         Point this section is tethered to, in the same local frame.
	 * @param CordLength     Maximum distance from Anchor. The section is pulled back with a
	 *                       stiff spring once it runs out of cord - this is what makes it swing
	 *                       and jerk instead of drifting off.
	 * @param SettleDir      Direction the section settles toward under drag (unit vector), i.e.
	 *                       "down the cord" once the canopy is holding the train.
	 * @param SettleAccel    Acceleration along SettleDir (cm/s^2). Stands in for the drag /
	 *                       gravity difference between the section and the main body.
	 * @param LinearDamping  Per-second velocity retention, 0..1 (0.5 = lose half a second's speed).
	 * @param AngularDamping Per-second tumble retention, 0..1.
	 */
	void Integrate(
		float DeltaSeconds,
		const FVector& Anchor,
		float CordLength,
		const FVector& SettleDir,
		float SettleAccel,
		float LinearDamping,
		float AngularDamping);
};
