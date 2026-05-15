Horizon OC Compilation Instructions

1. Install devkitpro (https://devkitpro.org/wiki/Getting_Started) with switch-dev
2. Set up a development enviorment for compiling Atmosphere (https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/building.md)
3. Install GNU make and ENSURE THAT YOUR ENVIORMENT HAS A PYTHON3 COMMAND AVAILABLE!
4. Git clone atmosphere (git clone https://github.com/Atmosphere-NX/Atmosphere.git)
5. Clone the Horizon OC develop branch (git clone https://github.com/Horizon-OC/Horizon-OC.git --recurse-submodules)
6. Create a new folder named "build" in the horizon oc repo
7. Copy atmosphere files into that build folder
8. Copy Source/Atmosphere/stratosphere/loader/source/ldr_process_creation.cpp to build/stratosphere/loader/source/ldr_process_creation.cpp, replacing any files if prompted
9. Run ./build.sh in the root directory
