cmake_minimum_required(VERSION 3.30)
project(GameEngineCloudTests LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "These tests require the engine's Windows/MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CLOUD_SCHEMA "${ENGINE_ROOT}/Schemas/FlatBuffers/VolumetricCloudComponent.fbs")
set(CLOUD_HEADER "${ENGINE_ROOT}/Generated/FlatBuffers/VolumetricCloudComponent_generated.h")
add_custom_command(OUTPUT "${CLOUD_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENGINE_ROOT}/Generated/FlatBuffers"
    COMMAND "${ENGINE_ROOT}/Tools/FlatBuffers/flatc.exe" --cpp
        -o "${ENGINE_ROOT}/Generated/FlatBuffers" "${CLOUD_SCHEMA}"
    DEPENDS "${CLOUD_SCHEMA}" "${ENGINE_ROOT}/Tools/FlatBuffers/flatc.exe"
    VERBATIM)

add_executable(CloudTests
    CloudTests.cpp
    "${CLOUD_HEADER}"
    "${ENGINE_ROOT}/Source/Components/Rendering/VolumetricCloudComponent.cpp"
    "${ENGINE_ROOT}/Source/Core/GameObject/ComponentRegistry.cpp"
    "${ENGINE_ROOT}/Source/Core/Logging/Logger.cpp"
    "${ENGINE_ROOT}/Library/imgui/imgui.cpp"
    "${ENGINE_ROOT}/Library/imgui/imgui_draw.cpp"
    "${ENGINE_ROOT}/Library/imgui/imgui_tables.cpp"
    "${ENGINE_ROOT}/Library/imgui/imgui_widgets.cpp"
)
target_include_directories(CloudTests PRIVATE
    "${ENGINE_ROOT}"
    "${ENGINE_ROOT}/Source"
    "${ENGINE_ROOT}/Library"
    "${ENGINE_ROOT}/Library/flatbuffers/include"
    "${ENGINE_ROOT}/Library/spdlog/include"
    "${ENGINE_ROOT}/Library/DirectXTex-main/Common"
    "${ENGINE_ROOT}/Library/imgui"
)
target_compile_options(CloudTests PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(CloudTests PRIVATE /OPT:REF /INCREMENTAL:NO)
target_link_libraries(CloudTests PRIVATE user32 dxguid)
enable_testing()
add_test(NAME CloudRegression COMMAND CloudTests)
