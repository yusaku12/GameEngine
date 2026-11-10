cmake_minimum_required(VERSION 3.30)
project(GameEngineRootMotionTests LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "These tests require the engine's Windows/MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(FBXSDK_ROOT "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9" CACHE PATH "FBX SDK installation")
foreach(CONFIG debug release)
    if(NOT EXISTS "${FBXSDK_ROOT}/lib/x64/${CONFIG}/libfbxsdk.lib")
        message(FATAL_ERROR "FBX SDK ${CONFIG} library not found; set FBXSDK_ROOT.")
    endif()
endforeach()

set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
set(ozz_build_data OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll OFF CACHE BOOL "" FORCE)
add_subdirectory("${ENGINE_ROOT}/Library/ozz-animation" ozz EXCLUDE_FROM_ALL)

set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_DX11 OFF CACHE BOOL "" FORCE)
set(BUILD_DX12 ON CACHE BOOL "" FORCE)
add_subdirectory("${ENGINE_ROOT}/Library/DirectXTex-main" DirectXTex EXCLUDE_FROM_ALL)

add_executable(RootMotionTests
    RootMotionTests.cpp
    "${ENGINE_ROOT}/Source/Animation/Runtime/AnimatorInstance.cpp"
    "${ENGINE_ROOT}/Source/Assets/Animation/AnimationAssetBuilder.cpp"
    "${ENGINE_ROOT}/Source/Assets/Animation/AnimatorControllerAsset.cpp"
    "${ENGINE_ROOT}/Source/Assets/Model/Import/FbxModelImporter.cpp"
    "${ENGINE_ROOT}/Source/Assets/Model/Serialization/ModelSerializer.cpp"
    "${ENGINE_ROOT}/Source/Core/Logging/Logger.cpp"
    "${ENGINE_ROOT}/Source/Core/Serialization/FlatBufferReader.cpp"
    "${ENGINE_ROOT}/Source/Core/Serialization/FlatBufferWriter.cpp"
    "${ENGINE_ROOT}/Source/Core/Math/SimpleMath.cpp"
    "${ENGINE_ROOT}/Source/Core/Math/Transform.cpp"
    "${ENGINE_ROOT}/Source/Graphics/Renderer/SkinningBounds.cpp"
    "${ENGINE_ROOT}/Source/Graphics/Texture/Texture.cpp"
    "${ENGINE_ROOT}/Source/Graphics/Texture/TextureManager.cpp"
    "${ENGINE_ROOT}/Source/Graphics/DirectX12/Command/Fence.cpp"
    "${ENGINE_ROOT}/Source/Graphics/DirectX12/Command/Command.cpp"
    "${ENGINE_ROOT}/Source/Graphics/DirectX12/Command/Queue.cpp"
    "${ENGINE_ROOT}/Source/Graphics/DirectX12/Resource/Descriptor.cpp"
    "${ENGINE_ROOT}/Source/Core/Object/ObjectGUID.cpp"
    "${ENGINE_ROOT}/Source/Terrain/Material/TerrainMaterial.cpp"
    "${ENGINE_ROOT}/Source/Terrain/Serialization/TerrainMaterialSerialization.cpp"
)
target_include_directories(RootMotionTests PRIVATE
    "${ENGINE_ROOT}/Source"
    "${ENGINE_ROOT}"
    "${ENGINE_ROOT}/Library"
    "${ENGINE_ROOT}/Library/flatbuffers/include"
    "${ENGINE_ROOT}/Library/spdlog/include"
    "${ENGINE_ROOT}/Library/DirectXTex-main/Common"
    "${ENGINE_ROOT}/Library/DirectXTex-main/DirectXTex"
    "${FBXSDK_ROOT}/include"
)
target_compile_options(RootMotionTests PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_libraries(RootMotionTests PRIVATE ozz_animation_offline ozz_animation ozz_base
    debug "${FBXSDK_ROOT}/lib/x64/debug/libfbxsdk.lib"
    optimized "${FBXSDK_ROOT}/lib/x64/release/libfbxsdk.lib"
    DirectXTex user32 dxguid d3d12 dxgi ole32 windowscodecs
)
add_custom_command(TARGET RootMotionTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${FBXSDK_ROOT}/lib/x64/$<IF:$<CONFIG:Debug>,debug,release>/libfbxsdk.dll"
    "$<TARGET_FILE_DIR:RootMotionTests>")
enable_testing()
add_test(NAME RootMotionRegression COMMAND RootMotionTests)
