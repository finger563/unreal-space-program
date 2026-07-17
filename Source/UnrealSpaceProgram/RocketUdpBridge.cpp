// Copyright Epic Games, Inc. All Rights Reserved.

#include "RocketUdpBridge.h"
#include "RocketFlightController.h"
#include "JSBSimMovementComponent.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

namespace
{
	const TCHAR* PhaseWireName(ERocketFlightPhase Phase)
	{
		switch (Phase)
		{
		case ERocketFlightPhase::Prelaunch:		return TEXT("PRELAUNCH");
		case ERocketFlightPhase::PoweredAscent:	return TEXT("POWERED_ASCENT");
		case ERocketFlightPhase::CoastAscent:	return TEXT("COAST");
		case ERocketFlightPhase::DrogueDescent:	return TEXT("DROGUE_DESCENT");
		case ERocketFlightPhase::MainDescent:	return TEXT("MAIN_DESCENT");
		case ERocketFlightPhase::Landed:		return TEXT("LANDED");
		default:								return TEXT("UNKNOWN");
		}
	}

	const TCHAR* BoolJson(bool b) { return b ? TEXT("true") : TEXT("false"); }
}

URocketUdpBridge::URocketUdpBridge()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URocketUdpBridge::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoStart)
	{
		StartBridge();
	}
}

void URocketUdpBridge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBridge();
	Super::EndPlay(EndPlayReason);
}

bool URocketUdpBridge::StartBridge()
{
	if (Socket)
	{
		return true;
	}

	Socket = FUdpSocketBuilder(TEXT("RocketHILBridge"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(ListenPort)
		.WithReceiveBufferSize(64 * 1024)
		.WithSendBufferSize(64 * 1024)
		.Build();

	if (!Socket)
	{
		UE_LOG(LogTemp, Error, TEXT("RocketUdpBridge: failed to bind UDP port %d."), ListenPort);
		return false;
	}

	// Resolve the telemetry destination.
	FIPv4Address Addr;
	if (FIPv4Address::Parse(TelemetryAddress, Addr))
	{
		TelemetryAddr = FIPv4Endpoint(Addr, (uint16)TelemetryPort).ToInternetAddr();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RocketUdpBridge: could not parse TelemetryAddress '%s' - telemetry only goes to the last commander."), *TelemetryAddress);
		TelemetryAddr.Reset();
	}

	UE_LOG(LogTemp, Display, TEXT("RocketUdpBridge: listening on UDP %d, telemetry -> %s:%d @ %.0f Hz."),
		ListenPort, *TelemetryAddress, TelemetryPort, TelemetryRateHz);
	return true;
}

void URocketUdpBridge::StopBridge()
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	TelemetryAddr.Reset();
	LastCommanderAddr.Reset();
}

URocketFlightController* URocketUdpBridge::GetFlightController()
{
	if (!FlightController)
	{
		if (AActor* Owner = GetOwner())
		{
			FlightController = Owner->FindComponentByClass<URocketFlightController>();
		}
	}
	return FlightController;
}

UJSBSimMovementComponent* URocketUdpBridge::GetMovement()
{
	if (AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UJSBSimMovementComponent>();
	}
	return nullptr;
}

void URocketUdpBridge::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Socket)
	{
		return;
	}

	ReceiveCommands();

	// Fixed-rate telemetry (at most one datagram per tick).
	TelemetryAccumulator += DeltaTime;
	const float Interval = 1.0f / FMath::Max(1.0f, TelemetryRateHz);
	if (TelemetryAccumulator >= Interval)
	{
		TelemetryAccumulator = FMath::Fmod(TelemetryAccumulator, Interval);
		SendTelemetry();
	}
}

void URocketUdpBridge::ReceiveCommands()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	uint32 PendingSize = 0;
	while (Socket->HasPendingData(PendingSize))
	{
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(FMath::Min(PendingSize, 64u * 1024u) + 1);

		int32 BytesRead = 0;
		TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
		if (!Socket->RecvFrom(Buffer.GetData(), Buffer.Num() - 1, BytesRead, *Sender) || BytesRead <= 0)
		{
			break;
		}
		Buffer[BytesRead] = 0;

		const FString Datagram = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));

		// A datagram may carry several newline-separated commands.
		TArray<FString> Lines;
		Datagram.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				HandleCommandLine(Trimmed, Sender);
			}
		}
	}
}

void URocketUdpBridge::HandleCommandLine(const FString& Line, const TSharedRef<FInternetAddr>& Sender)
{
	CommandsReceived++;
	LastCommand = Line;
	LastCommanderAddr = Sender->Clone();

	TArray<FString> Tokens;
	Line.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() == 0)
	{
		return;
	}
	const FString Cmd = Tokens[0].ToUpper();

	const TSharedPtr<FInternetAddr> Reply = LastCommanderAddr;
	URocketFlightController* FC = GetFlightController();

	auto NeedsController = [&]() -> bool
	{
		if (!FC)
		{
			SendString(TEXT("ERR no flight controller\n"), Reply);
			return false;
		}
		return true;
	};

	if (Cmd == TEXT("PING"))
	{
		SendString(TEXT("PONG\n"), Reply);
	}
	else if (Cmd == TEXT("HELLO"))
	{
		SendString(TEXT("OK HELLO\n"), Reply);
	}
	else if (Cmd == TEXT("IGNITE"))
	{
		if (NeedsController()) { FC->Ignite(); SendString(TEXT("OK IGNITE\n"), Reply); }
	}
	else if (Cmd == TEXT("SHUTDOWN"))
	{
		if (NeedsController()) { FC->ShutdownMotor(); SendString(TEXT("OK SHUTDOWN\n"), Reply); }
	}
	else if (Cmd == TEXT("THROTTLE"))
	{
		if (Tokens.Num() < 2) { SendString(TEXT("ERR THROTTLE needs a value 0..1\n"), Reply); }
		else if (NeedsController()) { FC->SetThrottle(FCString::Atof(*Tokens[1])); SendString(TEXT("OK THROTTLE\n"), Reply); }
	}
	else if (Cmd == TEXT("DROGUE"))
	{
		if (NeedsController()) { FC->DeployDrogue(); SendString(TEXT("OK DROGUE\n"), Reply); }
	}
	else if (Cmd == TEXT("MAIN"))
	{
		if (NeedsController()) { FC->DeployMain(); SendString(TEXT("OK MAIN\n"), Reply); }
	}
	else if (Cmd == TEXT("DROGUE_AREA"))
	{
		if (Tokens.Num() < 2) { SendString(TEXT("ERR DROGUE_AREA needs a value (ft2)\n"), Reply); }
		else if (NeedsController()) { FC->SetDrogueDragArea(FCString::Atof(*Tokens[1])); SendString(TEXT("OK DROGUE_AREA\n"), Reply); }
	}
	else if (Cmd == TEXT("MAIN_AREA"))
	{
		if (Tokens.Num() < 2) { SendString(TEXT("ERR MAIN_AREA needs a value (ft2)\n"), Reply); }
		else if (NeedsController()) { FC->SetMainDragArea(FCString::Atof(*Tokens[1])); SendString(TEXT("OK MAIN_AREA\n"), Reply); }
	}
	else if (Cmd == TEXT("RESET"))
	{
		if (NeedsController()) { FC->ResetFlight(); SendString(TEXT("OK RESET\n"), Reply); }
	}
	else if (Cmd == TEXT("PROP"))
	{
		// PROP <name> [value] - raw JSBSim property read/write passthrough.
		if (Tokens.Num() < 2)
		{
			SendString(TEXT("ERR PROP needs a property name\n"), Reply);
		}
		else if (UJSBSimMovementComponent* Move = GetMovement())
		{
			const FString InValue = (Tokens.Num() >= 3) ? Tokens[2] : FString();
			FString OutValue;
			Move->CommandConsole(Tokens[1], InValue, OutValue);
			SendString(FString::Printf(TEXT("PROP %s %s\n"), *Tokens[1], *OutValue), Reply);
		}
		else
		{
			SendString(TEXT("ERR no JSBSim movement component\n"), Reply);
		}
	}
	else
	{
		SendString(FString::Printf(TEXT("ERR unknown command '%s'\n"), *Cmd), Reply);
	}
}

void URocketUdpBridge::SendString(const FString& Message, const TSharedPtr<FInternetAddr>& Target)
{
	if (!Socket || !Target.IsValid())
	{
		return;
	}
	FTCHARToUTF8 Utf8(*Message);
	int32 BytesSent = 0;
	Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), BytesSent, *Target);
}

FString URocketUdpBridge::BuildTelemetryJson()
{
	URocketFlightController* FC = GetFlightController();
	if (!FC)
	{
		return TEXT("{\"error\":\"no flight controller\"}\n");
	}

	double Lat = 0.0, Lon = 0.0, Yaw = 0.0, Pitch = 0.0, Roll = 0.0;
	if (UJSBSimMovementComponent* Move = GetMovement())
	{
		Lat = Move->AircraftState.Latitude;
		Lon = Move->AircraftState.Longitude;
		Yaw = Move->AircraftState.LocalEulerAngles.Yaw;
		Pitch = Move->AircraftState.LocalEulerAngles.Pitch;
		Roll = Move->AircraftState.LocalEulerAngles.Roll;
	}

	return FString::Printf(
		TEXT("{\"met\":%.3f,\"phase\":\"%s\",\"ignited\":%s,\"liftoff\":%s,\"apogee\":%s,")
		TEXT("\"drogue\":%s,\"main\":%s,\"landed\":%s,")
		TEXT("\"agl_ft\":%.2f,\"asl_ft\":%.2f,\"vs_fps\":%.2f,\"thrust_lbf\":%.2f,")
		TEXT("\"max_agl_ft\":%.2f,\"apogee_agl_ft\":%.2f,")
		TEXT("\"lat\":%.7f,\"lon\":%.7f,\"yaw_deg\":%.2f,\"pitch_deg\":%.2f,\"roll_deg\":%.2f}\n"),
		FC->MissionTimeSeconds, PhaseWireName(FC->Phase),
		BoolJson(FC->bMotorIgnited), BoolJson(FC->bLiftedOff), BoolJson(FC->bReachedApogee),
		BoolJson(FC->bDrogueDeployed), BoolJson(FC->bMainDeployed), BoolJson(FC->bLanded),
		FC->AltitudeAGLFt, FC->AltitudeASLFt, FC->VerticalSpeedFps, FC->MotorThrustLbf,
		FC->MaxAltitudeAGLFt, FC->ApogeeAltitudeAGLFt,
		Lat, Lon, Yaw, Pitch, Roll);
}

void URocketUdpBridge::SendTelemetry()
{
	const FString Json = BuildTelemetryJson();

	bool bSent = false;
	if (TelemetryAddr.IsValid())
	{
		SendString(Json, TelemetryAddr);
		bSent = true;
	}
	if (bSendTelemetryToLastCommander && LastCommanderAddr.IsValid())
	{
		// Avoid double-sending when the commander IS the telemetry endpoint.
		if (!TelemetryAddr.IsValid() || !(*LastCommanderAddr == *TelemetryAddr))
		{
			SendString(Json, LastCommanderAddr);
			bSent = true;
		}
	}
	if (bSent)
	{
		TelemetryPacketsSent++;
	}
}
