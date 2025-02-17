cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW) # MSVC runtime library flags are selected by an abstraction i.e. CMAKE_MSVC_RUNTIME_LIBRARY

project(fmu_tools)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
mark_as_advanced(FORCE CMAKE_INSTALL_PREFIX)

# # -------------------------------------------------
# # Set FMU_TOOLS_DIR to point to the location of this script
# # (in this case, this is the top-level fmu_tools directory)

set(FMU_TOOLS_DIR ${CMAKE_SOURCE_DIR})

# -------------------------------------------------
# Add the various demo subdirectories

message(STATUS "\nDemos for FMI 2.0")
add_subdirectory(fmi2/demos/cosimulation)
add_subdirectory(fmi2/demos/model_exchange)

message(STATUS "\nDemos for FMI 3.0")
add_subdirectory(fmi3/demos/cosimulation)
add_subdirectory(fmi3/demos/model_exchange)
