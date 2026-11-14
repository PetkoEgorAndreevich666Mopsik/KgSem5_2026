cmake_minimum_required(VERSION 3.20)
project(KG_Sem5_Laba4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASSIMP_DIR "C:/assimp/assimp-6.0.5")

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
        assimp-vc143-mtd.lib
        zlibstaticd.lib
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
        windowscodecs.lib
        ole32.lib
        oleaut32.lib
)

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
    message(FATAL_ERROR "DXC from the Windows SDK is required for the raytracing pipeline")
endif()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso"
    COMMAND "${DXC_EXECUTABLE}" -T lib_6_3 -WX -O3
        -Fo "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/rt_shadows.hlsl"
    DEPENDS src/rt_shadows.hlsl VERBATIM)
add_custom_target(RTShaders DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rt_shadows.cso")
add_dependencies(CG_Sem2 RTShaders)

include(CTest)
if(BUILD_TESTING)
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
