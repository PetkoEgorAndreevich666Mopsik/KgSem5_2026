cmake_minimum_required(VERSION 3.20)
project(KG_Sem2_2026 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASSIMP_DIR "C:/assimp/assimp-6.0.5")
include_directories(${ASSIMP_DIR}/include)
include_directories(${ASSIMP_DIR}/build/include)
link_directories(${ASSIMP_DIR}/build/lib/Debug)
link_directories(${ASSIMP_DIR}/build/contrib/zlib/Debug)

set(SRC_FILES
        src/main.cpp
        src/Window.cpp
        src/DirectXApp.cpp
        src/RenderingSystem.cpp
        src/GBuffer.cpp
        src/model_loader.cpp
        src/DDSTextureLoader.cpp
        src/d3dUtil.cpp
        src/GameTimer.cpp
)

set(HEADER_FILES
        h/Window.h
        h/DirectXApp.h
        h/RenderingSystem.h
        h/GBuffer.h
        h/model_loader.h
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
        ${CMAKE_SOURCE_DIR}/h
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(CG_Sem2 PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(CG_Sem2 PRIVATE
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
        windowscodecs.lib
        assimp-vc143-mtd.lib
        zlibstaticd.lib
        ole32.lib
        oleaut32.lib
)

if(MSVC)
    target_compile_options(CG_Sem2 PRIVATE /W4 /EHsc)
endif()

set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
