// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RocketSectionMotion.generated.h"

/**
 * Per-step inputs for FRocketSectionMotion::Integrate, in the pawn's local frame.
 *
 * Grouped into a struct because the section dynamics need airflow, cord and damping terms
 * together, and a nine-argument function is impossible to call correctly.
 */
struct FRocketSectionForces
{
	/** Point the section is tethered to (local frame, relative to the section's stowed spot). */
	FVector Anchor = FVector::ZeroVector;

	/** Maximum distance from Anchor before the cord goes taut. */
	float CordLength = 0.0f;

	/**
	 * Unit vector the section settles along - "away from the canopy, down the cord".
	 *
	 * This is derived from the AIR-RELATIVE velocity, not from the body axis. A tethered
	 * section hangs along the airflow regardless of how the airframe above it is tumbling, so
	 * using the body's -X here makes sections swing to wherever the nose happens to point.
	 */
	FVector SettleDir = FVector::ZeroVector;

	/** Acceleration along SettleDir (cm/s^2). */
	float SettleAccel = 0.0f;

	/** Buffeting acceleration (cm/s^2), perpendicular to the airflow. Drives wind shudder. */
	FVector BuffetAccel = FVector::ZeroVector;

	/** Per-second velocity retention, 0..1 (0.5 = lose half a second's speed). */
	float LinearDamping = 0.5f;

	/** Per-second tumble retention, 0..1. */
	float AngularDamping = 0.35f;

	/**
	 * How strongly the section swings to point along its cord, 0..1 per second. A section
	 * dangling on a line settles nose-toward-the-anchor once the separation tumble bleeds off;
	 * without this the tumble just decays to the stowed orientation, which reads as the piece
	 * snapping back to attention.
	 */
	float AlignStrength = 0.0f;

	/**
	 * Which end of the section points AT THE ANCHOR: +1 aims its nose (+X) at the anchor, -1
	 * aims its tail.
	 *
	 * Note this is relative to the anchor, not to "up" - so the correct value depends on
	 * whether the anchor is above or below the section. A piece hanging by its forward end from
	 * an anchor above it wants +1; a piece bridled at its base whose anchor is the hardware
	 * BELOW it also wants +1, because aiming +X down at that anchor is what puts its base up.
	 */
	float AlignAxisSign = 1.0f;
};

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

	/** Advance one step. See FRocketSectionForces for the meaning of each term. */
	void Integrate(float DeltaSeconds, const FRocketSectionForces& Forces);
};
