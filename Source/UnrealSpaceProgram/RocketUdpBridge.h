// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RocketUdpBridge.generated.h"

class URocketFlightController;
class UJSBSimMovementComponent;
class FSocket;
class FInternetAddr;

/**
 * UDP hardware-in-the-loop bridge for the rocket.
 *
 * Turns the rocket into a network-controllable plant model: an external program (flight
 * computer firmware, a Python script, a test bench) receives a fixed-rate JSON telemetry
 * stream and sends plain-text commands back. Disabled by default - tick bAutoStart (or call
 * StartBridge) to use it. For full external authority, also set the flight controller to
 * Mode = Manual and bAutoRecovery = false.
 *
 * Wire protocol (UDP datagrams, UTF-8 text):
 *
 *   Telemetry (Unreal -> controller), sent at TelemetryRateHz, one JSON object per datagram:
 *     {"met":12.345,"phase":"POWERED_ASCENT","ignited":true,"liftoff":true,"apogee":false,
 *      "drogue":false,"main":false,"landed":false,"agl_ft":842.1,"asl_ft":868.4,
 *      "vs_fps":401.2,"thrust_lbf":428.0,"max_agl_ft":842.1,"apogee_agl_ft":0.0,
 *      "lat":37.0000000,"lon":-122.0000000,"yaw_deg":180.0,"pitch_deg":88.2,"roll_deg":0.4}
 *
 *   Commands (controller -> Unreal), one or more lines per datagram, case-insensitive:
 *     HELLO                    register as telemetry receiver (reply: OK HELLO)
 *     PING                     liveness check (reply: PONG)
 *     IGNITE                   ignite the motor
 *     SHUTDOWN                 cut the motor
 *     THROTTLE <0..1>          set throttle
 *     DROGUE                   deploy drogue chute
 *     MAIN                     deploy main chute
 *     DROGUE_AREA <ft2>        set drogue effective drag area directly
 *     MAIN_AREA <ft2>          set main effective drag area directly
 *     RESET                    reset the flight state machine
 *     PROP <name> [value]      read (or write) any JSBSim property (reply: PROP <name> <value>)
 *
 *   Every command gets an "OK <cmd>" / "ERR <reason>" (or PONG / PROP ...) reply to the sender.
 *
 * Telemetry is streamed to TelemetryAddress:TelemetryPort and - if
 * bSendTelemetryToLastCommander is set - also to whoever last sent a command (so a
 * single-socket client can just send HELLO and start receiving).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNREALSPACEPROGRAM_API URocketUdpBridge : public UActorComponent
{
	GENERATED_BODY()

public:
	URocketUdpBridge();

	// ---------------- Configuration ----------------

	/** Open the socket automatically on BeginPlay. Off by default; the bridge is opt-in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge")
	bool bAutoStart = false;

	/** UDP port this bridge listens on for commands. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ListenPort = 5761;

	/** Address the telemetry stream is sent to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge")
	FString TelemetryAddress = TEXT("127.0.0.1");

	/** Port the telemetry stream is sent to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 TelemetryPort = 5762;

	/** Telemetry datagrams per second (capped by the game frame rate). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge", meta = (ClampMin = "1", ClampMax = "240"))
	float TelemetryRateHz = 30.0f;

	/** Also stream telemetry to the address that last sent a command (single-socket clients). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge")
	bool bSendTelemetryToLastCommander = true;

	/** Optional explicit flight controller; if unset, the owner is searched. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "HIL Bridge")
	TObjectPtr<URocketFlightController> FlightController;

	// ---------------- Status ----------------

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "HIL Bridge|Status")
	int32 CommandsReceived = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "HIL Bridge|Status")
	int32 TelemetryPacketsSent = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "HIL Bridge|Status")
	FString LastCommand;

	// ---------------- Control ----------------

	/** Open the UDP socket and start bridging. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "HIL Bridge")
	bool StartBridge();

	/** Close the socket and stop bridging. */
	UFUNCTION(BlueprintCallable, Category = "HIL Bridge")
	void StopBridge();

	UFUNCTION(BlueprintCallable, Category = "HIL Bridge")
	bool IsBridgeRunning() const { return Socket != nullptr; }

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> TelemetryAddr;
	TSharedPtr<FInternetAddr> LastCommanderAddr;
	float TelemetryAccumulator = 0.0f;

	URocketFlightController* GetFlightController();
	UJSBSimMovementComponent* GetMovement();

	void ReceiveCommands();
	void HandleCommandLine(const FString& Line, const TSharedRef<FInternetAddr>& Sender);
	void SendString(const FString& Message, const TSharedPtr<FInternetAddr>& Target);
	void SendTelemetry();
	FString BuildTelemetryJson();
};
