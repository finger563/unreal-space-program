## Building the application 

 **1. Install Unreal Engine 5.x**
 
The procedure to install Unreal Engine is described here : https://www.unrealengine.com/en-US/download
For hobbyists, the [standard license](https://www.unrealengine.com/en-US/license) applies, and is 100% free! 

*Note:* In order to build C++ plugins for Unreal, you'll need at least Visual Studio 2019 v16.4.3 toolchain (14.24.28315) and Windows 10 SDK (10.0.18362.0). (Visual Studio Community can also be used)

---
***Important Note  for Visual Studio 2022 users***
As of 05/12/2022, Epic Games warns that some latests updates to VS2022 toolchain might break the build of UE applications. These issues will be addressed in a further UE hotfix release. While waiting for theses fixes, the recommended approach is to use the VS2019 Toolchain to build the application. 
You can still use VS2022 as an IDE, and don't need to have VS2019 IDE installed. Just its toolchain. 
1. From the Visual Studio installer, make sure you have these components installed:
- Windows 10 SDK (10.0.18362.0)
- MSVC v142 - VS2019 C++ x64/x86 build tools (v14.29-16.11)
2. Force Unreal Build tool to produce VS2019 projects
- Create a file named `"BuildConfiguration.xml"` in `%APPDATA%\Unreal Engine\UnrealBuildTool`  
   (ex: `C:\Users\JohnDoe\AppData\Roaming\Unreal Engine\UnrealBuildTool`) 
 - Edit the file so that he looks like this one with a CompilerVersion block :

>     <?xml version="1.0" encoding="utf-8" ?> 
>     <Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
>     <WindowsPlatform>
>         <CompilerVersion>14.29.30136</CompilerVersion>
>     </WindowsPlatform> </Configuration>
You can learn more about this option file [here](https://docs.unrealengine.com/4.27/en-US/ProductionPipelines/BuildTools/UnrealBuildTool/BuildConfiguration/)...

3. You need to do these changes before building/opening the UEReferenceApp project (step 4 below). 
If you already tried a build with VS2022, you'll need to clean all intermediare project files, by running the CleanProject.bat file in the UEReferenceApp root folder. 

*Thanks to Ali M. and James S. for reporting this special case issue.*

---
It is also recommended to set up Visual Studio for Unreal using the following procedures
[https://docs.unrealengine.com/5.0/en-US/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/](https://docs.unrealengine.com/5.0/en-US/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/)
[https://docs.unrealengine.com/5.0/en-US/using-the-unrealvs-extension-for-unreal-engine-cplusplus-projects/](https://docs.unrealengine.com/5.0/en-US/using-the-unrealvs-extension-for-unreal-engine-cplusplus-projects/)

**2. Build JSBSim as Dynamic libraries and stage Model files**

Unreal Engine requires that one plugin contains all its needed files in its sub-folders. 
This application contains a `Plugins/JSBSimFlightDynamicsModel` folder containing the JSBSim files.
In some of these subfolders, one has to place 
 - The JSBSim libraries, compiled as dynamic libs  
 - The aircrafts/engine/systems definition files.

When the UE application will be packaged, the resources will be copied along with the executable, and the application dynamically linked against the libs transparently.

To make this process easier, there is a new solution named JSBSimForUnreal.sln at the root of JSBSim repo. 

 - Simply open and build this solution with VS2019, in Release, (and in Debug if you want too, but this is not mandatory)
 - It will take care of making a clean build, and copy all needed files at the right location
	 - All libs and headers in `UnrealEngine\Plugins\JSBSimFlightDynamicsModel\Source\ThirdParty\JSBSim`
	 - All resource files (aircrafts/engines/systems) in *UnrealEngine\Plugins\JSBSimFlightDynamicsModel\Resources\JSBSim*
 
**3. [Optional] - Download HD resources**
 In order to keep the JSBSim repository lightweight, this application contains low quality resources. 
 If you would like to use better looking content, you can download HQ aircraft model, HD textures and non-flat terrain here: 
 [High Definition content pack (330 MB)](https://epicgames.box.com/s/93mupzix8qieu51v209ockq68heuxgwj)
 
 Simply extract this archive and copy/paste the content folder into the one of UEReferenceApp, overriding the existing files. 
 
**4. Build/Open the Unreal Project**

**Option 1** : Simply double click on the `UnrealEngine\UEReferenceApp.uproject` file.
It will open a popup complaining about missing modules (UEReferenceApp, JSBSimFlightDynamicsModel, JSBSimFlightDynamicsModelEditor). 
Answer Yes, and the build will be triggered as a background task. 

Once done, the UE Editor will open. If you get an error message, build manually using Option 2 below. 

**Option 2** : Generate a project solution, and build it using Visual Studio. 
Right click on the  `UnrealEngine\UEReferenceApp.uproject` 
A contextual menu will appear. Select "Generate Visual Studio project files"
After a short time, a new solution file `UEReferenceApp.sln` will appear beside the uproject file. 
Open it, and "Build Startup project" from the UnrealVS Extension bar. 

Note that this Option 2 is the recommended way to edit the plugin code, and then you can run and debug it like any other VS application. 
