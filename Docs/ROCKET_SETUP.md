# Amateur Suborbital Rocket — Setup Guide

This document explains how to build, place, and fly the JSBSim-driven amateur suborbital
rocket that was added to this project (launch → powered ascent → coast → apogee →
drogue chute → main chute → landing), and how an external / hardware-in-the-loop (HIL)
script can drive it.

It is the Unreal port of the standalone `jsbsim-rocket-test` reference simulation
(Cesaroni L1720 motor, ~47 lb rocket, ~3200 ft apogee, dual-deploy recovery).

---

## 1. What was added

**JSBSim model** (loaded by the plugin at runtime):
```
Plugins/JSBSimFlightDynamicsModel/Resources/JSBSim/aircraft/rocket/
  rocket.xml                         # adapted from the reference (see note below)
  Engines/cesaroni_l1720_engine.xml  # motor thrust curve (verbatim from reference)
  Engines/l1720_nozzle.xml           # nozzle (verbatim)
  Systems/Conventional Controls.xml  # FCS channels (verbatim)
  Systems/Landing Gear.xml           # gear kinematic (verbatim)
```
The only substantive change from the reference `rocket.xml` is the `<ground_reactions>`
block: it now has a firm, damped 3-point tripod of `STRUCTURE` contacts at the tail so the
rocket rests on real Cesium/terrain collision during the countdown and survives a
parachute-descent touchdown (the reference's single 0.01 lbs/ft contact would let it fall
through the ground). Aerodynamics, mass, propulsion, and the parachute `external_reactions`
are unchanged.

**C++ (game module `UnrealSpaceProgram`):**
```
Source/UnrealSpaceProgram/RocketFlightController.h / .cpp   # launch/recovery state machine + command API
Source/UnrealSpaceProgram/RocketPawn.h / .cpp               # ready-to-fly pawn (mesh + JSBSim + controller + camera)
```

**Plugin change** (`JSBSimFlightDynamicsModel`): added a `bTrimOnStart` option to
`UJSBSimMovementComponent`. It defaults to `true` (unchanged behavior for the plane). The
rocket pawn sets it to `false`, because JSBSim's trim solves for a steady aerodynamic state
that does not exist for a rocket sitting inert on a pad (and can diverge).

**Mesh generator:**
```
Tools/generate_rocket_glb.py   # dependency-free generator (stdlib only)
RawAssets/rocket.glb           # generated placeholder rocket mesh (nose along +X)
```

---

## 2. Build

You added new C++ files, so regenerate the project files and rebuild:

1. Close the editor.
2. Right-click `UnrealSpaceProgram.uproject` → **Generate Visual Studio project files**.
   (Or from a terminal, run the UBT `-projectfiles` command for the project.)
3. Open the `.sln` and build **Development Editor | Win64**, or just reopen the `.uproject`
   and let it prompt to compile.

The game module already depends on `JSBSimFlightDynamicsModel`, so no `.Build.cs` change is
required.

---

## 3. Import the rocket mesh (optional but recommended)

The pawn ships with a visible **placeholder** (a thin engine cylinder scaled to rocket
proportions), so it works with no import. For a proper rocket shape:

1. Drag `RawAssets/rocket.glb` into the Content Browser (e.g. into `Content/Rocket/`).
   Use the default glTF import; a scale of 100 (meters → cm) is applied automatically.
2. This gives you `SM_rocket` (a static mesh) with the nose along **+X**, matching the
   pawn's expected orientation.

To use **your own** rocket model instead, import it however you like and just assign it in
step 4 — the only requirement is that the **nose points along +X** (see §7 for fixing
orientation if it doesn't).

You can re-generate/adjust the placeholder any time:
```
python Tools/generate_rocket_glb.py
```

---

## 4. Create the rocket Blueprint

Making a Blueprint child lets you assign the mesh asset and tweak defaults per-instance.

1. Content Browser → right-click → **Blueprint Class** → expand **All Classes** → pick
   **RocketPawn** → name it `BP_Rocket`.
2. Open `BP_Rocket`. Select the **RocketMesh** component and set its **Static Mesh** to your
   imported `SM_rocket` (skip this to keep the placeholder).
3. (Optional) Select the **JSBSimMovement** component and, with the component selected in the
   viewport, use its reference-point visualization to tune **Structural Frame Origin** so the
   mesh lines up with the logical model (see §7).
4. (Optional) Select the **FlightController** component to configure the flight (see §5).

> You can also skip the Blueprint and drag the C++ **RocketPawn** straight into the level from
> the Place Actors panel, but then you can't assign a content mesh asset (you'll get the
> placeholder).

---

## 5. Configure the flight (FlightController component)

Key properties on **RocketFlightController**:

| Property | Meaning | Default |
|---|---|---|
| **Mode** | `Auto` = full sequence runs on Play. `Manual` = driven by input / Blueprint / external script. | Auto |
| **Ignition Delay Seconds** | (Auto) countdown before auto-ignition. | 2.0 |
| **b Auto Recovery** | If true, drogue + main deploy automatically in either mode. Set false for fully-manual recovery. | true |
| **Drogue / Main Drag Area Sq Ft** | Effective Cd·A applied on deploy. | 8 / 50 |
| **Main Deploy Altitude AGL Ft** | Where the main chute auto-deploys. | 500 |
| Detection thresholds | Liftoff/apogee/landing tuning — rarely need changing. | — |
| **b Show Flight HUD** | On-screen MET / altitude / phase readout. | true |

**Three ready-made experiences:**
- **Watch a launch, hands-off:** `Mode = Auto`, `bAutoRecovery = true`. Press Play.
- **Manual ignition, auto recovery:** `Mode = Manual`, `bAutoRecovery = true`. Press
  **Spacebar** (or call `Ignite()`) to launch; chutes deploy automatically.
- **Fully manual (for testing / external control):** `Mode = Manual`, `bAutoRecovery = false`.
  Trigger everything yourself: **Space** ignite, **G** drogue, **H** main, **R** reset.

### On-screen telemetry widget

When the pawn is possessed it automatically creates a UMG telemetry panel
(`URocketTelemetryWidget`): phase banner (color-coded), mission clock (counts down `T-` in
Auto mode, `T+` after ignition), altitude AGL/ASL, vertical speed, apogee, motor thrust and
recovery status. The layout is built entirely in C++, so **no widget asset needs to be
authored** — it just works.

- Toggle it with **`bShowTelemetryWidget`** on the pawn.
- To restyle it, create a **Widget Blueprint subclassing `RocketTelemetryWidget`**, design
  your own layout, and name your TextBlocks `PhaseText`, `MetText`, `AltitudeText`,
  `VerticalSpeedText`, `ApogeeText`, `ThrustText`, `RecoveryText` (any subset — they bind
  automatically via `BindWidgetOptional`). Then set the pawn's **`TelemetryWidgetClass`**
  to your Blueprint.
- The old on-screen debug text (`bShowFlightHUD` on the FlightController) still exists but
  the pawn now defaults it off in favor of the widget.

---

## 6. Place it on the pad and fly

Use the existing **`Content/Levels/Map_Rocket`** — it already contains the
`GeoReferencingSystem` and Cesium tileset the JSBSim component needs. (A rocket in an empty
level will error out with "impossible to use ... without a GeoReferencingSystem".)

1. Drag `BP_Rocket` into the level near where the plane starts (or anywhere over ground with
   collision).
2. **Rotate it Pitch = +90°** so the nose (+X) points at the sky. The JSBSim component reads
   the actor's placement to set the launch attitude, so this is what makes it a *vertical*
   launch. (Use ~89–90°; exactly 90° is fine here since trim is skipped.)

   **Frame convention:** the rocket body spans actor-local X ∈ [0 .. 277 cm] — the **tail
   (and its ground contacts) sit exactly at the actor origin**, the nose at +277 cm. So with
   Pitch = +90, put the **actor origin right on the pad surface**; the rocket stands above
   it. Don't bury the origin or float it — a few cm above the ground is fine (it settles),
   but meters above means a 2-second free-fall and meters below means the contacts start
   underground. The auto-ignition sequence will refuse to fire (and log a warning) if the
   rocket is not settled on the pad.
3. Make sure it sits on ground **that has collision**:
   - Over the `SM_Runway` static mesh, **or**
   - On Cesium World Terrain with collision enabled (select the `Cesium3DTileset` →
     **Create Physics Meshes** / collision enabled).
   The rocket's altitude-above-ground comes from a downward raycast; with no collider under
   it, it will fall.
4. The pawn has **Auto Possess Player = Player 0**, so pressing **Play** possesses the rocket
   and shows the stabilized chase camera. If your plane also grabs Player 0, set the plane's
   Auto Possess to Disabled, or set this level's **Game Mode Override → Default Pawn Class**
   to `BP_Rocket` (or `None`).
5. Press **Play**. In Auto mode it lifts off after the countdown; you'll see the phase HUD
   count through POWERED ASCENT → COAST → DROGUE DESCENT → MAIN DESCENT → LANDED.

**Default keys** (from the pawn, active when possessed):
`Space` ignite · `G` deploy drogue · `H` deploy main · `R` reset flight.

**Chase camera** framing is on the pawn (`Camera Distance Meters`, `Camera Height Meters`,
`Camera Azimuth Deg`). It's world-stabilized so it never tumbles with the rocket. Set
`bUseChaseCamera = false` if you'd rather drive the view from your own Blueprint / player
controller.

---

## 7. Aligning the mesh (Structural Frame Origin)

JSBSim computes in its own "structural frame" (origin at the nose, X toward the tail). The
plugin maps that to the actor so the actor's **+X is the nose direction**. The generated
`rocket.glb` already follows this.

To line up a mesh precisely:
- Select the **JSBSimMovement** component in the Blueprint/level. Its editor visualizer draws
  the CG / eye-point / visual-reference-point markers.
- Nudge **Structural Frame Origin** (a translation, in cm) until the markers sit correctly on
  your mesh (e.g. CG around the middle of the body).
- If your mesh faces the **wrong way**, rotate the **RocketMesh** component (not the pawn):
  a 180° yaw or pitch flips it; you don't need to re-export the model.

Enabling **DrawDebug** on the JSBSim component at runtime also draws the tail contact points
(green when off the ground, red with weight on them) — handy for confirming the tail sits on
the pad.

---

## 8. External / hardware-in-the-loop (HIL) control

Everything the auto sequence does is exposed as a `BlueprintCallable` command surface on
`URocketFlightController`, so an external controller drives the rocket by calling these
(from Blueprint, a custom UDP/socket actor, Unreal Remote Control, etc.):

**Commands**
```
Ignite()                       // full-throttle solid-motor ignition
ShutdownMotor()
SetThrottle(float 0..1)        // throttleable/liquid motors or HIL thrust control
DeployDrogue() / DeployMain()
SetDrogueDragArea(float ft2)   // continuous chute control (0 = stowed)
SetMainDragArea(float ft2)
ResetFlight()
```

**Telemetry** (BlueprintReadOnly on the controller): `Phase`, `MissionTimeSeconds`,
`TimeSinceIgnitionSeconds`, `AltitudeAGLFt`, `AltitudeASLFt`, `VerticalSpeedFps`,
`MaxAltitudeAGLFt`, `ApogeeAltitudeAGLFt`, `MotorThrustLbf`, and the `bMotorIgnited /
bLiftedOff / bReachedApogee / bDrogueDeployed / bMainDeployed / bLanded` flags.

**Events** (BlueprintAssignable): `OnIgnition`, `OnLiftoff`, `OnApogee`, `OnDrogueDeployed`,
`OnMainDeployed`, `OnLanded`, `OnPhaseChanged` — bind these to push telemetry out or trigger
effects.

**Arbitrary JSBSim access:** the movement component also exposes
`CommandConsole(Property, InValue, OutValue)` and `CommandConsoleBatch(...)` for reading or
writing *any* JSBSim property (e.g. `fcs/throttle-cmd-norm[0]`, `atmosphere/wind-north-fps`,
`propulsion/engine[0]/thrust-lbs`). A HIL bridge can use these directly for anything not on
the controller's surface.

### 8.1 The built-in UDP bridge (recommended for HIL loops)

`ARocketPawn` includes a **`RocketUdpBridge`** component — a lightweight UDP plant-model
interface. It is **disabled by default**; to use it:

1. On `BP_Rocket`, select the **UdpBridge** component and tick **`bAutoStart`**
   (or call `StartBridge()` at runtime). Defaults: commands in on UDP **5761**, telemetry
   out to **127.0.0.1:5762** at **30 Hz** (configurable up to 240).
2. For full external authority set the FlightController to `Mode = Manual` and
   `bAutoRecovery = false`, so nothing fires unless your controller commands it.
3. Run the example external flight computer:
   ```
   python Tools/hil_flight_computer.py
   ```
   It links up, runs its own countdown, ignites, detects apogee from the telemetry, and
   deploys drogue + main itself — the full avionics role, outside Unreal.

**Telemetry** — one JSON object per datagram at the configured rate:
```json
{"met":12.3,"phase":"POWERED_ASCENT","ignited":true,"liftoff":true,"apogee":false,
 "drogue":false,"main":false,"landed":false,"agl_ft":842.1,"asl_ft":868.4,
 "vs_fps":401.2,"thrust_lbf":428.0,"max_agl_ft":842.1,"apogee_agl_ft":0.0,
 "lat":37.0,"lon":-122.0,"yaw_deg":180.0,"pitch_deg":88.2,"roll_deg":0.4}
```

**Commands** — plain text, one or more per datagram, case-insensitive; each gets an
`OK ...` / `ERR ...` reply:

| Command | Effect |
|---|---|
| `HELLO` | Register as a telemetry receiver (single-socket clients) |
| `PING` | Liveness check (`PONG`) |
| `IGNITE` / `SHUTDOWN` | Motor control |
| `THROTTLE <0..1>` | Direct throttle |
| `DROGUE` / `MAIN` | Deploy chutes |
| `DROGUE_AREA <ft2>` / `MAIN_AREA <ft2>` | Set chute drag areas directly |
| `RESET` | Reset the flight state machine |
| `PROP <name> [value]` | Read/write **any** JSBSim property (passthrough to `CommandConsole`) |

Telemetry goes to the configured `TelemetryAddress:TelemetryPort` and (by default) also back
to whoever last sent a command, so a one-socket client that just sends `HELLO` starts
receiving immediately.

### 8.2 UDP bridge vs. Unreal Remote Control

The **Remote Control** plugin also works here with zero custom code — everything on
`URocketFlightController` is `BlueprintCallable`, so after enabling the plugin you can call
`Ignite()` etc. through its HTTP/WebSocket REST API and read the telemetry properties. That's
a good fit for **interactive** use: test benches, tuning panels, web dashboards, one-off
commands.

For an actual **HIL control loop**, prefer the UDP bridge:
- Remote Control is request/response HTTP + JSON per call — latency and jitter you don't
  want inside a control loop; UDP datagrams are fire-and-forget.
- Remote Control has no fixed-rate telemetry *push* (WebSocket subscriptions are
  change-driven); the bridge streams at a deterministic, configurable rate.
- Remote Control needs extra configuration in packaged/game builds; the bridge is just a
  component in the pawn.
- UDP text/JSON is trivial to speak from embedded firmware or a bare C test harness —
  the same reason FlightGear/X-Plane HIL rigs (and JSBSim's own socket I/O) use UDP.

The two are not exclusive: use the bridge for the loop and Remote Control for
poking/tuning while it runs.

---

## 8.5 Airframe separation, sections, and parachute visuals

The rocket is modeled and visualized as a real dual-deploy stack:

**Physics.** When the drogue deploys (or `SeparateAirframe()` is called — also the UDP
`SEPARATE` command), the airframe "separates": the JSBSim property `systems/fins-effective`
goes to 0, disabling the fin weathercock/damping moments (a broken, tethered stack has no
fin stability — before this fix, the fins fought the nose-attached parachute and the body
pendulumed hard during descent). Two new `*_damp_chute` moments, proportional to the
deployed drag area, model riser/canopy damping so the swing settles out. `ResetFlight()`
restores the intact airframe.

**Sections.** The pawn's visual is three sections that separate during recovery:

| Section | Actor X span | Component |
|---|---|---|
| Booster (fins + nozzle) | 0 – 110 cm | `BoosterRoot` |
| Upper airframe | 110 – 215 cm | `UpperRoot` |
| Nose cone | 215 – 277 cm | `NoseRoot` |

Placeholders built from engine basic shapes render out of the box. At separation the
booster slides down the (implied) shock cord below the upper airframe; at main deploy the
nose cone pops off and dangles beside the payload bay. Drogue and main canopies appear
above the nose and inflate on deploy. Tuning: `SeparationAnimSpeed`, `CanopyInflateSpeed`.

**Custom section meshes.** `Tools/generate_rocket_glb.py` now also emits
`rocket_booster.glb`, `rocket_upper.glb`, `rocket_nose.glb`, and `chute_canopy.glb`
(all origin-at-aft-joint, +X toward the nose, true size). Import them and attach each as a
static mesh under the matching `*Root` component (identity transform), then hide the
placeholder components. Assigning a single full-body mesh to `RocketMesh` instead switches
to single-body mode (sections hidden, no separation animation).

## 9. Physics caveats (inherited from the reference model)

The reference `jsbsim-rocket-test` notes its JSBSim tuning "is not necessarily ideal /
correct" — this port keeps that model faithfully, so the same caveats apply:

- The aero coefficients, fin moments, and the engine `thrust_table` were hand-tuned to
  roughly reproduce the manufacturer's ~3200 ft apogee, not derived rigorously.
- A single point-contact rocket is laterally unstable on the pad (like balancing a pencil);
  the tripod contacts and a near-vertical, quick launch keep it upright. If you add strong
  ground wind (`WindIntensityKts` on the JSBSim component) during a long countdown it can
  tip — keep the countdown short or wind low, or ignite manually.
- Apogee is detected from vertical-speed sign; parachute forces are simple `qbar * drag_area`
  applied at the nose. Good enough for visualization; refine in `rocket.xml` if you want
  higher fidelity.

---

## 10. Quick reference — files

| File | Purpose |
|---|---|
| `Plugins/.../Resources/JSBSim/aircraft/rocket/*` | JSBSim rocket model, engine, systems |
| `Source/UnrealSpaceProgram/RocketFlightController.*` | Launch/recovery state machine + command API |
| `Source/UnrealSpaceProgram/RocketPawn.*` | Ready-to-fly pawn (mesh + JSBSim + controller + camera + widget + bridge) |
| `Source/UnrealSpaceProgram/RocketTelemetryWidget.*` | On-screen telemetry UMG widget (code-built, BP-restylable) |
| `Source/UnrealSpaceProgram/RocketUdpBridge.*` | UDP HIL bridge (JSON telemetry out, text commands in) |
| `Plugins/.../Public/JSBSimMovementComponent.h` + `Private/...cpp` | Added `bTrimOnStart` option |
| `Tools/generate_rocket_glb.py`, `RawAssets/rocket.glb` | Placeholder mesh + generator |
| `Tools/hil_flight_computer.py` | Example external flight computer driving the UDP bridge |
