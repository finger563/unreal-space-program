#!/usr/bin/env python3
"""
hil_flight_computer.py — minimal external "flight computer" for the rocket UDP bridge.

Demonstrates full hardware-in-the-loop style control of the rocket in Unreal:
it receives the JSON telemetry stream, ignites the motor, detects apogee itself,
and deploys the drogue and main chutes — i.e. it plays the role the on-board
avionics would play on a real flight.

Setup in Unreal (see Docs/ROCKET_SETUP.md §8):
  - On BP_Rocket's FlightController: Mode = Manual, bAutoRecovery = False
    (so THIS script, not Unreal, is in control of the flight).
  - On BP_Rocket's UdpBridge component: bAutoStart = True (defaults: listen 5761,
    telemetry to 127.0.0.1:5762).
  - Press Play in the editor, then run:  python Tools/hil_flight_computer.py

No third-party dependencies (stdlib only).
"""

import json
import socket
import sys
import time

UE_COMMAND_ADDR = ("127.0.0.1", 5761)   # bridge ListenPort
TELEMETRY_BIND = ("127.0.0.1", 5762)    # bridge TelemetryAddress:TelemetryPort

COUNTDOWN_S = 3.0            # our own countdown after link-up
APOGEE_VS_FPS = -5.0         # declare apogee when descending faster than this
MAIN_DEPLOY_AGL_FT = 500.0   # main chute altitude


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(TELEMETRY_BIND)
    sock.settimeout(2.0)

    def send(cmd: str):
        sock.sendto((cmd + "\n").encode("utf-8"), UE_COMMAND_ADDR)
        print(f">> {cmd}")

    print(f"Flight computer up. Listening for telemetry on {TELEMETRY_BIND[0]}:{TELEMETRY_BIND[1]}, "
          f"commanding {UE_COMMAND_ADDR[0]}:{UE_COMMAND_ADDR[1]}.")
    send("HELLO")

    link_up_time = None
    ignited = False
    drogue_out = False
    main_out = False
    last_print = 0.0

    while True:
        try:
            data, _ = sock.recvfrom(64 * 1024)
        except socket.timeout:
            print("... waiting for telemetry (is the sim running with the bridge started?)")
            send("HELLO")
            continue

        for line in data.decode("utf-8", errors="replace").splitlines():
            line = line.strip()
            if not line.startswith("{"):
                if line:  # OK/ERR/PONG replies
                    print(f"<< {line}")
                continue

            try:
                t = json.loads(line)
            except json.JSONDecodeError:
                continue

            now = time.monotonic()
            if link_up_time is None:
                link_up_time = now
                print("Telemetry link established. Starting countdown...")

            # Periodic console status
            if now - last_print > 0.5:
                last_print = now
                print(f"  MET {t['met']:7.1f}s  {t['phase']:<15}  "
                      f"AGL {t['agl_ft']:8.1f} ft  VS {t['vs_fps']:+8.1f} ft/s  "
                      f"thrust {t['thrust_lbf']:6.1f} lbf")

            # --- Flight logic (the "avionics") ---
            if not ignited and now - link_up_time >= COUNTDOWN_S:
                send("IGNITE")
                ignited = True

            if ignited and not drogue_out and t["liftoff"] and t["vs_fps"] < APOGEE_VS_FPS:
                print(f"*** Apogee detected at {t['max_agl_ft']:.0f} ft — deploying drogue")
                send("DROGUE")
                drogue_out = True

            if drogue_out and not main_out and t["agl_ft"] < MAIN_DEPLOY_AGL_FT:
                print(f"*** {t['agl_ft']:.0f} ft AGL — deploying main")
                send("MAIN")
                main_out = True

            if t["landed"]:
                print(f"*** LANDED. Apogee {t['apogee_agl_ft']:.0f} ft, "
                      f"flight time {t['met']:.1f} s.")
                return


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
