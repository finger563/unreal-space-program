// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketTelemetryWidget.h"
#include "RocketFlightController.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "Styling/CoreStyle.h"

namespace
{
	const TCHAR* PhaseDisplayName(ERocketFlightPhase Phase)
	{
		switch (Phase)
		{
		case ERocketFlightPhase::Prelaunch:		return TEXT("PRELAUNCH");
		case ERocketFlightPhase::PoweredAscent:	return TEXT("POWERED ASCENT");
		case ERocketFlightPhase::CoastAscent:	return TEXT("COAST");
		case ERocketFlightPhase::DrogueDescent:	return TEXT("DROGUE DESCENT");
		case ERocketFlightPhase::MainDescent:	return TEXT("MAIN DESCENT");
		case ERocketFlightPhase::Landed:		return TEXT("LANDED");
		default:								return TEXT("UNKNOWN");
		}
	}

	FLinearColor PhaseColor(ERocketFlightPhase Phase)
	{
		switch (Phase)
		{
		case ERocketFlightPhase::Prelaunch:		return FLinearColor(0.75f, 0.75f, 0.75f);
		case ERocketFlightPhase::PoweredAscent:	return FLinearColor(1.00f, 0.45f, 0.10f);
		case ERocketFlightPhase::CoastAscent:	return FLinearColor(1.00f, 0.85f, 0.20f);
		case ERocketFlightPhase::DrogueDescent:	return FLinearColor(0.30f, 0.80f, 1.00f);
		case ERocketFlightPhase::MainDescent:	return FLinearColor(0.30f, 1.00f, 0.40f);
		case ERocketFlightPhase::Landed:		return FLinearColor(1.00f, 1.00f, 1.00f);
		default:								return FLinearColor::White;
		}
	}
}

URocketTelemetryWidget::URocketTelemetryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URocketTelemetryWidget::SetFlightController(URocketFlightController* InController)
{
	FlightController = InController;
}

void URocketTelemetryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Only build the code layout when there is no designer tree (i.e. we're the raw C++
	// class, not a Blueprint subclass with its own layout).
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
	}
}

UTextBlock* URocketTelemetryWidget::AddRow(UVerticalBox* Box, const FString& Label)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	LabelText->SetText(FText::FromString(Label));
	LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 13));
	LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)));
	LabelText->SetMinDesiredWidth(110.0f);
	Row->AddChildToHorizontalBox(LabelText);

	UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ValueText->SetText(FText::FromString(TEXT("--")));
	ValueText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 13));
	ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Row->AddChildToHorizontalBox(ValueText);

	UVerticalBoxSlot* RowSlot = Box->AddChildToVerticalBox(Row);
	RowSlot->SetPadding(FMargin(0.0f, 1.5f));

	return ValueText;
}

void URocketTelemetryWidget::BuildDefaultLayout()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Canvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	Panel->SetPadding(FMargin(16.0f, 12.0f));

	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
	PanelSlot->SetAlignment(FVector2D(0.0f, 0.0f));
	PanelSlot->SetPosition(FVector2D(24.0f, 24.0f));

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);

	// Phase banner
	PhaseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PhaseText"));
	PhaseText->SetText(FText::FromString(TEXT("PRELAUNCH")));
	PhaseText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 20));
	PhaseText->SetColorAndOpacity(FSlateColor(PhaseColor(ERocketFlightPhase::Prelaunch)));
	UVerticalBoxSlot* PhaseSlot = Box->AddChildToVerticalBox(PhaseText);
	PhaseSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	// Mission clock
	MetText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MetText"));
	MetText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 15));
	MetText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	UVerticalBoxSlot* MetSlot = Box->AddChildToVerticalBox(MetText);
	MetSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));

	// Data rows
	AltitudeText = AddRow(Box, TEXT("Altitude"));
	VerticalSpeedText = AddRow(Box, TEXT("V. Speed"));
	ApogeeText = AddRow(Box, TEXT("Apogee"));
	ThrustText = AddRow(Box, TEXT("Thrust"));
	RecoveryText = AddRow(Box, TEXT("Recovery"));
	WindText = AddRow(Box, TEXT("Wind"));
}

void URocketTelemetryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Late-resolve the controller from the owning pawn if it wasn't injected.
	if (!FlightController)
	{
		if (APawn* Pawn = GetOwningPlayerPawn())
		{
			FlightController = Pawn->FindComponentByClass<URocketFlightController>();
		}
		if (!FlightController)
		{
			if (PhaseText)
			{
				PhaseText->SetText(FText::FromString(TEXT("NO FLIGHT CONTROLLER")));
			}
			return;
		}
	}

	const URocketFlightController* FC = FlightController;

	if (PhaseText)
	{
		PhaseText->SetText(FText::FromString(PhaseDisplayName(FC->Phase)));
		PhaseText->SetColorAndOpacity(FSlateColor(PhaseColor(FC->Phase)));
	}

	if (MetText)
	{
		FString Clock;
		if (FC->bMotorIgnited)
		{
			Clock = FString::Printf(TEXT("T+ %7.1f s"), FC->TimeSinceIgnitionSeconds);
		}
		else if (FC->Mode == ERocketFlightMode::Auto)
		{
			Clock = FString::Printf(TEXT("T- %7.1f s"), FMath::Max(0.0f, FC->IgnitionDelaySeconds - FC->MissionTimeSeconds));
		}
		else
		{
			Clock = TEXT("T- --.- s  (awaiting ignition)");
		}
		MetText->SetText(FText::FromString(Clock));
	}

	if (AltitudeText)
	{
		AltitudeText->SetText(FText::FromString(FString::Printf(
			TEXT("%.0f ft AGL   (%.0f ft ASL)"), FC->AltitudeAGLFt, FC->AltitudeASLFt)));
	}

	if (VerticalSpeedText)
	{
		VerticalSpeedText->SetText(FText::FromString(FString::Printf(
			TEXT("%+.1f ft/s"), FC->VerticalSpeedFps)));
	}

	if (ApogeeText)
	{
		ApogeeText->SetText(FText::FromString(FC->bReachedApogee
			? FString::Printf(TEXT("%.0f ft"), FC->ApogeeAltitudeAGLFt)
			: FString::Printf(TEXT("%.0f ft (max so far)"), FC->MaxAltitudeAGLFt)));
	}

	if (ThrustText)
	{
		ThrustText->SetText(FText::FromString(FString::Printf(
			TEXT("%.0f lbf %s"), FC->MotorThrustLbf,
			FC->bMotorIgnited ? (FC->MotorThrustLbf > 0.01f ? TEXT("(burning)") : TEXT("(burnout)")) : TEXT(""))));
	}

	if (RecoveryText)
	{
		RecoveryText->SetText(FText::FromString(FString::Printf(
			TEXT("Drogue %s    Main %s"),
			FC->bDrogueDeployed ? TEXT("DEPLOYED") : TEXT("stowed"),
			FC->bMainDeployed ? TEXT("DEPLOYED") : TEXT("stowed"))));
	}

	if (WindText)
	{
		if (FC->bEnableWind && FC->WindSpeedKts > 0.0f)
		{
			// Show the effective (altitude-scaled) speed, and the heading the wind blows FROM.
			WindText->SetText(FText::FromString(FString::Printf(
				TEXT("%.0f kt from %.0f° (%s)"),
				FC->CurrentWindSpeedKts, FC->WindHeadingDeg,
				*URocketFlightController::CompassPoint(FC->WindHeadingDeg))));
		}
		else
		{
			WindText->SetText(FText::FromString(TEXT("calm")));
		}
	}
}
