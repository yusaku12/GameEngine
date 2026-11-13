#include "Pch.h"
#include "Components\Rendering\VolumetricCloudComponent.h"
#include "Generated\FlatBuffers\VolumetricCloudComponent_generated.h"
#include <limits>

namespace
{
    bool check(const bool condition, const char* message)
    {
        if (!condition)
            std::fprintf(stderr, "%s\n", message);
        return condition;
    }
}

int main()
{
    using namespace Engine;
    VolumetricCloudComponent cloud;
    VolumetricCloudSettings settings;
    settings.enabled = false;
    settings.baseHeightKm = 3.0f;
    settings.thicknessKm = 1.5f;
    settings.worldUnitsPerKilometer = 2000.0f;
    settings.coverage = 0.8f;
    settings.density = 2.0f;
    settings.noiseScaleKm = 6.0f;
    settings.maxDistanceKm = 50.0f;
    settings.sampleCount = 64;
    if (!check(cloud.setSettings(settings), "Valid settings were rejected."))
        return 1;
    std::vector<std::uint8_t> payload;
    VolumetricCloudComponent restored;
    if (!check(cloud.serializePayload(payload)
        && restored.deserializePayload(cloud.getPayloadVersion(), payload), "Round trip failed."))
        return 1;
    const auto& actual = restored.settings();
    if (!check(actual.enabled == settings.enabled && actual.baseHeightKm == settings.baseHeightKm
        && actual.thicknessKm == settings.thicknessKm
        && actual.worldUnitsPerKilometer == settings.worldUnitsPerKilometer
        && actual.coverage == settings.coverage && actual.density == settings.density
        && actual.noiseScaleKm == settings.noiseScaleKm && actual.maxDistanceKm == settings.maxDistanceKm
        && actual.sampleCount == settings.sampleCount, "Round trip changed settings."))
        return 1;

    settings.thicknessKm = std::numeric_limits<float>::quiet_NaN();
    if (!check(!restored.setSettings(settings) && restored.settings().thicknessKm == 1.5f,
        "Invalid settings changed the component."))
        return 1;
    settings.thicknessKm = 1.5f;
    settings.sampleCount = 65;
    if (!check(!restored.setSettings(settings), "Out-of-range samples were accepted."))
        return 1;
    if (!check(!restored.deserializePayload(2, payload)
        && !restored.deserializePayload(1, std::span(payload).first(7)),
        "Invalid payload was accepted."))
        return 1;
    flatbuffers::FlatBufferBuilder builder;
    const auto invalid = Serialization::CreateVolumetricCloudComponentPayload(builder, true,
        2.0f, 0.0f);
    Serialization::FinishVolumetricCloudComponentPayloadBuffer(builder, invalid);
    if (!check(!restored.deserializePayload(1, { builder.GetBufferPointer(), builder.GetSize() })
        && restored.settings().thicknessKm == 1.5f, "Invalid payload changed the component."))
        return 1;

    return 0;
}
