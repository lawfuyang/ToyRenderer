cmake_minimum_required(VERSION 3.21)

option(ENABLE_UNITY_BUILD "" OFF)
option(ENABLE_IPO "" OFF)
option(ENABLE_ADDRESS_SANITIZER "" OFF)

set(AGILITY_SDK_VERSION "1.615.1")
set(DXC_VERSION "v1.8.2502/dxc_2025_02_20")
set(SDL3_VERSION "3.2.14")
set(RENDERDOC_VERSION "1.38")

set(NRD_NORMAL_ENCODING 2 CACHE STRING "")
set(NRD_ROUGHNESS_ENCODING 1 CACHE STRING "")

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(BIN_DIR "${ROOT_DIR}/bin")
set(SRC_DIR "${ROOT_DIR}/source")
set(EXTERN_DIR "${ROOT_DIR}/extern")

# Helper to download and unzip a package from a URL
# Uses a zero-length file to identify the version of the package
# copied from https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/CMakeLists.txt
function(CheckAndDownloadPackage NAME VERSION LOCAL_PATH URL)
    # Do we already have the correct version?
    if(NOT EXISTS ${LOCAL_PATH}/${VERSION}.ver)
        # Was there a previous version that we need to delete?
        if(EXISTS ${LOCAL_PATH})
            message(STATUS "Deleting old " ${NAME})
            file(REMOVE_RECURSE ${LOCAL_PATH})
        endif()
        message(STATUS "Obtaining " ${NAME} " " ${VERSION})
        file(DOWNLOAD ${URL} ${LOCAL_PATH}.zip)
        message(STATUS "Extracting " ${NAME})
        file(ARCHIVE_EXTRACT INPUT ${LOCAL_PATH}.zip DESTINATION ${LOCAL_PATH})
        file(REMOVE ${LOCAL_PATH}.zip)
        # Create an empty file so we know which version we have
        file(WRITE ${LOCAL_PATH}/${VERSION}.ver)
    endif()
endfunction()

CheckAndDownloadPackage("Agility SDK" ${AGILITY_SDK_VERSION} ${EXTERN_DIR}/agilitysdk https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${AGILITY_SDK_VERSION})
CheckAndDownloadPackage("DXC" ${DXC_VERSION} ${EXTERN_DIR}/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/${DXC_VERSION}.zip)
CheckAndDownloadPackage("SDL" ${SDL3_VERSION} ${EXTERN_DIR}/sdl https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-devel-${SDL3_VERSION}-VC.zip)
CheckAndDownloadPackage("RenderDoc" ${RENDERDOC_VERSION} ${EXTERN_DIR}/renderdoc https://renderdoc.org/stable/${RENDERDOC_VERSION}/RenderDoc_${RENDERDOC_VERSION}_64.zip)

# create bin dir
file(MAKE_DIRECTORY "${BIN_DIR}")

# symlink dlls
execute_process(
COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${EXTERN_DIR}/agilitysdk/build/native/bin/x64/D3D12Core.dll
        ${BIN_DIR}/D3D12Core.dll
)

execute_process(
COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${EXTERN_DIR}/agilitysdk/build/native/bin/x64/d3d12SDKLayers.dll
        ${BIN_DIR}/d3d12SDKLayers.dll
)

execute_process(
COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${EXTERN_DIR}/sdl/SDL3-${SDL3_VERSION}/lib/x64/SDL3.dll
        ${BIN_DIR}/SDL3.dll
)

execute_process(
COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${EXTERN_DIR}/renderdoc/RenderDoc_${RENDERDOC_VERSION}_64/renderdoc.dll
        ${BIN_DIR}/renderdoc.dll
)

execute_process(
COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${EXTERN_DIR}/renderdoc/RenderDoc_${RENDERDOC_VERSION}_64/renderdoc_app.h
        ${EXTERN_DIR}/renderdoc/renderdoc_app.h
)

# i need to do this because shadermake manually & retardedly sets 'CMAKE_MSVC_RUNTIME_LIBRARY'
set(CMAKE_MSVC_RUNTIME_LIBRARY 
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" 
    CACHE STRING "MSVC runtime library" FORCE)
	
project(ToyRenderer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SUPPRESS_REGENERATION true)

# Global compiler flags
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_ITERATOR_DEBUG_LEVEL=0")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "/W0" DIRECTORY ${EXTERN_DIR})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "-w" DIRECTORY ${EXTERN_DIR})
endif()

if (ENABLE_ADDRESS_SANITIZER)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

if (ENABLE_IPO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

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
set(NVRHI_WITH_VULKAN ON)
set(NVRHI_WITH_AFTERMATH OFF)
set(NVRHI_WITH_RTXMU OFF)
set(AFTERMATH_SEARCH_PATHS "${EXTERN_DIR}/nvidia/aftermath")
set(TF_BUILD_TESTS OFF)
set(TF_BUILD_EXAMPLES OFF)
set(NRD_STATIC_LIBRARY ON)
set(NRD_DISABLE_SHADER_COMPILATION ON)
set(NRD_EMBEDS_SPIRV_SHADERS OFF)
set(NRD_EMBEDS_DXIL_SHADERS OFF)
set(NRD_EMBEDS_DXBC_SHADERS OFF)
set(RTXGI_BUILD_SAMPLES OFF)
set(RTXGI_STATIC_LIB ON)
set(SHADERMAKE_FIND_FXC OFF)
set(SHADERMAKE_FIND_DXC OFF)

add_subdirectory(extern/shadermake)
add_subdirectory(extern/meshoptimizer)
add_subdirectory(extern/microprofile)
add_subdirectory(extern/nvrhi)
add_subdirectory(extern/taskflow)
add_subdirectory(extern/nvidia/NRD)
add_subdirectory(extern/nvidia/RTXGI-DDGI)

add_executable(ToyRenderer WIN32 ${TOYRENDERER_SRC})

target_compile_options(ToyRenderer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/WX /W1>
    $<$<CXX_COMPILER_ID:Clang>:-Werror>
)

# startup proj = 'ToyRenderer'
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ToyRenderer)

set(TOYRENDERER_LIBRARIES
	d3d12 dxgi dxguid Ws2_32
	ShaderMakeBlob
	meshoptimizer
	microprofile
	nvrhi
	nvrhi_d3d12
	nvrhi_vk
	Taskflow
	NRD
	RTXGI-D3D12
	"${EXTERN_DIR}/sdl/SDL3-${SDL3_VERSION}/lib/x64/SDL3.lib"
)

# libs
target_link_libraries(ToyRenderer PUBLIC ${TOYRENDERER_LIBRARIES})

# PCH
target_precompile_headers(ToyRenderer PRIVATE "${SRC_DIR}/PCH.h")

# force include dirs
set(TOYRENDERER_INCLUDE_DIRS ${ROOT_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${SRC_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} ${EXTERN_DIR})
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/ShaderMake/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/amd/FidelityFX/sdk/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/imgui")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/magic_enum/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/sdl/SDL3-${SDL3_VERSION}/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/nvidia/RTXGI-DDGI/rtxgi-sdk/include")
set(TOYRENDERER_INCLUDE_DIRS ${TOYRENDERER_INCLUDE_DIRS} "${EXTERN_DIR}/agilitysdk/build/native/include")
target_include_directories(ToyRenderer PUBLIC ${TOYRENDERER_INCLUDE_DIRS})

# global preprocessor macros
target_compile_definitions(ToyRenderer PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ToyRenderer PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
target_compile_definitions(ToyRenderer PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

target_compile_definitions(microprofile PUBLIC MICROPROFILE_GPU_TIMERS MICROPROFILE_GPU_TIMERS_D3D12)

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

# Shaders do not participate in the build
set_source_files_properties(${SHADER_SRC} PROPERTIES VS_TOOL_OVERRIDE "None")

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