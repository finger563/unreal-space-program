# Unreal Space Program

This repository is a modification of the
[jsbsim/UnrealEngine](https://github.com/jsbsim-team/jsbsim) unreal engine
example project with JSBSim to provide a playground for realistic rocket
simulation. A useful reference I followed when extending this project is the
original post by Epic Games: [A DIY Flight Simulator
Tutorial](https://dev.epicgames.com/community/learning/tutorials/mmL/a-diy-flight-simulator-tutorial).

<img width="1698" alt="CleanShot 2023-08-05 at 13 36 31@2x" src="https://github.com/finger563/unreal-space-program/assets/213467/4aa77996-68f2-4dc5-b006-e60c10b8af6f">

<img width="1690" alt="CleanShot 2023-08-05 at 13 38 18@2x" src="https://github.com/finger563/unreal-space-program/assets/213467/b8f5e1bf-faa2-447d-bb7e-b7e31d16e64d">

<!-- markdown-toc start - Don't edit this section. Run M-x markdown-toc-refresh-toc -->
**Table of Contents**

- [Unreal Space Program](#unreal-space-program)
  - [Information](#information)
  - [Learning more about Unreal Engine](#learning-more-about-unreal-engine)
  - [Key Mappings](#key-mappings)
    - [Flight Commands](#flight-commands)
    - [Application](#application)
    - [Environment](#environment)
  - [Developing](#developing)
    - [Setting up Cesium Plugin](#setting-up-cesium-plugin)

<!-- markdown-toc end -->

## Information

This project has been tested on MacOS and is currently under heavy development.

## Learning more about Unreal Engine
You can find many free learning resources on Unreal Engine Developper Community portal : 
[Getting Started](https://dev.epicgames.com/community/getting-started)
[Library of Learning Courses](https://dev.epicgames.com/community/learning)

Still in the context of "Antoinette Project" we wrote a more advanced tutorial to leverage these developments in an even better looking application. You can find a very complete description here: 
https://dev.epicgames.com/community/learning/tutorials/mmL/a-diy-flight-simulator-tutorial

## Key Mappings

Gamepad Layout 
![enter image description here](https://support.8bitdo.com/Manual/USB_Adapter/images/manual/ps4/ps4_switch.svg?20210414)
### Flight Commands
|Command|Key Shortcut|Gamepad  
|-|-|-|
|Toggle Engines Starters On/Off| CTRL-Q |
|Toggle Engines Mixture On/Off| CTRL-W |
|Toggle Engines Running On/Off | CTRL-E |
|Toggle Engines CutOff On/Off  | CTRL-R |
|Throttle - Cut| 1
|Throttle - Decrease| 2|A
|Throttle - Increase| 3|B
|Throttle - Full| 4
|Flaps - Retract| 5
|Flaps - Decrease| 6| L
|Flaps - Increase| 7| R
|Flaps - Extend| 8
|Aileron - Left| NUM 4| L-Stick X
|Aileron - Right|NUM 6| L-Stick X
|Elevator - Up| NUM 2| L-Stick Y
|Elevator - Down|NUM 8|L-Stick Y
|Rudder - Left| NUM 0| ZL
|Rudder - Right|NUM ENTER| ZR
|Center Aileron & Rudder |NUM 5|
|Aileron Trim - Left| CTRL-LEFT |
|Aileron Trim - Right|CTRL-RIGHT|
|Elevator Trim - Up| CTRL-DOWN|
|Elevator Trim - Down|CTRL-UP|
|Rudder Trim - Left| CTRL-NUM-7|
|Rudder Trim - Right|CTRL-NUM-9|
|All Brakes | NUM . |Y	
|Left Brakes | NUM * |
|Right Brakes | NUM - |
|Parking Brakes | CTRL-NUM . |
|Toggle Gear Up/Down | G | L3

### Application
|Command|Shortcut|
|-|-|
|Toggle Pilot/Orbit Camera | TAB |
|Toggle FDM Debug Infos | D |
|Toggle Aircraft Trails | T |
|Orbit Camera Up/Dowm | RightMouseButton + Mouse Up/Down |
|Orbit Camera Left/Right | RightMouseButton + Mouse Left/Right|
|Orbit Camera Zoom In/Out | MiddleMouseButton(Wheel) + Up/Down |

### Environment

|Command|Shortcut|
|-|-|
|Time of day - Increase| PAGE DOWN|
|Time of day - Decrease| END|
|Time of day - Dawn Preset| INSERT|
|Time of day - Noon Preset| HOME|
|Time of day - Dusk Preset| PAGE UP|

## Developing

Make sure when you clone, you clone recursively, or after cloning, make sure you run:

```console
git submodule update --init --recursive
```

### Setting up Cesium Plugin

You'll need to ensure that the native libraries for cesium are built for your processor:

```console
cd Plugins/cesium-unreal/extern

# if you want to build debug
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DUNREAL_ENGINE_ROOT=/Users/Shared/Epic\ Games/UE_5.6

# if you want to build release
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUNREAL_ENGINE_ROOT=/Users/Shared/Epic\ Games/UE_5.6

# now actually build it
cmake --build build --target install --parallel 14
```

Afterwards, you'll need to make sure the libraries are available:

```console
cd Plugins/cesium-unreal/Source/ThirdParty/lib

# if you built debug above
ln -s ./Darwin-arm64-Debug Darwin-universal-Debug

# if you built release above
ln -s ./Darwin-arm64-Release Darwin-universal-Release
```
