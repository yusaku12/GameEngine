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
  "${CMAKE_CURRENT_LIST_DIR}/../../Tests/Animation/OzzRoundTripTests.cpp")
target_compile_features(AnimationOzzRoundTripTests PRIVATE cxx_std_20)
target_link_libraries(AnimationOzzRoundTripTests PRIVATE ozz_animation_offline)

if(MSVC)
  target_compile_options(AnimationOzzRoundTripTests PRIVATE /W4 /WX /utf-8)
endif()

add_test(NAME AnimationOzzRoundTrip COMMAND AnimationOzzRoundTripTests)
