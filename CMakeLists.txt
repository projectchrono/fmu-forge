cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW) # MSVC runtime library flags are selected by an abstraction i.e. CMAKE_MSVC_RUNTIME_LIBRARY

project(fmu-forge)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
mark_as_advanced(FORCE CMAKE_INSTALL_PREFIX)

# -------------------------------------------------
# Set FMU_FORGE_DIR to point to the location of this script
# (in this case, this is the top-level fmu-forge directory)

set(FMU_FORGE_DIR ${CMAKE_SOURCE_DIR})

# -------------------------------------------------
# Add the various demo subdirectories

message(STATUS "\nExamples for FMI 2.0")
add_subdirectory(examples/fmi2/cosimulation)
add_subdirectory(examples/fmi2/model_exchange)

message(STATUS "\nExamples for FMI 3.0")
add_subdirectory(examples/fmi3/cosimulation)
add_subdirectory(examples/fmi3/model_exchange)
