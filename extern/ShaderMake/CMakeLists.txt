cmake_minimum_required(VERSION 3.21)

# only Release
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Limited configurations" FORCE)

project(ShaderMake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SUPPRESS_REGENERATION true)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP") # Multi-Processor compilation

set(BIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../bin")

# all binaries for all config types (if any) go to the "bin" folder
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
endforeach()

file(GLOB_RECURSE SHADER_MAKE_SRC "*.cpp" "*.h")

add_executable(ShaderMake ${SHADER_MAKE_SRC})

target_include_directories(ShaderMake PUBLIC "include")

target_compile_definitions (ShaderMake PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_options (ShaderMake PRIVATE "/DELAYLOAD:dxcompiler.dll")
target_link_libraries (ShaderMake d3dcompiler dxcompiler delayimp)
