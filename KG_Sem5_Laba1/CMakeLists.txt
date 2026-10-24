cmake_minimum_required(VERSION 3.20)
project(KG_Sem2_2026 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASSIMP_DIR "C:/assimp/assimp-6.0.5")

set(SRC_FILES
        src/model_loader.cpp
        src/Terrain.cpp
        src/main.cpp
        src/Window.cpp
        src/DirectXApp.cpp
        src/RenderingSystem.cpp
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
    target_compile_options(CG_Sem2 PRIVATE /W4 /EHsc)
endif()

set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

include(CTest)
if(BUILD_TESTING)
    add_executable(TerrainTests tools/test_terrain.cpp src/Terrain.cpp)
    target_include_directories(TerrainTests PRIVATE h)
    target_compile_definitions(TerrainTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(TerrainTests PRIVATE /UNDEBUG)
    endif()
    add_test(NAME TerrainChecks COMMAND TerrainTests "${CMAKE_SOURCE_DIR}/assets/terrain")
endif()
