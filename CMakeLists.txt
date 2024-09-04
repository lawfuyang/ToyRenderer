cmake_minimum_required(VERSION 3.21)

option(ENABLE_UNITY_BUILD "" OFF)
option(ENABLE_IPO "" OFF)
option(ENABLE_ADDRESS_SANITIZER "" OFF)
option(GENERATE_SHADERMAKE "" OFF)

# Set available build types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Limited configurations" FORCE)

project(ToyRenderer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SUPPRESS_REGENERATION true)

# Global compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi")     # Program Database
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")     # Multi-Processor compilation
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")     # Level 4 warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")     # Warnings as errors

if (ENABLE_ADDRESS_SANITIZER)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

if (ENABLE_IPO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(BIN_DIR "${ROOT_DIR}/bin")
set(SRC_DIR "${ROOT_DIR}/source")
set(EXTERN_DIR "${ROOT_DIR}/extern")

# disable warnings for extern. we dont care about them
set_directory_properties(PROPERTIES COMPILE_OPTIONS "/W0" DIRECTORY ${EXTERN_DIR})

# all binaries for all config types (if any) go to the "bin" folder
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIR})
endforeach()

################################################################################
# ToyRenderer main app

# src files
file(GLOB_RECURSE BASE_SRC "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp" "${SRC_DIR}/*.inl")
file(GLOB_RECURSE NVRHI_SRC "${EXTERN_DIR}/nvrhi/src/d3d12/*" "${EXTERN_DIR}/nvrhi/src/validation/*" "${EXTERN_DIR}/nvrhi/src/common/*" "${EXTERN_DIR}/nvrhi/*.h")
file(GLOB_RECURSE TASKFLOW_SRC "${EXTERN_DIR}/taskflow/*.*")
file(GLOB_RECURSE MICROPROFILE_SRC "${EXTERN_DIR}/microprofile/*.*")
file(GLOB_RECURSE SPDLOG_SRC "${EXTERN_DIR}/spdlog/*.cpp" "${EXTERN_DIR}/spdlog/*.h")
file(GLOB_RECURSE IMGUI_SRC "${EXTERN_DIR}/imgui/*.*")
file(GLOB_RECURSE SIMPLEMATH_SRC "${EXTERN_DIR}/simplemath/*.*")
file(GLOB_RECURSE STB_SRC "${EXTERN_DIR}/stb/*.*")
file(GLOB_RECURSE SHADERMAKE_SRC "${EXTERN_DIR}/ShaderMake/src/argparse.cpp" "${EXTERN_DIR}/ShaderMake/src/ShaderBlob.cpp")
file(GLOB_RECURSE KTX_TRANSCODER_SRC "${EXTERN_DIR}/ktx_transcoder/*.*")
file(GLOB_RECURSE ZSTD_SRC "${EXTERN_DIR}/zstd/*.*")
file(GLOB_RECURSE D3D12MA_SRC "${EXTERN_DIR}/d3d12ma/*.*")

set(TOYRENDERER_SRC ${BASE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${NVRHI_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${TASKFLOW_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${MICROPROFILE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${SPDLOG_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${IMGUI_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${SIMPLEMATH_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${SHADERMAKE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${KTX_TRANSCODER_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${ZSTD_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${D3D12MA_SRC})
add_executable(ToyRenderer ${TOYRENDERER_SRC})

# startup proj = 'ToyRenderer'
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ToyRenderer)

# libs
target_link_libraries(ToyRenderer PUBLIC d3d12 dxgi dxguid Ws2_32)

# force include PCH.h
set(PCH_FILE "${SRC_DIR}/PCH.h")
set_target_properties(ToyRenderer PROPERTIES COMPILE_FLAGS "/FI \"${PCH_FILE}\"")

# force include dirs
set(TOYRENDERER_INCLUDE_DIRS ${ROOT_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${SRC_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${EXTERN_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/nvrhi/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/ShaderMake/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${EXTERN_DIR}/spdlog/include)
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/amd/FidelityFX/include")
target_include_directories(ToyRenderer PUBLIC ${TOYRENDERER_INCLUDE_DIRS})

# global preprocessor macros
target_compile_definitions(ToyRenderer PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ToyRenderer PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
target_compile_definitions(ToyRenderer PUBLIC SPDLOG_COMPILED_LIB SPDLOG_NO_SOURCE_LOC SPDLOG_NO_THREAD_ID SPDLOG_NO_ATOMIC_LEVELS SPDLOG_DISABLE_DEFAULT_LOGGER)
target_compile_definitions(ToyRenderer PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_OBSOLETE_KEYIO)
target_compile_definitions(ToyRenderer PUBLIC MICROPROFILE_GPU_TIMERS MICROPROFILE_GPU_TIMERS_D3D12)
target_compile_definitions(ToyRenderer PUBLIC JSON_USE_IMPLICIT_CONVERSIONS=0)
target_compile_definitions(ToyRenderer PUBLIC STBI_MAX_DIMENSIONS=8192)
target_compile_definitions(ToyRenderer PUBLIC TINYGLTF_NO_INCLUDE_JSON TINYGLTF_NO_EXTERNAL_IMAGE TINYGLTF_NO_STB_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE)
target_compile_definitions(ToyRenderer PUBLIC DEBUG_DRAW_CXX11_SUPPORTED)
target_compile_definitions(ToyRenderer PUBLIC BASISU_FORCE_DEVEL_MESSAGES)
target_compile_definitions(ToyRenderer PUBLIC NVRHI_D3D12_WITH_D3D12MA)

# disable checked iterators for perf
target_compile_definitions(ToyRenderer PUBLIC _ITERATOR_DEBUG_LEVEL=0)

if (ENABLE_UNITY_BUILD)
	set_target_properties(ToyRenderer PROPERTIES UNITY_BUILD ON)
	
	# unity builds results in large objs...
	target_compile_options(ToyRenderer PRIVATE /bigobj)
	
	# LibraryImplementations.cpp must explicitly not be in Unity build
	file(GLOB_RECURSE LIBRARY_IMPLEMENTATIONS_SRC "${SRC_DIR}/LibraryImplementations.cpp")
	set(NO_UNITY_FILES ${LIBRARY_IMPLEMENTATIONS_SRC})
	
	# AMD Fidelity FX SSSR blue noise distribution arrays defines a bunch of similarly named stuff
	set(NO_UNITY_FILES ${NO_UNITY_FILES} ${AMD_FFX_BLUE_NOISE_ERROR_DISTRO_SRC})
	
	# Set UNITY_BUILD property to OFF for the excluded file
	set_source_files_properties(${NO_UNITY_FILES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
endif()

# On Windows - FXC and DXC are part of WindowsSDK and there's also DXC in VulkanSDK which supports SPIR-V
if (DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
	set (WINDOWS_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
elseif (DEFINED ENV{WindowsSDKLibVersion})
	string (REGEX REPLACE "\\\\$" "" WINDOWS_SDK_VERSION "$ENV{WindowsSDKLibVersion}")
else ()
	message (FATAL_ERROR "WindowsSDK is not installed (CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is not defined; WindowsSDKLibVersion is '$ENV{WindowsSDKLibVersion}')!")
endif ()

if (${WINDOWS_SDK_VERSION} STRLESS "10.0.20348")
	message(FATAL_ERROR "Windows SDK version 10.0.20348 or later is required")
endif()

get_filename_component (WINDOWS_SDK_ROOT "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
set (WINDOWS_SDK_BIN "${WINDOWS_SDK_ROOT}/bin/${WINDOWS_SDK_VERSION}/x64")

find_program (DXC_PATH "${WINDOWS_SDK_BIN}/dxc")
if (NOT DXC_PATH)
	message (FATAL_ERROR "Can't find DXC in WindowsSDK: ${WINDOWS_SDK_BIN}")
endif ()
message (STATUS "Setting 'DXC_PATH' to '${DXC_PATH}'")

################################################################################

################################################################################
file(GLOB_RECURSE SHADER_SRC "${SRC_DIR}/*.hlsl" "${SRC_DIR}/*.hlsli")
set(SHADER_SRC_FILES ${SHADER_SRC})
add_custom_target(ShaderSourceFiles SOURCES ${SHADER_SRC_FILES})

################################################################################

################################################################################
# SHADER MAKE
if (GENERATE_SHADERMAKE)
    file(GLOB_RECURSE SHADER_MAKE_SRC "${EXTERN_DIR}/ShaderMake/src/*.cpp" "${EXTERN_DIR}/ShaderMake/src/*.h")

	add_executable(ShaderMake ${SHADER_MAKE_SRC})

	target_include_directories(ShaderMake PUBLIC "${EXTERN_DIR}/ShaderMake/include")

	target_compile_definitions (ShaderMake PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
	target_link_options (ShaderMake PRIVATE "/DELAYLOAD:dxcompiler.dll")
	target_link_libraries (ShaderMake d3dcompiler dxcompiler delayimp)
endif()
################################################################################


################################################################################
# SHADER COMPILATION

# compile shaders first before running the app, to ensure latest and proper Shader binaries
add_custom_target(CompileShaders
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling Shaders..."
    COMMAND ${CMAKE_COMMAND} -E env ${ROOT_DIR}/compileallshaders.bat
    VERBATIM
)
# Add a dependency to enforce the order
add_dependencies(ToyRenderer CompileShaders)
################################################################################