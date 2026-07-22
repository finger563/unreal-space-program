// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RocketTelemetryWidget.generated.h"

class URocketFlightController;
class UTextBlock;
class UVerticalBox;

/**
 * On-screen flight telemetry for the rocket: phase banner, mission clock, altitude,
 * vertical speed, apogee, motor thrust and recovery status.
 *
 * The default layout is built entirely in C++ (NativeOnInitialized), so it works with no
 * editor-authored asset: ARocketPawn creates one automatically on possession.
 *
 * To restyle it, make a Blueprint subclass with your own Designer layout - any TextBlocks
 * named like the BindWidgetOptional properties below (PhaseText, MetText, AltitudeText,
 * VerticalSpeedText, ApogeeText, ThrustText, RecoveryText) are bound automatically and
 * updated with live values; the code-built layout is skipped when a designer tree exists.
 */
UCLASS()
class UNREALSPACEPROGRAM_API URocketTelemetryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URocketTelemetryWidget(const FObjectInitializer& ObjectInitializer);

	/** The flight controller to read telemetry from. If unset, the owning pawn is searched. */
	UPROPERTY(BlueprintReadWrite, Category = "Rocket")
	TObjectPtr<URocketFlightController> FlightController;

	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void SetFlightController(URocketFlightController* InController);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Text elements. In the code-built layout these are created programmatically; in a
	// Blueprint subclass with a designer tree, same-named widgets bind automatically.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PhaseText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AltitudeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VerticalSpeedText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ApogeeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ThrustText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RecoveryText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WindText;

private:
	/** Builds the default panel (canvas > border > rows) when no designer tree exists. */
	void BuildDefaultLayout();

	/** Adds a "Label   Value" row to Box and returns the value TextBlock. */
	UTextBlock* AddRow(UVerticalBox* Box, const FString& Label);
};
