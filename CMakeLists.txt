cmake_minimum_required(VERSION 3.21)

option(ENABLE_UNITY_BUILD "" OFF)
option(ENABLE_IPO "" OFF)
option(ENABLE_ADDRESS_SANITIZER "" OFF)

# i need to do this because shadermake manually & retardedly sets 'CMAKE_MSVC_RUNTIME_LIBRARY'
set(CMAKE_MSVC_RUNTIME_LIBRARY 
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" 
    CACHE STRING "MSVC runtime library" FORCE)

project(ToyRenderer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SUPPRESS_REGENERATION true)

# Global compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi")     # Program Database
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")     # Multi-Processor compilation

# Set iterator debug level to 0 for all targets (no iterator debugging)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_ITERATOR_DEBUG_LEVEL=0")

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

set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})

################################################################################
# ToyRenderer main app

# src files
file(GLOB_RECURSE BASE_SRC "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp" "${SRC_DIR}/*.inl")
file(GLOB IMGUI_SRC "${EXTERN_DIR}/imgui/*.*" "${EXTERN_DIR}/imgui/backends/imgui_impl_sdl3.*")

set(TOYRENDERER_SRC ${BASE_SRC} ${IMGUI_SRC})

set(NVRHI_WITH_DX12 ON)
set(NVRHI_WITH_AFTERMATH OFF)
set(NVRHI_WITH_RTXMU ON)
set(AFTERMATH_SEARCH_PATHS "${EXTERN_DIR}/nvidia/aftermath")
set(TF_BUILD_TESTS OFF)
set(TF_BUILD_EXAMPLES OFF)
set(NRD_DISABLE_SHADER_COMPILATION ON)
set(NRD_EMBEDS_SPIRV_SHADERS OFF)
set(NRD_EMBEDS_DXIL_SHADERS OFF)
set(NRD_EMBEDS_DXBC_SHADERS OFF)

add_subdirectory(extern/shadermake)
add_subdirectory(extern/basis_universal)
add_subdirectory(extern/meshoptimizer)
add_subdirectory(extern/d3d12ma)
add_subdirectory(extern/microprofile)
add_subdirectory(extern/nvrhi)
add_subdirectory(extern/taskflow)
add_subdirectory(extern/nvidia/NRD)

add_executable(ToyRenderer WIN32 ${TOYRENDERER_SRC})

target_compile_definitions(ToyRenderer PUBLIC "/W4") # Level 4 warnings
target_compile_definitions(ToyRenderer PUBLIC "/WX") # Warnings as errors

# startup proj = 'ToyRenderer'
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ToyRenderer)

set(TOYRENDERER_LIBRARIES
	d3d12 dxgi dxguid Ws2_32
	ShaderMakeBlob
	basisu_encoder
	meshoptimizer
	D3D12MemoryAllocator
	microprofile
	nvrhi
	nvrhi_d3d12
	Taskflow
	NRD
	"${BIN_DIR}/SDL3.lib"
)

# libs
target_link_libraries(ToyRenderer PUBLIC ${TOYRENDERER_LIBRARIES})

# force include PCH.h
set(PCH_FILE "${SRC_DIR}/PCH.h")
set_target_properties(ToyRenderer PROPERTIES COMPILE_FLAGS "/FI \"${PCH_FILE}\"")

# force include dirs
set(TOYRENDERER_INCLUDE_DIRS ${ROOT_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${SRC_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${EXTERN_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/ShaderMake/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/amd/FidelityFX/sdk/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/imgui")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/magic_enum/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/SDL")
target_include_directories(ToyRenderer PUBLIC ${TOYRENDERER_INCLUDE_DIRS})

# global preprocessor macros
target_compile_definitions(ToyRenderer PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ToyRenderer PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
target_compile_definitions(ToyRenderer PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

target_compile_definitions(microprofile PUBLIC MICROPROFILE_GPU_TIMERS MICROPROFILE_GPU_TIMERS_D3D12)

target_compile_definitions(nvrhi_d3d12 PUBLIC NVRHI_D3D12_WITH_D3D12MA D3D12MA_INCLUDE_DIR="${EXTERN_DIR}/d3d12ma/include/D3D12MemAlloc.h")

# disable as many transcoder formats as possible to save code size
target_compile_definitions(basisu_encoder PUBLIC BASISU_FORCE_DEVEL_MESSAGES=1)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_ASTC=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_ATC=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_BC7_MODE5=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_DXT1=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_DXT5A=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_ETC2_EAC_A8=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_ETC2_EAC_RG11=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_FXT1=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_PVRTC1=0)
target_compile_definitions(basisu_encoder PUBLIC BASISD_SUPPORT_PVRTC2=0)
target_compile_definitions(basisu_encoder PRIVATE "/W0")

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