cmake_minimum_required(VERSION 3.30)

project(GameEngineAnimationDependencies LANGUAGES CXX)

enable_testing()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
set(ozz_build_data OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix ON CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../Library/ozz-animation" ozz)

add_executable(AnimationOzzRoundTripTests
  "${CMAKE_CURRENT_LIST_DIR}/../../Tests/Animation/OzzRoundTripTests.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Tests/Animation/AnimationAssetRoundTripTests.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Animation/Runtime/AnimatorInstance.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/AnimatorControllerAsset.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/AnimationAssetBuilder.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/Serialization/AnimationClipSerializer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/Serialization/AnimatorControllerSerializer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/Serialization/OzzArchiveUtils.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Assets/Animation/Serialization/SkeletonSerializer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Logging/Logger.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Math/SimpleMath.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Serialization/FlatBufferReader.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Serialization/FlatBufferWriter.cpp")
target_compile_features(AnimationOzzRoundTripTests PRIVATE cxx_std_20)
target_link_libraries(AnimationOzzRoundTripTests PRIVATE ozz_animation_offline)
target_include_directories(AnimationOzzRoundTripTests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../.."
  "${CMAKE_CURRENT_LIST_DIR}/../../Source"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/DirectXTex-main/Common"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/flatbuffers/include"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/spdlog/include")

if(MSVC)
  target_compile_options(AnimationOzzRoundTripTests PRIVATE
    /W4 /WX /utf-8
    "/FI${CMAKE_CURRENT_LIST_DIR}/../../Source/Pch.h")
endif()

add_test(NAME AnimationOzzRoundTrip COMMAND AnimationOzzRoundTripTests)

add_executable(ComponentSerializationRoundTripTests
  "${CMAKE_CURRENT_LIST_DIR}/../../Tests/Serialization/ComponentSerializationRoundTripTests.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/GameObject/GameObject.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/GameObject/GameObjectManager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/GameObject/ComponentRegistry.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Logging/Logger.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Math/SimpleMath.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Math/Transform.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Object/ObjectGUID.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Prefab/Prefab.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Prefab/PrefabSerializer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Scene/Scene.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Scene/SceneSerializer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Serialization/ComponentSerialization.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Serialization/FlatBufferReader.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../Source/Core/Serialization/FlatBufferWriter.cpp")
target_compile_features(ComponentSerializationRoundTripTests PRIVATE cxx_std_20)
target_include_directories(ComponentSerializationRoundTripTests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../.."
  "${CMAKE_CURRENT_LIST_DIR}/../../Source"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/DirectXTex-main/Common"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/flatbuffers/include"
  "${CMAKE_CURRENT_LIST_DIR}/../../Library/spdlog/include")
if(MSVC)
  target_compile_options(ComponentSerializationRoundTripTests PRIVATE
    /W4 /WX /utf-8
    "/FI${CMAKE_CURRENT_LIST_DIR}/../../Source/Pch.h")
endif()
add_test(NAME ComponentSerializationRoundTrip COMMAND ComponentSerializationRoundTripTests)
