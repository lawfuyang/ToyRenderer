cmake_minimum_required(VERSION 3.21)

option(ENABLE_UNITY_BUILD "" OFF)
option(ENABLE_IPO "" OFF)
option(ENABLE_ADDRESS_SANITIZER "" OFF)

# Set available build types
set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "Limited configurations" FORCE)

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
file(GLOB IMGUI_SRC "${EXTERN_DIR}/imgui/*.*" "${EXTERN_DIR}/imgui/backends/imgui_impl_win32.*" )
file(GLOB_RECURSE SIMPLEMATH_SRC "${EXTERN_DIR}/simplemath/*.*")
file(GLOB_RECURSE STB_SRC "${EXTERN_DIR}/stb/*.*")
file(GLOB_RECURSE SHADERMAKE_SRC "${EXTERN_DIR}/ShaderMake/src/argparse.cpp" "${EXTERN_DIR}/ShaderMake/src/ShaderBlob.cpp")
file(GLOB_RECURSE KTX_TRANSCODER_SRC "${EXTERN_DIR}/ktx_transcoder/*.*")
file(GLOB_RECURSE ZSTD_SRC "${EXTERN_DIR}/zstd/*.*")
file(GLOB_RECURSE D3D12MA_SRC "${EXTERN_DIR}/d3d12ma/*.*")
file(GLOB_RECURSE MESHOPTIMIZER_SRC "${EXTERN_DIR}/meshoptimizer/*.*")

set(TOYRENDERER_SRC ${BASE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${NVRHI_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${TASKFLOW_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${MICROPROFILE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${IMGUI_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${SIMPLEMATH_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${SHADERMAKE_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${KTX_TRANSCODER_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${ZSTD_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${D3D12MA_SRC})
set(TOYRENDERER_SRC ${TOYRENDERER_SRC} ${MESHOPTIMIZER_SRC})

add_executable(ToyRenderer WIN32 ${TOYRENDERER_SRC})

# startup proj = 'ToyRenderer'
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ToyRenderer)

# libs
target_link_libraries(ToyRenderer PUBLIC d3d12 dxgi dxguid Ws2_32 "${EXTERN_DIR}/nvidia/aftermath/GFSDK_Aftermath_Lib.x64.lib")

# force include PCH.h
set(PCH_FILE "${SRC_DIR}/PCH.h")
set_target_properties(ToyRenderer PROPERTIES COMPILE_FLAGS "/FI \"${PCH_FILE}\"")

# force include dirs
set(TOYRENDERER_INCLUDE_DIRS ${ROOT_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${SRC_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${EXTERN_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/nvrhi/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/ShaderMake/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/amd/FidelityFX/sdk/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/nvidia/aftermath")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/imgui")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/magic_enum/include")
target_include_directories(ToyRenderer PUBLIC ${TOYRENDERER_INCLUDE_DIRS})

# global preprocessor macros
target_compile_definitions(ToyRenderer PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ToyRenderer PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
target_compile_definitions(ToyRenderer PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_definitions(ToyRenderer PUBLIC MICROPROFILE_GPU_TIMERS MICROPROFILE_GPU_TIMERS_D3D12)
target_compile_definitions(ToyRenderer PUBLIC DEBUG_DRAW_CXX11_SUPPORTED)
target_compile_definitions(ToyRenderer PUBLIC BASISU_FORCE_DEVEL_MESSAGES)
target_compile_definitions(ToyRenderer PUBLIC NVRHI_D3D12_WITH_D3D12MA NVRHI_WITH_AFTERMATH=1)

# disable as many transcoder formats as possible to save code size
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_ASTC=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_ATC=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_BC7_MODE5=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_DXT1=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_DXT5A=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_ETC2_EAC_A8=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_ETC2_EAC_RG11=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_FXT1=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_PVRTC1=0)
target_compile_definitions(ToyRenderer PUBLIC BASISD_SUPPORT_PVRTC2=0)

# disable checked iterators for perf
target_compile_definitions(ToyRenderer PUBLIC _ITERATOR_DEBUG_LEVEL=0)

if (ENABLE_UNITY_BUILD)
	set_target_properties(ToyRenderer PROPERTIES UNITY_BUILD ON)
	
	# unity builds results in large objs...
	target_compile_options(ToyRenderer PRIVATE /bigobj)
endif()

################################################################################

################################################################################
file(GLOB_RECURSE SHADER_SRC "${SRC_DIR}/*.hlsl" "${SRC_DIR}/*.hlsli")
set(SHADER_SRC_FILES ${SHADER_SRC})
add_custom_target(ShaderSourceFiles SOURCES ${SHADER_SRC_FILES})

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