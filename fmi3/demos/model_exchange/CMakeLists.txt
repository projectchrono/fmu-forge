set(COMPONENT_NAME "myFmuModelExchange_fmi3")
set(COMPONENT_MAIN_DIR "${CMAKE_BINARY_DIR}")
set(COMPONENT_SOURCES myFmuModelExchange_fmi3.h myFmuModelExchange_fmi3.cpp)
set(COMPONENT_RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/my_resources)
set(COMPONENT_DLL_DEPENDENCIES "")

#--------------------------------------------------------------

set(FMU_ME TRUE)
set(USE_CUSTOM_TYPESPLATFORM OFF CACHE BOOL "Use custom types platform definition.")

# RADU TODO
set(FMU_TESTING OFF)

#--------------------------------------------------------------

# Set include paths, compiler & linker flags, and libraries
set(COMPILER_FLAGS "")
set(LINKER_FLAGS "")
list(APPEND LIBS "")

# Set the minimum required C++ standard to C++14, allow C++17 if available
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_STANDARD 17)

#==============================================================

include(FetchContent)

#--------------------------------------------------------------

message(STATUS "...add ${COMPONENT_NAME}")
message(STATUS "   FetchContent(fmu-forge) from ${FMU_FORGE_DIR}")

set(FMU_MODEL_IDENTIFIER "${COMPONENT_NAME}" CACHE INTERNAL "")
set(FMU_RESOURCES_DIRECTORY ${COMPONENT_RESOURCES_DIR})
set(FMU_DEPENDENCIES ${COMPONENT_DLL_DEPENDENCIES})
set(FMU_MAIN_DIRECTORY ${COMPONENT_MAIN_DIR})
set(FMU_MSG_PREFIX "   - ")

FetchContent_Declare(
    ${FMU_MODEL_IDENTIFIER}
    SOURCE_DIR ${FMU_FORGE_DIR}/fmi3
)

FetchContent_MakeAvailable(${FMU_MODEL_IDENTIFIER})

string(TOUPPER ${FMU_MODEL_IDENTIFIER} FMU_MODEL_IDENTIFIER_UPPERCASE)
MARK_AS_ADVANCED(FETCHCONTENT_BASE_DIR)
MARK_AS_ADVANCED(FETCHCONTENT_FULLY_DISCONNECTED)
MARK_AS_ADVANCED(FETCHCONTENT_QUIET)
MARK_AS_ADVANCED(FETCHCONTENT_SOURCE_DIR_${FMU_MODEL_IDENTIFIER_UPPERCASE})
MARK_AS_ADVANCED(FETCHCONTENT_UPDATES_DISCONNECTED)
MARK_AS_ADVANCED(FETCHCONTENT_UPDATES_DISCONNECTED_${FMU_MODEL_IDENTIFIER_UPPERCASE})

#--------------------------------------------------------------
# add to the FMU creation target

target_sources(${FMU_MODEL_IDENTIFIER} PRIVATE ${COMPONENT_SOURCES})

set_target_properties(${FMU_MODEL_IDENTIFIER} PROPERTIES FOLDER demos COMPILE_FLAGS "${COMPILER_FLAGS}" LINK_FLAGS "${LINKER_FLAGS}")
target_link_libraries(${FMU_MODEL_IDENTIFIER} ${LIBS})  

source_group("" FILES ${COMPONENT_SOURCES})

#==============================================================

message(STATUS "...add instantiation demo for ${COMPONENT_NAME}")

set(DEMO demo_ModelExchange_fmi3)
set(DEMO_SOURCES demo_ModelExchange_fmi3.cpp)
source_group("" FILES ${DEMO_SOURCES})

add_executable(${DEMO} ${DEMO_SOURCES})

target_include_directories(${DEMO} PRIVATE "${CMAKE_SOURCES_DIR}")
target_include_directories(${DEMO} PRIVATE "${FMU_FORGE_DIR}")
target_compile_definitions(${DEMO} PUBLIC FMU_UNPACK_DIRECTORY="${FMU_DIRECTORY}/tmp_unpack")
target_compile_definitions(${DEMO} PUBLIC FMU_DIRECTORY="${FMU_DIRECTORY}")
target_compile_definitions(${DEMO} PUBLIC FMU_FILENAME="${FMU_FILENAME}")

target_compile_definitions(${DEMO} PUBLIC FMI3_PLATFORM="${FMI3_PLATFORM}")
target_compile_definitions(${DEMO} PUBLIC SHARED_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "6.0")
    target_link_options(${DEMO} PRIVATE "LINKER:-as-needed")
    target_link_libraries(${DEMO} PRIVATE stdc++fs)
    target_link_libraries(${DEMO} PRIVATE ${CMAKE_DL_LIBS})
endif()

add_dependencies(${DEMO} ${COMPONENT_NAME})
