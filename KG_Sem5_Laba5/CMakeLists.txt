cmake_minimum_required(VERSION 3.21)
project(KG_Sem5_Laba5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASSIMP_DIR "C:/assimp/assimp-6.0.5")

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(directstorage
    URL https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.directstorage/1.3.0/microsoft.direct3d.directstorage.1.3.0.nupkg
    URL_HASH SHA256=8ab6c1082537565388b79050d94fdd7f5f437cd0f1d909e7b19f786042b97177
    DOWNLOAD_NAME directstorage.zip)
FetchContent_MakeAvailable(directstorage)
set(DIRECTSTORAGE_NATIVE "${directstorage_SOURCE_DIR}/native")

FetchContent_Declare(directxmesh
    URL https://codeload.github.com/microsoft/DirectXMesh/tar.gz/refs/tags/may2026
    URL_HASH SHA256=f2fe15dbf59ea0b612a76b95e9cd3aa8542e42e67901c84fe23b88de09deca05
    DOWNLOAD_NAME directxmesh.tar.gz)
set(BUILD_TOOLS OFF)
FetchContent_MakeAvailable(directxmesh)

add_library(imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_compile_definitions(imgui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi imm32)

set(SRC_FILES
        src/model_loader.cpp
        src/Terrain.cpp
        src/main.cpp
        src/Window.cpp
        src/DirectXApp.cpp
        src/SettingsUI.cpp
        src/RenderingSystem.cpp
        src/Meshlets.cpp
        src/DirectStorageReader.cpp
        src/RayTracedShadows.cpp
        src/GBuffer.cpp
        src/DDSTextureLoader.cpp
        src/d3dUtil.cpp
        src/GameTimer.cpp
)

set(HEADER_FILES
        h/Terrain.h
        h/Window.h
        h/DirectXApp.h
        h/RenderingSystem.h
        h/Meshlets.h
        h/MeshletLimits.h
        h/DirectStorageReader.h
        h/RayTracedShadows.h
        h/GBuffer.h
        h/mesh_data.h
        h/DDSTextureLoader.h
        h/d3dUtil.h
        h/UploadBuffer.h
        h/GameTimer.h
        h/Vertex.h
        h/Material.h
        h/Light.h
        h/d3dx12.h
)

set(SHADER_FILES
        src/main_shader.hlsl
        src/lighting.hlsl
        src/particles.hlsl
        src/post_effects.hlsl
        src/atmosphere.hlsl
        src/rt_shadows.hlsl
        src/mesh_pipeline.hlsl
)

add_executable(CG_Sem2 WIN32
        ${SRC_FILES}
        ${HEADER_FILES}
        ${SHADER_FILES}
)

target_include_directories(CG_Sem2 PRIVATE
        ${ASSIMP_DIR}/include
        ${ASSIMP_DIR}/build/include
        ${CMAKE_SOURCE_DIR}/h
        ${CMAKE_SOURCE_DIR}/src
        "${DIRECTSTORAGE_NATIVE}/include"
)

target_compile_definitions(CG_Sem2 PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_directories(CG_Sem2 PRIVATE ${ASSIMP_DIR}/build/lib/Debug ${ASSIMP_DIR}/build/contrib/zlib/Debug)
target_link_libraries(CG_Sem2 PRIVATE
        imgui
        DirectXMesh
        assimp-vc143-mtd.lib
        zlibstaticd.lib
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
        windowscodecs.lib
        ole32.lib
        oleaut32.lib
        "${DIRECTSTORAGE_NATIVE}/lib/x64/dstorage.lib"
)

foreach(runtime dstorage.dll dstoragecore.dll)
    configure_file("${DIRECTSTORAGE_NATIVE}/bin/x64/${runtime}" "${CMAKE_CURRENT_BINARY_DIR}/${runtime}" COPYONLY)
endforeach()

if(MSVC)
    target_compile_options(CG_Sem2 PRIVATE /W4 /EHsc /utf-8)
endif()

set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

file(GLOB DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
list(SORT DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
find_program(DXC_EXECUTABLE dxc)
if(NOT DXC_EXECUTABLE AND DXC_CANDIDATES)
    list(GET DXC_CANDIDATES 0 DXC_EXECUTABLE)
endif()
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "DXC from the Windows SDK is required for mesh shaders and Inline Ray Tracing")
endif()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso"
    COMMAND "${DXC_EXECUTABLE}" -T cs_6_5 -E CS_Shadow -WX -O3
        -Fo "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/rt_shadows.hlsl"
    DEPENDS src/rt_shadows.hlsl VERBATIM)
add_custom_target(RTShaders DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso")
add_dependencies(CG_Sem2 RTShaders)

function(add_mesh_shader output profile entry)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}.cso"
        COMMAND "${DXC_EXECUTABLE}" -T ${profile} -E ${entry} -WX -O3
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/${output}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh_pipeline.hlsl"
        DEPENDS src/mesh_pipeline.hlsl src/main_shader.hlsl h/MeshletLimits.h VERBATIM)
endfunction()
add_mesh_shader(mesh_geometry ms_6_5 MS_Geometry)
add_mesh_shader(mesh_amplification as_6_5 AS_CullMeshlets)
add_mesh_shader(mesh_pixel ps_6_5 PS_Geometry)
add_custom_target(MeshShaders DEPENDS mesh_amplification.cso mesh_geometry.cso mesh_pixel.cso)
add_dependencies(CG_Sem2 MeshShaders)

include(CTest)
if(BUILD_TESTING)
    add_executable(MeshPipelineTests tools/test_mesh_pipeline.cpp
        src/Meshlets.cpp src/model_loader.cpp src/DirectStorageReader.cpp src/RenderingSystem.cpp src/GBuffer.cpp src/d3dUtil.cpp)
    target_include_directories(MeshPipelineTests PRIVATE h
        "${ASSIMP_DIR}/include" "${ASSIMP_DIR}/build/include" "${DIRECTSTORAGE_NATIVE}/include")
    target_compile_definitions(MeshPipelineTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_directories(MeshPipelineTests PRIVATE "${ASSIMP_DIR}/build/lib/Debug" "${ASSIMP_DIR}/build/contrib/zlib/Debug")
    target_link_libraries(MeshPipelineTests PRIVATE assimp-vc143-mtd.lib zlibstaticd.lib
        DirectXMesh d3d12 dxgi d3dcompiler "${DIRECTSTORAGE_NATIVE}/lib/x64/dstorage.lib")
    add_dependencies(MeshPipelineTests MeshShaders)
    foreach(mode geometry storage pso)
        add_test(NAME "MeshPipeline_${mode}" COMMAND MeshPipelineTests
            "${mode}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/treasure_chest")
    endforeach()
    set_tests_properties(MeshPipeline_pso PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(RTShadowTests tools/test_rt_shadows.cpp src/RayTracedShadows.cpp src/d3dUtil.cpp)
    target_include_directories(RTShadowTests PRIVATE h)
    target_compile_definitions(RTShadowTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(RTShadowTests PRIVATE d3d12 dxgi d3dcompiler)
    add_dependencies(RTShadowTests RTShaders)
    add_test(NAME RTShadowChecks COMMAND RTShadowTests)
    set_tests_properties(RTShadowChecks PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(AtmosphereTests tools/test_atmosphere.cpp)
    target_compile_definitions(AtmosphereTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        ATMOSPHERE_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(AtmosphereTests PRIVATE d3d11 d3dcompiler)
    add_test(NAME AtmosphereChecks COMMAND AtmosphereTests "${CMAKE_SOURCE_DIR}")
    add_executable(TerrainTests tools/test_terrain.cpp src/Terrain.cpp)
    target_include_directories(TerrainTests PRIVATE h)
    target_compile_definitions(TerrainTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(TerrainTests PRIVATE /UNDEBUG)
    endif()
    add_test(NAME TerrainChecks COMMAND TerrainTests "${CMAKE_SOURCE_DIR}/assets/terrain")
endif()
