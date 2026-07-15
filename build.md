## Building the application 

**1. Install Unreal Engine 5.7**

The procedure to install Unreal Engine is described here: https://www.unrealengine.com/en-US/download
For hobbyists, the [standard license](https://www.unrealengine.com/en-US/license) applies, and is 100% free!

For Windows builds, use Visual Studio 2022 with the Unreal Engine C++ workload and make sure these components are installed:
- Windows 10/11 SDK `10.0.22621.0` or newer
- MSVC v143 C++ x64/x86 build tools (`14.38` or newer)
- .NET SDK required by Unreal Build Tool

It is also recommended to set up Visual Studio for Unreal using the following procedures:
[https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/)
[https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-unrealvs-extension-for-unreal-engine-cplusplus-projects/](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-unrealvs-extension-for-unreal-engine-cplusplus-projects/)

**Cesium for Unreal**

This project expects the `CesiumForUnreal` plugin to be available when opening the project. Either:
- install the plugin into the UE 5.7 engine from Fab / Marketplace, or
- initialize the `Plugins\cesium-unreal` submodule and build it from source.

**2. Build JSBSim as Dynamic libraries and stage Model files**

Unreal Engine requires that one plugin contains all its needed files in its sub-folders. 
This application contains a `Plugins/JSBSimFlightDynamicsModel` folder containing the JSBSim files.
In some of these subfolders, one has to place 
 - The JSBSim libraries, compiled as dynamic libs  
 - The aircrafts/engine/systems definition files.

When the UE application will be packaged, the resources will be copied along with the executable, and the application dynamically linked against the libs transparently.

To make this process easier, there is a solution named `JSBSimForUnreal.sln` at the root of the JSBSim repo. 

 - Open and build this solution with VS2022 in `Release|x64` (and in `Debug|x64` too if you want, but this is not mandatory)
 - If the project is pinned to an older Windows SDK or toolset on your machine, retarget it to your installed Windows SDK and MSVC v143 toolset before building
 - It will take care of making a clean build, and copy all needed files at the right location
	 - All libs and headers in `UnrealEngine\Plugins\JSBSimFlightDynamicsModel\Source\ThirdParty\JSBSim`
	 - All resource files (aircrafts/engines/systems) in *UnrealEngine\Plugins\JSBSimFlightDynamicsModel\Resources\JSBSim*
 
**3. [Optional] - Download HD resources**
 In order to keep the JSBSim repository lightweight, this application contains low quality resources. 
 If you would like to use better looking content, you can download HQ aircraft model, HD textures and non-flat terrain here: 
 [High Definition content pack (330 MB)](https://epicgames.box.com/s/93mupzix8qieu51v209ockq68heuxgwj)
 
 Simply extract this archive and copy/paste the content folder into the one of UEReferenceApp, overriding the existing files. 
 
**4. Build/Open the Unreal Project**

**Option 1**: Simply double click on the `UnrealSpaceProgram.uproject` file.
It will open a popup complaining about missing modules (`UnrealSpaceProgram`, `JSBSimFlightDynamicsModel`, `JSBSimFlightDynamicsModelEditor`).
Answer Yes, and the build will be triggered as a background task. 

Once done, the UE Editor will open. If you get an error message, build manually using Option 2 below. 

**Option 2** : Generate a project solution, and build it using Visual Studio. 
Right click on `UnrealSpaceProgram.uproject`.
A contextual menu will appear. Select "Generate Visual Studio project files".
After a short time, a new solution file `UnrealSpaceProgram.sln` will appear beside the uproject file.
Open it, and "Build Startup project" from the UnrealVS Extension bar.

Note that this Option 2 is the recommended way to edit the plugin code, and then you can run and debug it like any other VS application. 
