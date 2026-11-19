#include "Pch.h"
#include "Components\Effects\ParticleEffectComponent.h"
#include "Assets\Effects\ParticleEffectAssetManager.h"
#include "Core\Containers\String.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\System\GameSession.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Generated\FlatBuffers\ParticleEffectComponent_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <imgui.h>

namespace Engine
{
    namespace
    {
        constexpr float PARTICLE_PI = 3.14159265358979323846f;
        constexpr float MAX_SIMULATION_DELTA = 0.25f;
        constexpr float SIMULATION_STEP = 1.0f / 60.0f;
        constexpr float MAX_PARTICLE_LIFETIME = 3600.0f;
        constexpr float MAX_TRAIL_LIFETIME = 60.0f;
        constexpr std::uint32_t MAX_PARTICLES = 4096;
        constexpr std::size_t MAX_BURSTS = 32;
        constexpr std::size_t MAX_CURVE_KEYS = 32;
        constexpr std::size_t MAX_FLIPBOOK_FRAMES = 256;

        const auto particleEffectType =
            ComponentRegistry::instance().registerType<ParticleEffectComponent>(
                "Particle Effect", false, false, true);

        bool isFinite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFinite(const Vector4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool isFinite(const Matrix& value) noexcept
        {
            const std::array elements{
                value._11, value._12, value._13, value._14,
                value._21, value._22, value._23, value._24,
                value._31, value._32, value._33, value._34,
                value._41, value._42, value._43, value._44,
            };
            return std::all_of(elements.begin(), elements.end(),
                [](const float element) { return std::isfinite(element); });
        }

        const char* gpuSimulationFallbackReason(
            const ParticleEffectSettings& settings, const bool editorPreview = false) noexcept
        {
            if (editorPreview)
                return "Editor preview uses the CPU simulator.";
            return ParticleRenderSubmissionQueue::instance().gpuSimulationFallbackReason(settings);
        }

        template<typename Key>
        bool validKeyTimes(const std::vector<Key>& keys) noexcept
        {
            if (keys.size() > MAX_CURVE_KEYS)
                return false;
            float previous = -1.0f;
            for (const auto& key : keys)
            {
                if (!std::isfinite(key.time) || key.time < 0.0f || key.time > 1.0f || key.time <= previous)
                    return false;
                previous = key.time;
            }
            return true;
        }

        bool validLifetimeCurves(const ParticleLifetimeCurves& curves) noexcept
        {
            if (!validKeyTimes(curves.color) || !validKeyTimes(curves.size) || !validKeyTimes(curves.speed)
                || !validKeyTimes(curves.angularSpeed) || !validKeyTimes(curves.width) || !validKeyTimes(curves.height)
                || !validKeyTimes(curves.acceleration) || !validKeyTimes(curves.velocity)
                || !validKeyTimes(curves.gravity) || !validKeyTimes(curves.drag))
                return false;
            for (const auto& key : curves.color)
                if (!isFinite(key.value) || key.value.x < 0.0f || key.value.x > 100.0f
                    || key.value.y < 0.0f || key.value.y > 100.0f || key.value.z < 0.0f || key.value.z > 100.0f
                    || key.value.w < 0.0f || key.value.w > 1.0f)
                    return false;
            for (const auto& key : curves.size)
                if (!std::isfinite(key.value) || key.value < 0.0f || key.value > 10000.0f)
                    return false;
            for (const auto* curve : { &curves.speed, &curves.width, &curves.height, &curves.gravity, &curves.drag })
                for (const auto& key : *curve)
                    if (!std::isfinite(key.value) || key.value < 0.0f || key.value > 100.0f)
                        return false;
            for (const auto& key : curves.angularSpeed)
                if (!std::isfinite(key.value) || std::abs(key.value) > 100.0f)
                    return false;
            for (const auto* curve : { &curves.acceleration, &curves.velocity })
                for (const auto& key : *curve)
                    if (!isFinite(key.value) || std::abs(key.value.x) > 10000.0f
                        || std::abs(key.value.y) > 10000.0f || std::abs(key.value.z) > 10000.0f)
                        return false;
            return true;
        }

        double evaluateEmissionRate(const std::vector<ParticleScalarKey>& keys, const double time) noexcept
        {
            const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                [](const double t, const ParticleScalarKey& key) { return t < key.time; });
            if (upper == keys.begin())
                return keys.front().value;
            if (upper == keys.end())
                return keys.back().value;
            const auto& left = *std::prev(upper);
            const auto& right = *upper;
            const double factor = (time - left.time) / (right.time - left.time);
            return std::lerp(static_cast<double>(left.value), static_cast<double>(right.value), factor);
        }

        double integrateEmissionCurve(const std::vector<ParticleScalarKey>& keys, const double endTime) noexcept
        {
            if (endTime <= 0.0)
                return 0.0;
            double integral = 0.0;
            double previousTime = 0.0;
            double previousValue = evaluateEmissionRate(keys, 0.0);
            for (const auto& key : keys)
            {
                if (key.time <= 0.0f || key.time >= endTime)
                    continue;
                const double keyTime = key.time;
                const double keyValue = key.value;
                integral += (keyTime - previousTime) * (previousValue + keyValue) * 0.5;
                previousTime = keyTime;
                previousValue = keyValue;
            }
            const double endValue = evaluateEmissionRate(keys, endTime);
            integral += (endTime - previousTime) * (previousValue + endValue) * 0.5;
            return integral;
        }

        double integrateEmissionRate(const ParticleEffectSettings& settings, const double start,
            const double duration, const double activeDuration) noexcept
        {
            if (!(duration > 0.0) || !(activeDuration > 0.0))
                return 0.0;
            if (settings.emissionRateKeys.empty())
                return static_cast<double>(settings.emissionRate) * activeDuration;

            const auto cumulative = [&](const double elapsed)
            {
                const double cycles = std::floor(elapsed / duration);
                const double remainder = elapsed - cycles * duration;
                const double cycleIntegral = integrateEmissionCurve(settings.emissionRateKeys, 1.0) * duration;
                return cycles * cycleIntegral
                    + duration * integrateEmissionCurve(settings.emissionRateKeys, remainder / duration);
            };
            const double end = start + activeDuration;
            const double integrated = settings.looping
                ? cumulative(end) - cumulative(start)
                : duration * (integrateEmissionCurve(settings.emissionRateKeys,
                    std::min(1.0, end / duration))
                    - integrateEmissionCurve(settings.emissionRateKeys, std::min(1.0, start / duration)));
            return std::max(0.0, integrated);
        }

        struct FlipbookSample
        {
            std::uint32_t current = 0;
            std::uint32_t next = 0;
            float blend = 0.0f;
        };

        FlipbookSample sampleFlipbook(const ParticleEffectSettings& settings, const double age,
            const double lifetime, const std::uint32_t startFrame) noexcept
        {
            const bool custom = settings.flipbookPlayback == ParticleFlipbookPlayback::Custom;
            const std::uint32_t sequenceCount = custom
                ? static_cast<std::uint32_t>(settings.flipbookFrames.size())
                : settings.flipbookColumns * settings.flipbookRows;
            if (sequenceCount == 0)
                return {};
            const std::uint32_t start = startFrame % sequenceCount;
            const auto frameAt = [&](const std::uint32_t sequenceIndex)
            {
                const std::uint32_t index = (start + sequenceIndex) % sequenceCount;
                return custom ? settings.flipbookFrames[index].frame : index;
            };
            const auto makeSample = [&](const std::uint32_t index, const float blend, const bool hold = false)
            {
                const auto bounded = std::min(index, sequenceCount - 1);
                const std::uint32_t next = !hold && bounded + 1 < sequenceCount ? bounded + 1
                    : (!hold ? 0 : bounded);
                return FlipbookSample{ frameAt(bounded),
                    settings.flipbookFrameBlending ? frameAt(next) : frameAt(bounded),
                    settings.flipbookFrameBlending ? blend : 0.0f };
            };
            if (settings.flipbookPlayback == ParticleFlipbookPlayback::Lifetime)
            {
                const double phase = std::clamp(age / lifetime, 0.0, 1.0) * sequenceCount;
                if (phase >= sequenceCount)
                    return makeSample(sequenceCount - 1, 0.0f, true);
                const auto index = static_cast<std::uint32_t>(phase);
                return makeSample(index, static_cast<float>(phase - index), index + 1 == sequenceCount);
            }
            if (settings.flipbookPlayback == ParticleFlipbookPlayback::FramesPerSecond)
            {
                const double phase = age * settings.flipbookFramesPerSecond;
                const double totalFrames = static_cast<double>(sequenceCount) * settings.flipbookLoopCount;
                if (settings.flipbookLoopCount != 0 && phase >= totalFrames)
                    return makeSample(sequenceCount - 1, 0.0f, true);
                const double cyclePhase = settings.flipbookLoopCount == 0
                    ? std::fmod(phase, static_cast<double>(sequenceCount)) : phase;
                const auto index = static_cast<std::uint32_t>(cyclePhase) % sequenceCount;
                const bool finalFrame = settings.flipbookLoopCount != 0
                    && std::floor(phase / sequenceCount) >= settings.flipbookLoopCount - 1
                    && index + 1 == sequenceCount;
                return makeSample(index, static_cast<float>(cyclePhase - std::floor(cyclePhase)), finalFrame);
            }

            double cycleDuration = 0.0;
            for (const auto& frame : settings.flipbookFrames)
                cycleDuration += frame.duration;
            const double totalDuration = cycleDuration * settings.flipbookLoopCount;
            if (settings.flipbookLoopCount != 0 && age >= totalDuration)
                return makeSample(sequenceCount - 1, 0.0f, true);
            const double cycleTime = settings.flipbookLoopCount == 0
                ? std::fmod(age, cycleDuration) : age;
            double frameTime = std::fmod(cycleTime, cycleDuration);
            const bool finalLoop = settings.flipbookLoopCount != 0
                && std::floor(age / cycleDuration) >= settings.flipbookLoopCount - 1;
            for (std::uint32_t index = 0; index < sequenceCount; ++index)
            {
                const double frameDuration = settings.flipbookFrames[(start + index) % sequenceCount].duration;
                if (frameTime < frameDuration || index + 1 == sequenceCount)
                    return makeSample(index, static_cast<float>(std::clamp(frameTime / frameDuration, 0.0, 1.0)),
                        finalLoop && index + 1 == sequenceCount);
                frameTime -= frameDuration;
            }
            return makeSample(sequenceCount - 1, 0.0f);
        }

        template<typename Key>
        auto evaluateCurve(const std::vector<Key>& keys, const float time, const bool smooth = false) noexcept
        {
            const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                [](const float t, const Key& key) { return t < key.time; });
            if (upper == keys.begin())
                return keys.front().value;
            if (upper == keys.end())
                return keys.back().value;
            const auto& left = *std::prev(upper);
            const auto& right = *upper;
            float factor = (time - left.time) / (right.time - left.time);
            if (smooth)
                factor = factor * factor * (3.0f - 2.0f * factor);
            return left.value + (right.value - left.value) * factor;
        }

        template<typename Key, typename Value>
        void addCurveKey(std::vector<Key>& keys, const Value& value)
        {
            if (keys.empty())
            {
                keys.push_back({ 0.0f, value });
                return;
            }
            float left = 0.0f;
            float bestLeft = 0.0f;
            float bestRight = keys.front().time;
            for (const auto& key : keys)
            {
                if (key.time - left > bestRight - bestLeft)
                {
                    bestLeft = left;
                    bestRight = key.time;
                }
                left = key.time;
            }
            if (1.0f - left > bestRight - bestLeft)
            {
                bestLeft = left;
                bestRight = 1.0f;
            }
            const float time = (bestLeft + bestRight) * 0.5f;
            const auto position = std::lower_bound(keys.begin(), keys.end(), time,
                [](const Key& key, const float t) { return key.time < t; });
            keys.insert(position, Key{ time, value });
        }

        template<typename Key>
        bool editCurveTime(std::vector<Key>& keys, const std::size_t index)
        {
            const float lower = index == 0 ? 0.0f : std::nextafter(keys[index - 1].time, 1.0f);
            const float upper = index + 1 == keys.size() ? 1.0f : std::nextafter(keys[index + 1].time, 0.0f);
            return ImGui::SliderFloat("Time", &keys[index].time, lower, upper, "%.4f",
                ImGuiSliderFlags_NoRoundToFormat);
        }

        bool editScalarGraph(const char* label, std::vector<ParticleScalarKey>& keys, const float limit,
            const bool smooth)
        {
            if (keys.empty())
                return false;
            ImGui::PushID(label);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(120.0f, ImGui::GetContentRegionAvail().x), 120.0f);
            ImGui::InvisibleButton("graph", size);
            ImGuiStorage* const storage = ImGui::GetStateStorage();
            const ImGuiID activeId = ImGui::GetID("activeKey");
            const ImGuiID scaleId = ImGui::GetID("dragScale");
            float scale = 1.0f;
            for (const auto& key : keys)
                scale = std::max(scale, key.value);
            if (ImGui::IsItemClicked())
            {
                int closest = -1;
                float distance = 100.0f;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                for (std::size_t i = 0; i < keys.size(); ++i)
                {
                    const float x = origin.x + keys[i].time * size.x;
                    const float y = origin.y + (1.0f - keys[i].value / scale) * size.y;
                    const float d = (mouse.x - x) * (mouse.x - x) + (mouse.y - y) * (mouse.y - y);
                    if (d < distance)
                    {
                        closest = static_cast<int>(i);
                        distance = d;
                    }
                }
                storage->SetInt(activeId, closest);
                storage->SetFloat(scaleId, scale);
            }
            bool changed = false;
            const int active = storage->GetInt(activeId, -1);
            if (ImGui::IsItemActive() && active >= 0 && static_cast<std::size_t>(active) < keys.size())
            {
                scale = storage->GetFloat(scaleId, scale);
                const std::size_t index = static_cast<std::size_t>(active);
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float lower = index == 0 ? 0.0f : std::nextafter(keys[index - 1].time, 1.0f);
                const float upper = index + 1 == keys.size() ? 1.0f : std::nextafter(keys[index + 1].time, 0.0f);
                const float time = std::clamp((mouse.x - origin.x) / size.x, lower, upper);
                const float value = std::clamp((1.0f - (mouse.y - origin.y) / size.y) * scale, 0.0f, limit);
                changed = time != keys[index].time || value != keys[index].value;
                keys[index].time = time;
                keys[index].value = value;
            }
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 28, 32, 255));
            draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(100, 110, 120, 255));
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const ImVec2 point(origin.x + keys[i].time * size.x,
                    origin.y + (1.0f - keys[i].value / scale) * size.y);
                if (i != 0)
                {
                    const auto& previous = keys[i - 1];
                    ImVec2 last(origin.x + previous.time * size.x,
                        origin.y + (1.0f - previous.value / scale) * size.y);
                    const int segments = smooth ? 16 : 1;
                    for (int segment = 1; segment <= segments; ++segment)
                    {
                        const float factor = static_cast<float>(segment) / static_cast<float>(segments);
                        const float time = std::lerp(previous.time, keys[i].time, factor);
                        const float curveValue = evaluateCurve(keys, time, smooth);
                        const ImVec2 next(origin.x + time * size.x, origin.y + (1.0f - curveValue / scale) * size.y);
                        draw->AddLine(last, next, IM_COL32(90, 200, 255, 255), 2.0f);
                        last = next;
                    }
                }
                draw->AddCircleFilled(point, 4.0f, IM_COL32(255, 210, 80, 255));
            }
            ImGui::TextDisabled("Drag keys; range 0 to %.3g", scale);
            ImGui::PopID();
            return changed;
        }

        bool editGradient(std::vector<ParticleColorKey>& keys, const bool smooth)
        {
            if (keys.empty())
                return false;
            ImGui::PushID("gradient");
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(120.0f, ImGui::GetContentRegionAvail().x), 48.0f);
            ImGui::InvisibleButton("bar", size,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            ImGuiStorage* const storage = ImGui::GetStateStorage();
            const ImGuiID activeId = ImGui::GetID("activeKey");
            const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (leftClicked || rightClicked)
            {
                int closest = -1;
                float distance = 144.0f;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                for (std::size_t i = 0; i < keys.size(); ++i)
                {
                    const float x = origin.x + keys[i].time * size.x;
                    const float y = origin.y + (1.0f - keys[i].value.w) * size.y;
                    const float d = (mouse.x - x) * (mouse.x - x) + (mouse.y - y) * (mouse.y - y);
                    if (d < distance)
                    {
                        closest = static_cast<int>(i);
                        distance = d;
                    }
                }
                storage->SetInt(activeId, closest);
                if (rightClicked && closest >= 0)
                    ImGui::OpenPopup("keyColor");
            }
            bool changed = false;
            const int active = storage->GetInt(activeId, -1);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                && active >= 0 && static_cast<std::size_t>(active) < keys.size())
            {
                const auto index = static_cast<std::size_t>(active);
                const float lower = index == 0 ? 0.0f : std::nextafter(keys[index - 1].time, 1.0f);
                const float upper = index + 1 == keys.size() ? 1.0f : std::nextafter(keys[index + 1].time, 0.0f);
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float time = std::clamp((mouse.x - origin.x) / size.x, lower, upper);
                const float alpha = std::clamp(1.0f - (mouse.y - origin.y) / size.y, 0.0f, 1.0f);
                changed = time != keys[index].time || alpha != keys[index].value.w;
                keys[index].time = time;
                keys[index].value.w = alpha;
            }
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            constexpr int samples = 128;
            for (int i = 0; i < samples; ++i)
            {
                const float x0 = origin.x + size.x * static_cast<float>(i) / samples;
                const float x1 = origin.x + size.x * static_cast<float>(i + 1) / samples;
                const auto color = evaluateCurve(keys, (static_cast<float>(i) + 0.5f) / samples, smooth);
                draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1 + 1.0f, origin.y + size.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f)));
                const float alphaY = origin.y + (1.0f - color.w) * size.y;
                draw->AddLine(ImVec2(x0, alphaY), ImVec2(x1 + 1.0f, alphaY), IM_COL32(255, 255, 255, 255), 2.0f);
            }
            for (const auto& key : keys)
            {
                const float x = origin.x + key.time * size.x;
                draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), IM_COL32(0, 0, 0, 255), 2.0f);
                draw->AddCircleFilled(ImVec2(x, origin.y + (1.0f - key.value.w) * size.y),
                    4.0f, IM_COL32(255, 210, 80, 255));
            }
            if (ImGui::BeginPopup("keyColor"))
            {
                if (active >= 0 && static_cast<std::size_t>(active) < keys.size())
                    changed |= ImGui::ColorPicker4("Color", &keys[active].value.x, ImGuiColorEditFlags_HDR);
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("Drag markers for time/alpha; right-click to edit color.");
            ImGui::PopID();
            return changed;
        }

        Vector3 randomUnitVector(const float azimuth, const float z) noexcept
        {
            const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return Vector3(radial * std::cos(azimuth), z, radial * std::sin(azimuth));
        }

        constexpr std::array ASSET_FILTERS{ FileDialogFilter{ L"Particle Effect", L"*.particleeffect" } };
        constexpr std::array TEXTURE_FILTERS{
            FileDialogFilter{ L"Texture", L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.dds;*.tga" }
        };
        constexpr std::array MODEL_FILTERS{ FileDialogFilter{ L"Model", L"*.model;*.mdl" } };

        std::string assetLabel(const std::filesystem::path& path)
        {
            if (path.empty())
                return "<Unsaved>";
            const auto utf8 = String::fromWide(path.filename().c_str());
            return std::string(utf8.c_str(), utf8.size());
        }

        void requestOpenAsset(const ObjectGUID sceneGuid, const ObjectGUID objectGuid, const HWND owner)
        {
            MainThreadDispatcher::instance().post([sceneGuid, objectGuid, owner]
            {
                if (GameSession::instance().isPlaying())
                    return;
                std::vector<std::filesystem::path> paths;
                const auto result = Dialog::openFile(paths, L"Open Particle Effect", {}, ASSET_FILTERS, false, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok || paths.empty())
                {
                    LOG_ERROR("[ParticleEffect] Asset open dialog failed.");
                    return;
                }
                GameObject* object = nullptr;
                SceneManager::instance().forEachScene([&](Scene& scene)
                {
                    if (scene.getGUID() == sceneGuid)
                        object = scene.find(objectGuid);
                });
                auto* const component = object ? object->getComponent<ParticleEffectComponent>() : nullptr;
                if (component == nullptr || GameSession::instance().isPlaying())
                {
                    LOG_ERROR("[ParticleEffect] Asset owner changed while the open dialog was active.");
                    return;
                }
                const auto handle = ParticleEffectAssetManager::instance().load(paths.front());
                if (handle.isValid())
                    static_cast<void>(component->setAsset(handle));
            });
        }

        void requestSaveAsset(const ParticleEffectAssetHandle handle, const HWND owner)
        {
            MainThreadDispatcher::instance().post([handle, owner]
            {
                if (GameSession::instance().isPlaying())
                    return;
                std::filesystem::path path;
                const auto result = Dialog::saveFile(path, L"Save Particle Effect", {},
                    L"particleeffect", ASSET_FILTERS, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok)
                {
                    LOG_ERROR("[ParticleEffect] Asset save dialog failed.");
                    return;
                }
                if (!GameSession::instance().isPlaying())
                    static_cast<void>(ParticleEffectAssetManager::instance().save(handle, path));
            });
        }

        void requestTexture(const ObjectGUID sceneGuid, const ObjectGUID objectGuid, const HWND owner,
            std::filesystem::path ParticleEffectSettings::* const field, const bool model = false)
        {
            MainThreadDispatcher::instance().post([sceneGuid, objectGuid, owner, field, model]
            {
                std::vector<std::filesystem::path> paths;
                const std::span<const FileDialogFilter> filters = model ? std::span(MODEL_FILTERS) : std::span(TEXTURE_FILTERS);
                const auto result = Dialog::openFile(paths, model ? L"Select Particle Model" : L"Select Particle Texture",
                    L"Assets", filters, false, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok || paths.empty())
                {
                    LOG_ERROR("[ParticleEffect] Texture selection dialog failed.");
                    return;
                }
                GameObject* object = nullptr;
                SceneManager::instance().forEachScene([&](Scene& scene)
                {
                    if (scene.getGUID() == sceneGuid)
                        object = scene.find(objectGuid);
                });
                auto* const component = object ? object->getComponent<ParticleEffectComponent>() : nullptr;
                if (component == nullptr)
                {
                    LOG_ERROR("[ParticleEffect] Texture owner changed while the dialog was active.");
                    return;
                }
                const auto previous = component->settings();
                auto settings = previous;
                settings.*field = paths.front();
                if (!component->setSettings(settings))
                    return;
                if (component->asset().isValid() && !GameSession::instance().isPlaying()
                    && !ParticleEffectAssetManager::instance().update(component->asset(), settings)
                    && !component->setSettings(previous))
                    LOG_ERROR("[ParticleEffect] Failed to restore settings after a texture update failure.");
            });
        }
    }

    bool ParticleEffectComponent::setSettings(const ParticleEffectSettings& settings)
    {
        const float directionLengthSquared = settings.direction.LengthSquared();
        if (settings.maxParticles == 0 || settings.maxParticles > MAX_PARTICLES
            || !std::isfinite(settings.emissionRate) || settings.emissionRate < 0.0f || settings.emissionRate > 10000.0f
            || settings.burstCount > MAX_PARTICLES
            || !std::isfinite(settings.duration) || settings.duration < 0.0f || settings.duration > 3600.0f
            || !std::isfinite(settings.lifetimeMin) || !std::isfinite(settings.lifetimeMax)
            || settings.lifetimeMin <= 0.0f || settings.lifetimeMax < settings.lifetimeMin || settings.lifetimeMax > MAX_PARTICLE_LIFETIME
            || !std::isfinite(settings.shapeRadius) || settings.shapeRadius < 0.0f || settings.shapeRadius > 10000.0f
            || !isFinite(settings.direction) || !std::isfinite(directionLengthSquared) || directionLengthSquared < 1e-8f
            || !std::isfinite(settings.spreadAngleDegrees) || settings.spreadAngleDegrees < 0.0f || settings.spreadAngleDegrees > 180.0f
            || !std::isfinite(settings.speedMin) || !std::isfinite(settings.speedMax)
            || settings.speedMin < 0.0f || settings.speedMax < settings.speedMin || settings.speedMax > 10000.0f
            || !isFinite(settings.gravity) || std::abs(settings.gravity.x) > 10000.0f
            || std::abs(settings.gravity.y) > 10000.0f || std::abs(settings.gravity.z) > 10000.0f
            || !std::isfinite(settings.drag) || settings.drag < 0.0f || settings.drag > 100.0f
            || !std::isfinite(settings.startSize) || !std::isfinite(settings.endSize)
            || settings.startSize < 0.0f || settings.startSize > 10000.0f
            || settings.endSize < 0.0f || settings.endSize > 10000.0f
            || !isFinite(settings.startColor) || !isFinite(settings.endColor)
            || settings.startColor.x < 0.0f || settings.startColor.y < 0.0f || settings.startColor.z < 0.0f
            || settings.endColor.x < 0.0f || settings.endColor.y < 0.0f || settings.endColor.z < 0.0f
            || settings.startColor.x > 100.0f || settings.startColor.y > 100.0f || settings.startColor.z > 100.0f
            || settings.endColor.x > 100.0f || settings.endColor.y > 100.0f || settings.endColor.z > 100.0f
            || settings.startColor.w < 0.0f || settings.startColor.w > 1.0f
            || settings.endColor.w < 0.0f || settings.endColor.w > 1.0f)
        {
            LOG_ERROR("[ParticleEffect] Invalid particle effect settings.");
            return false;
        }
        if (settings.simulationBackend > ParticleSimulationBackend::GPU
            || !isParticleBlendModeValid(settings.blendMode)
            || settings.flipbookColumns == 0 || settings.flipbookColumns > 64
            || settings.flipbookRows == 0 || settings.flipbookRows > 64
            || settings.flipbookPlayback > ParticleFlipbookPlayback::Custom
            || !std::isfinite(settings.flipbookFramesPerSecond)
            || settings.flipbookFramesPerSecond <= 0.0f || settings.flipbookFramesPerSecond > 1000.0f
            || settings.flipbookLoopCount > 10000
            || settings.flipbookFrames.size() > MAX_FLIPBOOK_FRAMES
            || settings.orientation > ParticleOrientation::Stretched
            || !isFinite(settings.orientationDirection)
            || !std::isfinite(settings.orientationDirection.LengthSquared())
            || settings.orientationDirection.LengthSquared() < 1e-8f
            || !std::isfinite(settings.stretchBySpeed) || settings.stretchBySpeed < 0.0f
            || settings.stretchBySpeed > 10000.0f
            || !std::isfinite(settings.softDistance) || settings.softDistance < 0.0f
            || settings.softDistance > 10000.0f
            || !std::isfinite(settings.uvScroll.x) || !std::isfinite(settings.uvScroll.y)
            || std::abs(settings.uvScroll.x) > 10000.0f || std::abs(settings.uvScroll.y) > 10000.0f
            || !std::isfinite(settings.uvDistortion) || std::abs(settings.uvDistortion) > 10.0f
            || !std::isfinite(settings.refractionPixels) || std::abs(settings.refractionPixels) > 1024.0f
            || settings.texturePath.native().find(L'\0') != std::wstring::npos
            || settings.secondaryTexturePath.native().find(L'\0') != std::wstring::npos
            || settings.maskTexturePath.native().find(L'\0') != std::wstring::npos
            || settings.distortionTexturePath.native().find(L'\0') != std::wstring::npos
            || settings.meshPath.native().find(L'\0') != std::wstring::npos)
        {
            LOG_ERROR("[ParticleEffect] Invalid particle appearance settings.");
            return false;
        }
        if ((settings.shape != ParticleEmissionShape::Sphere && settings.shape != ParticleEmissionShape::Box
                && settings.shape != ParticleEmissionShape::Disk)
            || !isFinite(settings.boxExtents)
            || settings.boxExtents.x < 0.0f || settings.boxExtents.x > 10000.0f
            || settings.boxExtents.y < 0.0f || settings.boxExtents.y > 10000.0f
            || settings.boxExtents.z < 0.0f || settings.boxExtents.z > 10000.0f
            || settings.bursts.size() > MAX_BURSTS || settings.curveVariants.size() > 3
            || !validLifetimeCurves(settings.curves)
            || !validKeyTimes(settings.emissionRateKeys)
            || (!settings.emissionRateKeys.empty() && settings.duration <= 0.0f)
            || !std::all_of(settings.curveVariants.begin(), settings.curveVariants.end(), validLifetimeCurves))
        {
            LOG_ERROR("[ParticleEffect] Invalid emission shape, burst, or curve settings.");
            return false;
        }
        for (const auto& key : settings.emissionRateKeys)
            if (!std::isfinite(key.value) || key.value < 0.0f || key.value > 10000.0f)
            {
                LOG_ERROR("[ParticleEffect] Emission-rate curve values must be in [0, 10000].");
                return false;
            }
        for (const auto& frame : settings.flipbookFrames)
            if (frame.frame >= settings.flipbookColumns * settings.flipbookRows
                || !std::isfinite(frame.duration) || frame.duration < 0.001f || frame.duration > 3600.0f)
            {
                LOG_ERROR("[ParticleEffect] Invalid custom flipbook frame.");
                return false;
            }
        if (settings.flipbookPlayback == ParticleFlipbookPlayback::Custom && settings.flipbookFrames.empty())
        {
            LOG_ERROR("[ParticleEffect] Custom flipbook playback requires at least one frame.");
            return false;
        }
        double randomizedBurstWork = 0.0;
        for (const auto& burst : settings.bursts)
        {
            if (!std::isfinite(burst.time) || burst.time < 0.0f || burst.time > settings.duration
                || burst.count > MAX_PARTICLES || burst.countMin > burst.countMax
                || burst.countMax > MAX_PARTICLES || !std::isfinite(burst.probability)
                || burst.probability < 0.0f || burst.probability > 1.0f
                || burst.repeatCount == 0 || burst.repeatCount > 128
                || !std::isfinite(burst.repeatInterval) || burst.repeatInterval < 0.0f
                || (burst.repeatCount > 1 && burst.repeatInterval == 0.0f)
                || static_cast<double>(burst.time)
                    + static_cast<double>(burst.repeatCount - 1) * burst.repeatInterval
                    > std::nextafter(settings.duration, std::numeric_limits<float>::infinity()))
            {
                LOG_ERROR("[ParticleEffect] Invalid scheduled burst.");
                return false;
            }
            if (burst.probability > 0.0f && (burst.randomCount || burst.probability < 1.0f))
            {
                const double cycles = settings.looping && settings.duration > 0.0f
                    ? std::ceil(static_cast<double>(SIMULATION_STEP) / settings.duration) + 1.0 : 1.0;
                randomizedBurstWork += cycles * burst.repeatCount;
            }
        }
        if (randomizedBurstWork > 8192.0)
        {
            LOG_ERROR("[ParticleEffect] Randomized bursts exceed the per-step event budget.");
            return false;
        }
        const auto validColorRange = [](const Vector4& low, const Vector4& high)
        {
            return isFinite(low) && isFinite(high)
                && low.x >= 0.0f && low.x <= high.x && high.x <= 100.0f
                && low.y >= 0.0f && low.y <= high.y && high.y <= 100.0f
                && low.z >= 0.0f && low.z <= high.z && high.z <= 100.0f
                && low.w >= 0.0f && low.w <= high.w && high.w <= 1.0f;
        };
        if (!std::isfinite(settings.initialSizeMin) || !std::isfinite(settings.initialSizeMax)
            || settings.initialSizeMin < 0.0f || settings.initialSizeMin > settings.initialSizeMax
            || settings.initialSizeMax > 10000.0f
            || !validColorRange(settings.initialColorMin, settings.initialColorMax)
            || !std::isfinite(settings.initialRotationMin) || !std::isfinite(settings.initialRotationMax)
            || settings.initialRotationMin < -10000.0f || settings.initialRotationMax > 10000.0f
            || settings.initialRotationMin > settings.initialRotationMax
            || !std::isfinite(settings.angularVelocityMin) || !std::isfinite(settings.angularVelocityMax)
            || settings.angularVelocityMin < -10000.0f || settings.angularVelocityMax > 10000.0f
            || settings.angularVelocityMin > settings.angularVelocityMax
            || !std::isfinite(settings.simulationSpeed)
            || settings.simulationSpeed < 0.0f || settings.simulationSpeed > 4.0f
            || !std::isfinite(settings.initialSizeScale.x) || !std::isfinite(settings.initialSizeScale.y)
            || settings.initialSizeScale.x < 0.0f || settings.initialSizeScale.x > 100.0f
            || settings.initialSizeScale.y < 0.0f || settings.initialSizeScale.y > 100.0f
            || !std::isfinite(settings.startDelay) || settings.startDelay < 0.0f || settings.startDelay > 3600.0f
            || (settings.prewarm && (!settings.looping || settings.duration <= 0.0f))
            || !std::isfinite(settings.distancePerParticle) || settings.distancePerParticle < 0.0f
            || (settings.distancePerParticle > 0.0f && settings.distancePerParticle < 0.0001f)
            || settings.distancePerParticle > 10000.0f || !std::isfinite(settings.inheritVelocity)
            || settings.inheritVelocity < 0.0f || settings.inheritVelocity > 4.0f)
        {
            LOG_ERROR("[ParticleEffect] Invalid initial particle ranges.");
            return false;
        }
        if (!std::isfinite(settings.trailLifetime) || settings.trailLifetime < 0.01f
            || settings.trailLifetime > MAX_TRAIL_LIFETIME || !std::isfinite(settings.trailWidth)
            || settings.trailWidth <= 0.0f || settings.trailWidth > 10000.0f
            || !std::isfinite(settings.trailMinDistance) || settings.trailMinDistance < 0.0f
            || settings.trailMinDistance > 10000.0f)
        {
            LOG_ERROR("[ParticleEffect] Invalid trail settings.");
            return false;
        }

        ParticleEffectSettings newSettings;
        auto newVisual = m_renderVisual;
        std::shared_ptr<const ParticleLifetimeCurves> newCurves;
        std::vector<std::shared_ptr<const ParticleLifetimeCurves>> newVariants;
        try
        {
            newSettings = settings;
            newCurves = std::make_shared<const ParticleLifetimeCurves>(settings.curves);
            newVariants.reserve(settings.curveVariants.size());
            for (const auto& variant : settings.curveVariants)
                newVariants.push_back(std::make_shared<const ParticleLifetimeCurves>(variant));
            m_particles.reserve(settings.maxParticles);
            m_renderParticles.reserve(settings.maxParticles);
            if (settings.trailEnabled)
            {
                m_trails.reserve(settings.maxParticles);
                m_freeTrails.reserve(settings.maxParticles);
                m_renderTrails.reserve(static_cast<std::size_t>(settings.maxParticles) * TRAIL_POINT_CAPACITY);
            }
            if (!m_renderVisual || m_renderVisual->texturePath != settings.texturePath
                || m_renderVisual->blendMode != settings.blendMode
                || m_renderVisual->flipbookColumns != settings.flipbookColumns
                || m_renderVisual->flipbookRows != settings.flipbookRows
                || m_renderVisual->orientation != settings.orientation
                || m_renderVisual->orientationDirection != settings.orientationDirection
                || m_renderVisual->stretchBySpeed != settings.stretchBySpeed
                || m_renderVisual->softDistance != settings.softDistance
                || m_renderVisual->secondaryTexturePath != settings.secondaryTexturePath
                || m_renderVisual->maskTexturePath != settings.maskTexturePath
                || m_renderVisual->distortionTexturePath != settings.distortionTexturePath
                || m_renderVisual->uvDistortion != settings.uvDistortion
                || m_renderVisual->refractionPixels != settings.refractionPixels
                || m_renderVisual->meshPath != settings.meshPath)
                newVisual = std::make_shared<const ParticleRenderVisual>(
                    ParticleRenderVisual{ settings.texturePath, settings.blendMode,
                        settings.flipbookColumns, settings.flipbookRows, settings.orientation,
                        settings.orientationDirection, settings.stretchBySpeed, settings.softDistance,
                        settings.secondaryTexturePath, settings.maskTexturePath, settings.distortionTexturePath,
                        settings.uvDistortion, settings.refractionPixels, settings.meshPath });
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to reserve particle storage.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Particle storage exceeds its maximum capacity.");
            return false;
        }

        if (m_particles.size() > settings.maxParticles)
        {
            for (std::size_t index = settings.maxParticles; index < m_particles.size(); ++index)
                if (m_particles[index].trailIndex != NO_TRAIL)
                    detachTrail(m_trails[m_particles[index].trailIndex]);
            m_particles.resize(settings.maxParticles);
        }
        const bool motionChanged = settings.distancePerParticle != m_settings.distancePerParticle
            || settings.inheritVelocity != m_settings.inheritVelocity || settings.localSpace != m_settings.localSpace;
        if (settings.simulationBackend != m_settings.simulationBackend)
        {
            ParticleRenderSubmissionQueue::instance().removeGpuEmitter(m_gpuEmitterId);
            m_gpuEmitterId = 0;
            m_gpuClearPending = settings.simulationBackend == ParticleSimulationBackend::GPU;
            m_gpuPendingEmission = {};
            m_gpuPendingMotion = {};
            m_gpuPendingDeltaTime = 0.0f;
            m_gpuLiveTimeRemaining = 0.0;
            m_particles.clear();
            m_renderParticles.clear();
            clearTrails();
            resetEmitterMotion();
        }
        m_settings = std::move(newSettings);
        m_renderVisual = std::move(newVisual);
        m_lifetimeCurves = std::move(newCurves);
        m_curveVariants = std::move(newVariants);
        if (motionChanged)
        {
            resetEmitterMotion();
            m_distanceRemainder = 0.0;
            m_gpuResetDistancePending = true;
            m_gpuPendingMotion = {};
        }
        if (m_prewarmRemaining > 0.0)
        {
            clear();
            m_prewarmFailed = true;
            LOG_WARNING("[ParticleEffect] Prewarm was interrupted by a settings change.");
        }
        if (m_previewSeeking)
        {
            m_previewSeeking = false;
            m_previewSeekFailed = true;
            LOG_WARNING("[ParticleEffect] Preview seek was interrupted by a settings change.");
        }
        return true;
    }

    bool ParticleEffectComponent::setAsset(const ParticleEffectAssetHandle handle)
    {
        std::shared_ptr<const ParticleEffectAsset> snapshot;
        if (handle.isValid())
        {
            snapshot = ParticleEffectAssetManager::instance().get(handle);
            if (!snapshot)
            {
                LOG_ERROR("[ParticleEffect] Cannot assign a stale asset handle.");
                return false;
            }
            if (!setSettings(snapshot->settings))
                return false;
        }
        else if (handle != ParticleEffectAssetHandle{})
        {
            LOG_ERROR("[ParticleEffect] Cannot assign a malformed asset handle.");
            return false;
        }
        m_asset = handle;
        m_assetSnapshot = std::move(snapshot);
        return true;
    }

    bool ParticleEffectComponent::synchronizeAsset()
    {
        if (!m_asset.isValid())
            return true;
        const auto snapshot = ParticleEffectAssetManager::instance().get(m_asset);
        if (!snapshot)
        {
            LOG_ERROR("[ParticleEffect] Assigned asset handle is stale.");
            return false;
        }
        if (snapshot != m_assetSnapshot)
        {
            if (!setSettings(snapshot->settings))
                return false;
            m_assetSnapshot = snapshot;
        }
        return true;
    }

    bool ParticleEffectComponent::play()
    {
        if (!GameSession::instance().isPlaying() && !synchronizeAsset())
            return false;
        if (m_settings.prewarm && getGameObject() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Prewarm requires an attached GameObject.");
            return false;
        }
        if (m_particles.capacity() < m_settings.maxParticles
            || m_renderParticles.capacity() < m_settings.maxParticles)
        {
            try
            {
                m_particles.reserve(m_settings.maxParticles);
                m_renderParticles.reserve(m_settings.maxParticles);
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[ParticleEffect] Failed to reserve particle storage for playback.");
                return false;
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[ParticleEffect] Particle playback exceeds the maximum capacity.");
                return false;
            }
        }

        m_particles.clear();
        m_renderParticles.clear();
        clearTrails();
        m_emissionRemainder = 0.0f;
        m_emissionElapsed = 0.0f;
        m_prewarmRemaining = m_settings.prewarm ? m_settings.duration : 0.0;
        m_delayRemaining = m_settings.prewarm ? 0.0 : m_settings.startDelay;
        if (m_settings.prewarm)
            m_prewarmWorld = getGameObject()->getWorldMatrix();
        m_prewarmFailed = false;
        m_hasEmitterPosition = false;
        m_motionValid = false;
        m_emitterVelocity = Vector3::Zero;
        m_distanceRemainder = 0.0;
        m_motionElapsed = 0.0;
        m_randomState = m_settings.seed == 0 ? 0x9E3779B9u : m_settings.seed;
        m_gpuPendingEmission = {};
        m_gpuPendingMotion = {};
        m_gpuPendingSeed = m_randomState;
        m_gpuLiveTimeRemaining = 0.0;
        m_gpuClearPending = true;
        m_gpuPendingDeltaTime = 0.0f;
        m_emitting = true;
        m_burstPending = m_settings.burstCount > 0 || !m_settings.bursts.empty();
        m_paused = false;
        m_deltaCappedReported = false;
        m_droppedParticles = 0;
        m_droppedTrails = 0;
        m_submissionFailed = false;
        return true;
    }

    void ParticleEffectComponent::stop() noexcept
    {
        if (m_settings.clearOnStop || m_prewarmRemaining > 0.0 || m_delayRemaining > 0.0)
            clear();
        else
        {
            m_emitting = false;
            m_burstPending = false;
        }
    }

    void ParticleEffectComponent::setPaused(const bool paused) noexcept
    {
        if (m_paused != paused)
            resetEmitterMotion();
        m_paused = paused;
    }

    void ParticleEffectComponent::onDisable()
    {
        ParticleRenderSubmissionQueue::instance().removeGpuEmitter(m_gpuEmitterId);
        m_gpuEmitterId = 0;
        resetEmitterMotion();
    }

    void ParticleEffectComponent::resetEmitterMotion() noexcept
    {
        m_hasEmitterPosition = false;
        m_motionValid = false;
        m_emitterVelocity = Vector3::Zero;
        m_motionElapsed = 0.0;
    }

    void ParticleEffectComponent::clear() noexcept
    {
        m_emitting = false;
        m_burstPending = false;
        m_emissionRemainder = 0.0f;
        m_emissionElapsed = 0.0f;
        m_delayRemaining = 0.0;
        m_prewarmRemaining = 0.0;
        resetEmitterMotion();
        m_distanceRemainder = 0.0;
        m_particles.clear();
        m_renderParticles.clear();
        clearTrails();
        m_gpuClearPending = true;
        m_gpuPendingEmission = {};
        m_gpuPendingMotion = {};
        m_gpuPendingDeltaTime = 0.0f;
        m_gpuLiveTimeRemaining = 0.0;
        m_deltaCappedReported = false;
    }

    bool ParticleEffectComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        Serialization::AssetGuid assetGuid;
        flatbuffers::Offset<flatbuffers::String> assetPath;
        if (m_asset.isValid())
        {
            auto& manager = ParticleEffectAssetManager::instance();
            const auto snapshot = manager.get(m_asset);
            const auto path = manager.getPath(m_asset);
            if (!snapshot || path.empty())
            {
                LOG_ERROR("[ParticleEffect] Save the referenced asset before saving its Scene/Prefab.");
                return false;
            }
            const auto utf8 = String::fromWide(path.c_str());
            if (utf8.size() == 0)
            {
                LOG_ERROR("[ParticleEffect] Cannot encode the asset path as UTF-8.");
                return false;
            }
            assetGuid = Serialization::AssetGuid(snapshot->guid.high, snapshot->guid.low);
            assetPath = builder.CreateString(utf8.view());
        }
        const Serialization::Vec3 direction(
            m_settings.direction.x, m_settings.direction.y, m_settings.direction.z);
        const Serialization::Vec3 gravity(
            m_settings.gravity.x, m_settings.gravity.y, m_settings.gravity.z);
        const Serialization::Vec4 startColor(
            m_settings.startColor.x, m_settings.startColor.y, m_settings.startColor.z, m_settings.startColor.w);
        const Serialization::Vec4 endColor(
            m_settings.endColor.x, m_settings.endColor.y, m_settings.endColor.z, m_settings.endColor.w);
        flatbuffers::Offset<flatbuffers::String> texturePath;
        if (!m_settings.texturePath.empty())
        {
            const auto utf8 = String::fromWide(m_settings.texturePath.c_str());
            if (utf8.size() == 0)
            {
                LOG_ERROR("[ParticleEffect] Cannot encode the texture path as UTF-8.");
                return false;
            }
            texturePath = builder.CreateString(utf8.view());
        }
        const auto encodeTexture = [&builder](const std::filesystem::path& path)
        {
            if (path.empty())
                return flatbuffers::Offset<flatbuffers::String>{};
            const auto utf8 = String::fromWide(path.c_str());
            return utf8.size() ? builder.CreateString(utf8.view()) : flatbuffers::Offset<flatbuffers::String>{};
        };
        const auto secondaryTexturePath = encodeTexture(m_settings.secondaryTexturePath);
        const auto maskTexturePath = encodeTexture(m_settings.maskTexturePath);
        const auto distortionTexturePath = encodeTexture(m_settings.distortionTexturePath);
        const auto meshPath = encodeTexture(m_settings.meshPath);
        if ((!m_settings.secondaryTexturePath.empty() && !secondaryTexturePath.o)
            || (!m_settings.maskTexturePath.empty() && !maskTexturePath.o)
            || (!m_settings.distortionTexturePath.empty() && !distortionTexturePath.o)
            || (!m_settings.meshPath.empty() && !meshPath.o))
        {
            LOG_ERROR("[ParticleEffect] Cannot encode an auxiliary texture path as UTF-8.");
            return false;
        }
        std::vector<Serialization::ParticleBurst> bursts;
        std::vector<Serialization::ParticleBurstOptions> burstOptions;
        std::vector<Serialization::ParticleColorKey> colorKeys;
        std::vector<Serialization::ParticleScalarKey> sizeKeys;
        std::vector<Serialization::ParticleScalarKey> speedKeys;
        std::vector<Serialization::ParticleScalarKey> angularSpeedKeys;
        std::vector<Serialization::ParticleScalarKey> widthKeys;
        std::vector<Serialization::ParticleScalarKey> heightKeys;
        std::vector<Serialization::ParticleVectorKey> accelerationKeys;
        std::vector<Serialization::ParticleVectorKey> velocityKeys;
        std::vector<Serialization::ParticleScalarKey> gravityKeys;
        std::vector<Serialization::ParticleScalarKey> dragKeys;
        bursts.reserve(m_settings.bursts.size());
        colorKeys.reserve(m_settings.curves.color.size());
        sizeKeys.reserve(m_settings.curves.size.size());
        speedKeys.reserve(m_settings.curves.speed.size());
        for (const auto& burst : m_settings.bursts)
        {
            bursts.emplace_back(burst.time, burst.count);
            burstOptions.emplace_back(burst.countMin, burst.countMax, burst.probability,
                burst.repeatCount, burst.repeatInterval, burst.randomCount);
        }
        for (const auto& key : m_settings.curves.color)
            colorKeys.emplace_back(key.time, Serialization::Vec4(key.value.x, key.value.y, key.value.z, key.value.w));
        for (const auto& key : m_settings.curves.size)
            sizeKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.speed)
            speedKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.angularSpeed)
            angularSpeedKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.width)
            widthKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.height)
            heightKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.acceleration)
            accelerationKeys.emplace_back(key.time, Serialization::Vec3(key.value.x, key.value.y, key.value.z));
        for (const auto& key : m_settings.curves.velocity)
            velocityKeys.emplace_back(key.time, Serialization::Vec3(key.value.x, key.value.y, key.value.z));
        for (const auto& key : m_settings.curves.gravity)
            gravityKeys.emplace_back(key.time, key.value);
        for (const auto& key : m_settings.curves.drag)
            dragKeys.emplace_back(key.time, key.value);
        const auto burstVector = builder.CreateVectorOfStructs(bursts);
        const auto burstOptionsVector = builder.CreateVectorOfStructs(burstOptions);
        const auto colorVector = builder.CreateVectorOfStructs(colorKeys);
        const auto sizeVector = builder.CreateVectorOfStructs(sizeKeys);
        const auto speedVector = builder.CreateVectorOfStructs(speedKeys);
        const auto angularSpeedVector = builder.CreateVectorOfStructs(angularSpeedKeys);
        const auto widthVector = builder.CreateVectorOfStructs(widthKeys);
        const auto heightVector = builder.CreateVectorOfStructs(heightKeys);
        const auto accelerationVector = builder.CreateVectorOfStructs(accelerationKeys);
        const auto velocityVector = builder.CreateVectorOfStructs(velocityKeys);
        const auto gravityVector = builder.CreateVectorOfStructs(gravityKeys);
        const auto dragVector = builder.CreateVectorOfStructs(dragKeys);
        const Serialization::Vec3 boxExtents(m_settings.boxExtents.x, m_settings.boxExtents.y, m_settings.boxExtents.z);
        const auto& colorMin = m_settings.initialColorMin;
        const auto& colorMax = m_settings.initialColorMax;
        const Serialization::Vec4 initialColorMin(colorMin.x, colorMin.y, colorMin.z, colorMin.w);
        const Serialization::Vec4 initialColorMax(colorMax.x, colorMax.y, colorMax.z, colorMax.w);
        const Serialization::Vec2 initialSizeScale(m_settings.initialSizeScale.x, m_settings.initialSizeScale.y);
        const Serialization::Vec3 orientationDirection(m_settings.orientationDirection.x,
            m_settings.orientationDirection.y, m_settings.orientationDirection.z);
        const Serialization::Vec2 uvScroll(m_settings.uvScroll.x, m_settings.uvScroll.y);
        const auto encodeScalars = [&](const std::vector<ParticleScalarKey>& keys)
        {
            std::vector<Serialization::ParticleScalarKey> encoded;
            encoded.reserve(keys.size());
            for (const auto& key : keys)
                encoded.emplace_back(key.time, key.value);
            return builder.CreateVectorOfStructs(encoded);
        };
        const auto emissionRateVector = encodeScalars(m_settings.emissionRateKeys);
        std::vector<Serialization::ParticleFlipbookFrame> flipbookFrames;
        flipbookFrames.reserve(m_settings.flipbookFrames.size());
        for (const auto& frame : m_settings.flipbookFrames)
            flipbookFrames.emplace_back(frame.frame, frame.duration);
        const auto flipbookFrameVector = builder.CreateVectorOfStructs(flipbookFrames);
        const auto encodeVectors = [&](const std::vector<ParticleVectorKey>& keys)
        {
            std::vector<Serialization::ParticleVectorKey> encoded;
            encoded.reserve(keys.size());
            for (const auto& key : keys)
                encoded.emplace_back(key.time, Serialization::Vec3(key.value.x, key.value.y, key.value.z));
            return builder.CreateVectorOfStructs(encoded);
        };
        std::vector<flatbuffers::Offset<Serialization::ParticleCurveVariant>> variants;
        variants.reserve(m_settings.curveVariants.size());
        for (const auto& curve : m_settings.curveVariants)
        {
            std::vector<Serialization::ParticleColorKey> colors;
            colors.reserve(curve.color.size());
            for (const auto& key : curve.color)
                colors.emplace_back(key.time, Serialization::Vec4(key.value.x, key.value.y, key.value.z, key.value.w));
            const auto color = builder.CreateVectorOfStructs(colors);
            const auto size = encodeScalars(curve.size);
            const auto speed = encodeScalars(curve.speed);
            const auto angular = encodeScalars(curve.angularSpeed);
            const auto width = encodeScalars(curve.width);
            const auto height = encodeScalars(curve.height);
            const auto acceleration = encodeVectors(curve.acceleration);
            const auto velocity = encodeVectors(curve.velocity);
            const auto curveGravity = encodeScalars(curve.gravity);
            const auto drag = encodeScalars(curve.drag);
            variants.push_back(Serialization::CreateParticleCurveVariant(builder, color, size, speed, angular,
                width, height, curve.smooth, acceleration, velocity, curveGravity, drag));
        }
        const auto variantVector = builder.CreateVector(variants);
        const auto root = Serialization::CreateParticleEffectComponentPayload(builder,
            m_settings.playOnAwake, m_settings.looping, m_settings.localSpace, m_settings.maxParticles,
            m_settings.emissionRate, m_settings.burstCount, m_settings.duration,
            m_settings.lifetimeMin, m_settings.lifetimeMax, m_settings.shapeRadius, &direction,
            m_settings.spreadAngleDegrees, m_settings.speedMin, m_settings.speedMax, &gravity,
            m_settings.drag, m_settings.startSize, m_settings.endSize, &startColor, &endColor, m_settings.seed,
            m_asset.isValid() ? &assetGuid : nullptr, assetPath, texturePath,
            static_cast<std::uint8_t>(m_settings.blendMode), m_settings.flipbookColumns, m_settings.flipbookRows,
            static_cast<std::uint8_t>(m_settings.shape), &boxExtents, burstVector, colorVector, sizeVector, speedVector,
            m_settings.trailEnabled, m_settings.trailLifetime, m_settings.trailWidth, m_settings.trailMinDistance,
            m_settings.randomInitial, m_settings.initialSizeMin, m_settings.initialSizeMax,
            &initialColorMin, &initialColorMax, m_settings.initialRotationMin, m_settings.initialRotationMax,
            m_settings.angularVelocityMin, m_settings.angularVelocityMax, angularSpeedVector,
            m_settings.simulationSpeed, m_settings.clearOnStop, m_settings.curves.smooth,
            &initialSizeScale, widthVector, heightVector, accelerationVector, velocityVector,
            gravityVector, dragVector, burstOptionsVector, m_settings.startDelay, m_settings.prewarm, variantVector,
            m_settings.distancePerParticle, m_settings.inheritVelocity,
            static_cast<std::uint8_t>(m_settings.orientation), &orientationDirection, m_settings.stretchBySpeed,
            m_settings.softDistance, &uvScroll, secondaryTexturePath, maskTexturePath,
            distortionTexturePath, m_settings.uvDistortion, m_settings.refractionPixels, meshPath,
            emissionRateVector, static_cast<std::uint8_t>(m_settings.flipbookPlayback),
            m_settings.flipbookFramesPerSecond, m_settings.flipbookLoopCount,
            m_settings.flipbookRandomStartFrame, m_settings.flipbookFrameBlending, flipbookFrameVector,
            static_cast<std::uint8_t>(m_settings.simulationBackend));
        Serialization::FinishParticleEffectComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool ParticleEffectComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if ((version < 1 || version > 16) || payload.size() < 8
            || !Serialization::VerifyParticleEffectComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[ParticleEffect] Invalid particle effect payload.");
            return false;
        }

        const auto* const source = Serialization::GetParticleEffectComponentPayload(payload.data());
        if (source->direction() == nullptr || source->gravity() == nullptr
            || source->start_color() == nullptr || source->end_color() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Particle effect payload is missing vector or color data.");
            return false;
        }
        ParticleEffectAssetHandle handle;
        if (version >= 2 && (source->asset_guid() != nullptr || source->asset_path() != nullptr))
        {
            if (source->asset_guid() == nullptr || source->asset_path() == nullptr)
            {
                LOG_ERROR("[ParticleEffect] Asset reference requires both GUID and path.");
                return false;
            }
            const ObjectGUID guid{ source->asset_guid()->high(), source->asset_guid()->low() };
            const auto path = source->asset_path()->string_view();
            if (!guid.isValid() || path.empty() || path.find('\0') != std::string_view::npos)
            {
                LOG_ERROR("[ParticleEffect] Invalid asset GUID or path.");
                return false;
            }
            const auto wide = String(path).toWide();
            if (wide.empty())
            {
                LOG_ERROR("[ParticleEffect] Asset path is not valid UTF-8.");
                return false;
            }
            handle = ParticleEffectAssetManager::instance().load(std::filesystem::path(wide), guid);
            if (!handle.isValid())
                return false;
        }
        ParticleEffectSettings settings{
            source->play_on_awake(),
            source->looping(),
            source->local_space(),
            source->max_particles(),
            source->emission_rate(),
            source->burst_count(),
            source->duration(),
            source->lifetime_min(),
            source->lifetime_max(),
            source->shape_radius(),
            Vector3(source->direction()->x(), source->direction()->y(), source->direction()->z()),
            source->spread_angle_degrees(),
            source->speed_min(),
            source->speed_max(),
            Vector3(source->gravity()->x(), source->gravity()->y(), source->gravity()->z()),
            source->drag(),
            source->start_size(),
            source->end_size(),
            Vector4(source->start_color()->x(), source->start_color()->y(),
                source->start_color()->z(), source->start_color()->w()),
            Vector4(source->end_color()->x(), source->end_color()->y(),
                source->end_color()->z(), source->end_color()->w()),
            source->seed(),
        };
        if (version >= 3)
        {
            settings.blendMode = static_cast<ParticleBlendMode>(source->blend_mode());
            if (version < 8 && settings.blendMode > ParticleBlendMode::Additive)
            {
                LOG_ERROR("[ParticleEffect] Blend mode is unsupported by this payload version.");
                return false;
            }
            settings.flipbookColumns = source->flipbook_columns();
            settings.flipbookRows = source->flipbook_rows();
            if (const auto* texture = source->texture_path())
            {
                const auto path = texture->string_view();
                if (path.empty() || path.find('\0') != std::string_view::npos)
                {
                    LOG_ERROR("[ParticleEffect] Invalid texture path.");
                    return false;
                }
                const auto wide = String(path).toWide();
                if (wide.empty())
                {
                    LOG_ERROR("[ParticleEffect] Texture path is not valid UTF-8.");
                    return false;
                }
                settings.texturePath = wide;
            }
        }
        if (version >= 4)
        {
            const auto* extents = source->box_extents();
            const auto* bursts = source->bursts();
            const auto* colors = source->color_keys();
            const auto* sizes = source->size_keys();
            const auto* speeds = source->speed_keys();
            if (extents == nullptr || (bursts && bursts->size() > MAX_BURSTS)
                || (colors && colors->size() > MAX_CURVE_KEYS)
                || (sizes && sizes->size() > MAX_CURVE_KEYS)
                || (speeds && speeds->size() > MAX_CURVE_KEYS))
            {
                LOG_ERROR("[ParticleEffect] Invalid emission and lifetime curve payload.");
                return false;
            }
            settings.shape = static_cast<ParticleEmissionShape>(source->emission_shape());
            settings.boxExtents = Vector3(extents->x(), extents->y(), extents->z());
            if (bursts)
                for (const auto* burst : *bursts)
                    settings.bursts.push_back({ burst->time(), burst->count() });
            if (colors)
                for (const auto* key : *colors)
                    settings.curves.color.push_back({ key->time(),
                        Vector4(key->value().x(), key->value().y(), key->value().z(), key->value().w()) });
            if (sizes)
                for (const auto* key : *sizes)
                    settings.curves.size.push_back({ key->time(), key->value() });
            if (speeds)
                for (const auto* key : *speeds)
                    settings.curves.speed.push_back({ key->time(), key->value() });
        }
        if (version >= 5)
        {
            settings.trailEnabled = source->trail_enabled();
            settings.trailLifetime = source->trail_lifetime();
            settings.trailWidth = source->trail_width();
            settings.trailMinDistance = source->trail_min_distance();
        }
        if (version >= 6)
        {
            const auto* low = source->initial_color_min();
            const auto* high = source->initial_color_max();
            const auto* angularKeys = source->angular_speed_keys();
            const auto* sizeScale = source->initial_size_scale();
            const auto* widthKeys = source->width_keys();
            const auto* heightKeys = source->height_keys();
            if (!low || !high || !sizeScale || (angularKeys && angularKeys->size() > MAX_CURVE_KEYS)
                || (widthKeys && widthKeys->size() > MAX_CURVE_KEYS)
                || (heightKeys && heightKeys->size() > MAX_CURVE_KEYS))
            {
                LOG_ERROR("[ParticleEffect] Invalid initial range or angular-speed curve payload.");
                return false;
            }
            settings.randomInitial = source->random_initial();
            settings.initialSizeMin = source->initial_size_min();
            settings.initialSizeMax = source->initial_size_max();
            settings.initialColorMin = Vector4(low->x(), low->y(), low->z(), low->w());
            settings.initialColorMax = Vector4(high->x(), high->y(), high->z(), high->w());
            settings.initialRotationMin = source->initial_rotation_min();
            settings.initialRotationMax = source->initial_rotation_max();
            settings.angularVelocityMin = source->angular_velocity_min();
            settings.angularVelocityMax = source->angular_velocity_max();
            settings.simulationSpeed = source->simulation_speed();
            settings.clearOnStop = source->clear_on_stop();
            settings.curves.smooth = source->smooth_curves();
            settings.initialSizeScale = Vector2(sizeScale->x(), sizeScale->y());
            if (angularKeys)
                for (const auto* key : *angularKeys)
                    settings.curves.angularSpeed.push_back({ key->time(), key->value() });
            if (widthKeys)
                for (const auto* key : *widthKeys)
                    settings.curves.width.push_back({ key->time(), key->value() });
            if (heightKeys)
                for (const auto* key : *heightKeys)
                    settings.curves.height.push_back({ key->time(), key->value() });
        }
        if (version >= 7)
        {
            const auto* acceleration = source->acceleration_keys();
            const auto* velocity = source->velocity_keys();
            const auto* gravity = source->gravity_keys();
            const auto* drag = source->drag_keys();
            const auto* burstOptions = source->burst_options();
            settings.startDelay = source->start_delay();
            settings.prewarm = source->prewarm();
            settings.distancePerParticle = source->distance_per_particle();
            settings.inheritVelocity = source->inherit_velocity();
            if ((acceleration && acceleration->size() > MAX_CURVE_KEYS)
                || (velocity && velocity->size() > MAX_CURVE_KEYS)
                || (gravity && gravity->size() > MAX_CURVE_KEYS)
                || (drag && drag->size() > MAX_CURVE_KEYS)
                || (burstOptions && burstOptions->size() != settings.bursts.size()))
            {
                LOG_ERROR("[ParticleEffect] Motion curves exceed the key limit.");
                return false;
            }
            if (acceleration)
                for (const auto* key : *acceleration)
                    settings.curves.acceleration.push_back({ key->time(),
                        Vector3(key->value().x(), key->value().y(), key->value().z()) });
            if (velocity)
                for (const auto* key : *velocity)
                    settings.curves.velocity.push_back({ key->time(),
                        Vector3(key->value().x(), key->value().y(), key->value().z()) });
            if (gravity)
                for (const auto* key : *gravity)
                    settings.curves.gravity.push_back({ key->time(), key->value() });
            if (drag)
                for (const auto* key : *drag)
                    settings.curves.drag.push_back({ key->time(), key->value() });
            if (burstOptions)
            {
                for (std::size_t index = 0; index < settings.bursts.size(); ++index)
                {
                    const auto* options = burstOptions->Get(static_cast<flatbuffers::uoffset_t>(index));
                    auto& burst = settings.bursts[index];
                    burst.randomCount = options->random_count();
                    burst.countMin = options->count_min();
                    burst.countMax = options->count_max();
                    burst.probability = options->probability();
                    burst.repeatCount = options->repeat_count();
                    burst.repeatInterval = options->repeat_interval();
                }
            }
            if (const auto* variants = source->curve_variants())
            {
                if (variants->size() > 3)
                {
                    LOG_ERROR("[ParticleEffect] Too many lifetime curve variants.");
                    return false;
                }
                for (const auto* sourceCurve : *variants)
                {
                    const auto tooMany = [](const auto* keys) { return keys && keys->size() > MAX_CURVE_KEYS; };
                    if (!sourceCurve || tooMany(sourceCurve->color()) || tooMany(sourceCurve->size())
                        || tooMany(sourceCurve->speed()) || tooMany(sourceCurve->angular_speed())
                        || tooMany(sourceCurve->width()) || tooMany(sourceCurve->height())
                        || tooMany(sourceCurve->acceleration()) || tooMany(sourceCurve->velocity())
                        || tooMany(sourceCurve->gravity()) || tooMany(sourceCurve->drag()))
                    {
                        LOG_ERROR("[ParticleEffect] Invalid lifetime curve variant.");
                        return false;
                    }
                    ParticleLifetimeCurves curve;
                    curve.smooth = sourceCurve->smooth();
                    if (const auto* colors = sourceCurve->color())
                        for (const auto* key : *colors)
                            curve.color.push_back({ key->time(),
                                Vector4(key->value().x(), key->value().y(), key->value().z(), key->value().w()) });
                    const auto readScalars = [](const auto* keys, std::vector<ParticleScalarKey>& target)
                    {
                        if (keys)
                            for (const auto* key : *keys)
                                target.push_back({ key->time(), key->value() });
                    };
                    const auto readVectors = [](const auto* keys, std::vector<ParticleVectorKey>& target)
                    {
                        if (keys)
                            for (const auto* key : *keys)
                                target.push_back({ key->time(),
                                    Vector3(key->value().x(), key->value().y(), key->value().z()) });
                    };
                    readScalars(sourceCurve->size(), curve.size);
                    readScalars(sourceCurve->speed(), curve.speed);
                    readScalars(sourceCurve->angular_speed(), curve.angularSpeed);
                    readScalars(sourceCurve->width(), curve.width);
                    readScalars(sourceCurve->height(), curve.height);
                    readVectors(sourceCurve->acceleration(), curve.acceleration);
                    readVectors(sourceCurve->velocity(), curve.velocity);
                    readScalars(sourceCurve->gravity(), curve.gravity);
                    readScalars(sourceCurve->drag(), curve.drag);
                    settings.curveVariants.push_back(std::move(curve));
                }
            }
        }
        if (version >= 9)
        {
            const auto* direction = source->orientation_direction();
            if (!direction)
            {
                LOG_ERROR("[ParticleEffect] Particle orientation direction is missing.");
                return false;
            }
            settings.orientation = static_cast<ParticleOrientation>(source->orientation());
            settings.orientationDirection = Vector3(direction->x(), direction->y(), direction->z());
            settings.stretchBySpeed = source->stretch_by_speed();
        }
        if (version >= 10)
            settings.softDistance = source->soft_distance();
        if (version >= 11)
        {
            const auto* scroll = source->uv_scroll();
            if (!scroll)
            {
                LOG_ERROR("[ParticleEffect] Particle UV scroll is missing.");
                return false;
            }
            settings.uvScroll = Vector2(scroll->x(), scroll->y());
        }
        if (version >= 12)
        {
            const auto decodeTexture = [](const flatbuffers::String* text, std::filesystem::path& path)
            {
                if (!text)
                    return true;
                const auto value = text->string_view();
                if (value.empty() || value.find('\0') != std::string_view::npos)
                    return false;
                const auto wide = String(value).toWide();
                if (wide.empty())
                    return false;
                path = wide;
                return true;
            };
            if (!decodeTexture(source->secondary_texture_path(), settings.secondaryTexturePath)
                || !decodeTexture(source->mask_texture_path(), settings.maskTexturePath)
                || !decodeTexture(source->distortion_texture_path(), settings.distortionTexturePath))
            {
                LOG_ERROR("[ParticleEffect] Invalid auxiliary texture path.");
                return false;
            }
            settings.uvDistortion = source->uv_distortion();
        }
        if (version >= 13)
            settings.refractionPixels = source->refraction_pixels();
        if (version >= 14 && source->mesh_path())
        {
            const auto path = source->mesh_path()->string_view();
            if (path.empty() || path.find('\0') != std::string_view::npos)
            {
                LOG_ERROR("[ParticleEffect] Invalid particle mesh path.");
                return false;
            }
            const auto wide = String(path).toWide();
            if (wide.empty())
            {
                LOG_ERROR("[ParticleEffect] Particle mesh path is not valid UTF-8.");
                return false;
            }
            settings.meshPath = wide;
        }
        if (version >= 15)
        {
            const auto* const emissionRateKeys = source->emission_rate_keys();
            const auto* const flipbookFrames = source->flipbook_frames();
            if ((emissionRateKeys && emissionRateKeys->size() > MAX_CURVE_KEYS)
                || (flipbookFrames && flipbookFrames->size() > MAX_FLIPBOOK_FRAMES))
            {
                LOG_ERROR("[ParticleEffect] Emission-rate curve or custom flipbook sequence exceeds its limit.");
                return false;
            }
            settings.flipbookPlayback = static_cast<ParticleFlipbookPlayback>(source->flipbook_playback());
            settings.flipbookFramesPerSecond = source->flipbook_frames_per_second();
            settings.flipbookLoopCount = source->flipbook_loop_count();
            settings.flipbookRandomStartFrame = source->flipbook_random_start_frame();
            settings.flipbookFrameBlending = source->flipbook_frame_blending();
            if (emissionRateKeys)
                for (const auto* key : *emissionRateKeys)
                    settings.emissionRateKeys.push_back({ key->time(), key->value() });
            if (flipbookFrames)
                for (const auto* frame : *flipbookFrames)
                    settings.flipbookFrames.push_back({ frame->frame(), frame->duration() });
        }
        if (version >= 16)
            settings.simulationBackend = static_cast<ParticleSimulationBackend>(source->simulation_backend());
        if (handle.isValid())
            return setAsset(handle);
        if (!setSettings(settings))
            return false;
        m_asset = {};
        m_assetSnapshot.reset();
        return true;
    }

    bool ParticleEffectComponent::copyConfigurationTo(Component& destination) const
    {
        auto* const target = dynamic_cast<ParticleEffectComponent*>(&destination);
        if (target == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot copy settings to a different component type.");
            return false;
        }
        if (m_asset.isValid())
        {
            if (!target->setAsset(m_asset))
                return false;
            return !GameSession::instance().isPlaying() || target->setSettings(m_settings);
        }
        if (!target->setSettings(m_settings))
            return false;
        target->m_asset = {};
        target->m_assetSnapshot.reset();
        return true;
    }

    void ParticleEffectComponent::onStart()
    {
        if (!synchronizeAsset())
            return;
        if (m_settings.playOnAwake && !play())
            LOG_ERROR("[ParticleEffect] Play On Awake could not start the effect.");
    }

    void ParticleEffectComponent::onLateUpdate(const float deltaTime)
    {
        const auto started = std::chrono::steady_clock::now();
        const GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return;

        if (m_settings.simulationBackend == ParticleSimulationBackend::GPU
            && gpuSimulationFallbackReason(m_settings) == nullptr
            && GameSession::instance().state() != PlayState::Editing)
        {
            const Matrix world = gameObject->getWorldMatrix();
            updateGpuSimulation(deltaTime, world);
            submitRenderData();
            m_cpuMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            return;
        }

        if (!m_paused)
        {
            if (m_prewarmRemaining > 0.0)
            {
                resetEmitterMotion();
                advancePrewarm(16);
            }
            else
            {
                const Matrix world = gameObject->getWorldMatrix();
                const float scaledDelta = deltaTime * m_settings.simulationSpeed;
                const double delay = m_delayRemaining;
                const double elapsed = m_emissionElapsed;
                const bool emitting = m_emitting;
                if (!std::isfinite(scaledDelta) || scaledDelta < 0.0f)
                {
                    resetEmitterMotion();
                    simulate(scaledDelta, world);
                }
                else if (scaledDelta > 0.0f)
                {
                    static_cast<void>(sampleEmitterMotion(deltaTime, world));
                    simulate(scaledDelta, world);
                    emitByDistance(std::min(scaledDelta, MAX_SIMULATION_DELTA), delay, elapsed, emitting, world);
                }
                else
                    resetEmitterMotion();
            }
        }
        submitRenderData();
        m_cpuMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }

    void ParticleEffectComponent::updateGpuSimulation(const float deltaTime, const Matrix& worldMatrix)
    {
        m_gpuPendingEmission = {};
        m_gpuPendingMotion = {};
        m_gpuPendingDeltaTime = 0.0f;
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f || !isFinite(worldMatrix))
        {
            resetEmitterMotion();
            LOG_ERROR("[ParticleEffect] GPU simulation received invalid frame data.");
            return;
        }
        if (m_paused || GameSession::instance().state() == PlayState::Paused)
        {
            resetEmitterMotion();
            return;
        }

        float simulationDelta = deltaTime * m_settings.simulationSpeed;
        if (!std::isfinite(simulationDelta))
        {
            resetEmitterMotion();
            LOG_ERROR("[ParticleEffect] GPU simulation delta is not finite.");
            return;
        }
        if (simulationDelta > MAX_SIMULATION_DELTA)
        {
            if (!m_deltaCappedReported)
                LOG_WARNING("[ParticleEffect] GPU simulation delta was capped at {} seconds.",
                    MAX_SIMULATION_DELTA);
            m_deltaCappedReported = true;
            simulationDelta = MAX_SIMULATION_DELTA;
        }
        else
            m_deltaCappedReported = false;
        m_gpuPendingDeltaTime = simulationDelta;

        if (simulationDelta > 0.0f)
        {
            if (sampleEmitterMotion(deltaTime, worldMatrix))
                m_gpuPendingMotion.localVelocity = m_emitterVelocity;
        }
        else
            resetEmitterMotion();

        if (m_emitting && simulationDelta > 0.0f)
        {
            auto& window = m_gpuPendingEmission;
            window.delay = m_delayRemaining;
            const double delay = std::min(m_delayRemaining, static_cast<double>(simulationDelta));
            m_delayRemaining -= delay;
            const double afterDelay = static_cast<double>(simulationDelta) - delay;
            const double duration = m_settings.duration;
            if (afterDelay > 0.0)
            {
                window.initial = m_burstPending;
                if (m_settings.looping && duration > 0.0 && m_emissionElapsed >= duration)
                    m_emissionElapsed = std::fmod(m_emissionElapsed, duration);
                window.phase = std::min(m_emissionElapsed, duration);
                const double activeTime = m_settings.looping && duration > 0.0
                    ? afterDelay : std::min(afterDelay, duration - window.phase);
                const double distanceTime = m_settings.looping ? afterDelay : activeTime;
                if (m_motionValid && m_settings.distancePerParticle > 0.0f && distanceTime > 0.0)
                {
                    const Vector3 movement = m_motionEnd - m_motionStart;
                    const float startFromEnd = static_cast<float>(-afterDelay / simulationDelta);
                    const float span = static_cast<float>(distanceTime / simulationDelta);
                    const auto travel = movement * span;
                    const float distance = travel.Length();
                    const Vector3 startOffset = Vector3::TransformNormal(movement * startFromEnd, m_motionInverse);
                    const Vector3 localTravel = Vector3::TransformNormal(travel, m_motionInverse);
                    if (!std::isfinite(distance) || !isFinite(startOffset) || !isFinite(localTravel)
                        || !isFinite(startOffset + localTravel))
                    {
                        LOG_ERROR("[ParticleEffect] GPU emitter travel is not finite.");
                        resetEmitterMotion();
                    }
                    else
                    {
                        m_gpuPendingMotion.distance = distance;
                        m_gpuPendingMotion.segmentStartOffset = startOffset;
                        m_gpuPendingMotion.segmentDelta = localTravel;
                    }
                }
                window.activeTime = static_cast<float>(activeTime);
                const double end = window.phase + activeTime;
                if (m_settings.looping && duration > 0.0 && end >= duration)
                {
                    window.endPhase = std::fmod(end, duration);
                    window.wraps = std::round((end - window.endPhase) / duration);
                    window.completeCycleTime = std::max(0.0,
                        activeTime - (duration - window.phase) - window.endPhase);
                }
                else
                    window.endPhase = std::min(end, duration);
                m_emissionElapsed = window.endPhase;
                if ((!m_settings.looping && end >= duration)
                    || (duration <= 0.0 && m_settings.distancePerParticle == 0.0f))
                    m_emitting = false;
            }
        }

        // Do not retire a GPU emitter before float age accumulation can expire its particles and trails.
        if (m_gpuPendingDeltaTime > 0.0f && m_gpuLiveTimeRemaining > 0.0)
            m_gpuLiveTimeRemaining = std::max(0.0,
                m_gpuLiveTimeRemaining - std::max(0.0, static_cast<double>(m_gpuPendingDeltaTime)
                    - (static_cast<double>(std::nextafter(MAX_PARTICLE_LIFETIME,
                        (std::numeric_limits<float>::infinity)())) + MAX_TRAIL_LIFETIME)
                        * static_cast<double>(std::numeric_limits<float>::epsilon()) * 0.5));
        if (m_gpuPendingEmission.maySpawn(m_settings) || m_gpuPendingMotion.distance > 0.0f)
            m_gpuLiveTimeRemaining = std::max(m_gpuLiveTimeRemaining,
                static_cast<double>(std::nextafter(m_settings.lifetimeMax, (std::numeric_limits<float>::infinity)()))
                    + (m_settings.trailEnabled ? static_cast<double>(m_settings.trailLifetime) : 0.0));
        m_gpuPendingSeed = nextRandom();
    }

    bool ParticleEffectComponent::submitGpuEmitter(const float deltaTime)
    {
        if (getGameObject() == nullptr || !std::isfinite(deltaTime) || deltaTime < 0.0f
            || deltaTime > MAX_SIMULATION_DELTA)
        {
            LOG_ERROR("[ParticleEffect] Cannot submit an invalid GPU emitter.");
            return false;
        }
        if (m_gpuEmitterId == 0)
        {
            static std::atomic<std::uint64_t> nextId{ 1 };
            m_gpuEmitterId = nextId.fetch_add(1, std::memory_order_relaxed);
            if (m_gpuEmitterId == 0)
            {
                LOG_CRITICAL("[ParticleEffect] GPU emitter identity space is exhausted.");
                return false;
            }
        }

        ParticleGpuEmitterSubmission submission;
        submission.id = m_gpuEmitterId;
        try
        {
            submission.settings = m_settings;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to snapshot GPU emitter settings.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] GPU emitter settings exceed snapshot capacity.");
            return false;
        }
        submission.worldMatrix = getGameObject()->getWorldMatrix();
        submission.deltaTime = deltaTime;
        if (deltaTime > 0.0f)
        {
            submission.emission = m_gpuPendingEmission;
            submission.motion = m_gpuPendingMotion;
        }
        submission.motion.resetDistance = m_gpuResetDistancePending;
        submission.spawnSeed = m_gpuPendingSeed;
        submission.clear = m_gpuClearPending;
        if (!ParticleRenderSubmissionQueue::instance().submitGpuEmitter(submission))
        {
            m_submissionFailed = true;
            return false;
        }
        m_gpuPendingDeltaTime = 0.0f;
        if (submission.emission.initial)
            m_burstPending = false;
        m_gpuPendingEmission = {};
        m_gpuPendingMotion = {};
        m_gpuResetDistancePending = false;
        m_gpuClearPending = false;
        m_submissionFailed = false;
        return true;
    }

    void ParticleEffectComponent::submitRenderData()
    {
        const GameObject* const gameObject = getGameObject();
        if (gameObject == nullptr)
            return;
        if (m_settings.simulationBackend == ParticleSimulationBackend::GPU
            && gpuSimulationFallbackReason(m_settings) == nullptr
            && GameSession::instance().state() != PlayState::Editing)
        {
            if (GameSession::instance().state() == PlayState::Paused)
                resetEmitterMotion();
            if (!m_emitting && m_gpuLiveTimeRemaining <= 0.0 && !m_gpuClearPending
                && !m_gpuPendingEmission.initial && m_gpuPendingEmission.activeTime == 0.0f
                && m_gpuPendingMotion.distance == 0.0f)
            {
                if (m_gpuEmitterId != 0)
                {
                    ParticleRenderSubmissionQueue::instance().removeGpuEmitter(m_gpuEmitterId);
                    m_gpuEmitterId = 0;
                }
                return;
            }
            const float delta = GameSession::instance().state() == PlayState::Paused ? 0.0f : m_gpuPendingDeltaTime;
            static_cast<void>(submitGpuEmitter(delta));
            return;
        }
        if (GameSession::instance().state() == PlayState::Paused)
            resetEmitterMotion();
        if (m_prewarmRemaining > 0.0 || m_delayRemaining > 0.0)
            return;
        if (!buildRenderSnapshot(gameObject->getWorldMatrix()))
        {
            m_submissionFailed = true;
            return;
        }
        m_submissionFailed = (!m_renderParticles.empty() || !m_renderTrails.empty())
            && !ParticleRenderSubmissionQueue::instance().submit(m_renderParticles, m_renderTrails);
    }

    bool ParticleEffectComponent::startPreview()
    {
        if (GameSession::instance().state() != PlayState::Editing || getGameObject() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Preview requires an editor scene object.");
            return false;
        }
        if (!play())
            return false;
        m_previewActive = true;
        m_previewPaused = false;
        m_previewElapsed = 0.0;
        m_previewRemainder = 0.0;
        m_previewSeekTime = 0.0f;
        m_previewSeeking = false;
        m_previewSeekFailed = false;
        return true;
    }

    void ParticleEffectComponent::stopPreview() noexcept
    {
        clear();
        m_previewActive = false;
        m_previewPaused = false;
        m_previewElapsed = 0.0;
        m_previewRemainder = 0.0;
        m_previewSeeking = false;
    }

    void ParticleEffectComponent::updatePreview(const float deltaTime)
    {
        const auto started = std::chrono::steady_clock::now();
        if (!m_previewActive || getGameObject() == nullptr)
            return;
        const auto revision = GameSession::instance().particlePreviewSelectionRevision();
        if (m_previewSelectionRevision != revision)
        {
            resetEmitterMotion();
            m_previewSelectionRevision = revision;
        }
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            LOG_ERROR("[ParticleEffect] Invalid preview delta time.");
            return;
        }
        if (m_prewarmRemaining > 0.0)
        {
            resetEmitterMotion();
            if (!m_previewPaused || m_previewSeeking)
                advancePrewarm(16);
            m_cpuMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            return;
        }
        if (m_previewSeeking)
        {
            resetEmitterMotion();
            constexpr int maxStepsPerFrame = 16;
            for (int step = 0; step < maxStepsPerFrame && m_previewSeekRemaining > 0.0; ++step)
            {
                const float delta = static_cast<float>(std::min(m_previewSeekRemaining,
                    static_cast<double>(SIMULATION_STEP)));
                simulate(delta, m_previewSeekWorld);
                m_previewSeekRemaining -= delta;
                m_previewElapsed += delta;
            }
            if (m_previewSeekRemaining <= 0.0)
                m_previewSeeking = false;
        }
        else if (!m_previewPaused)
        {
            m_previewRemainder += std::min(deltaTime, MAX_SIMULATION_DELTA) * m_previewSpeed
                * m_settings.simulationSpeed;
            if (m_previewSpeed == 0.0f || m_settings.simulationSpeed == 0.0f)
                resetEmitterMotion();
            else
                m_motionElapsed += deltaTime;
            const Matrix world = getGameObject()->getWorldMatrix();
            const double delay = m_delayRemaining;
            const double elapsed = m_emissionElapsed;
            const bool emitting = m_emitting;
            double simulated = 0.0;
            if (m_previewRemainder >= SIMULATION_STEP)
            {
                static_cast<void>(sampleEmitterMotion(static_cast<float>(m_motionElapsed), world));
                m_motionElapsed = 0.0;
            }
            while (m_previewRemainder >= SIMULATION_STEP)
            {
                simulate(SIMULATION_STEP, world);
                m_previewRemainder -= SIMULATION_STEP;
                m_previewElapsed += SIMULATION_STEP;
                simulated += SIMULATION_STEP;
            }
            emitByDistance(static_cast<float>(simulated), delay, elapsed, emitting, world);
        }
        submitRenderData();
        m_cpuMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }

    bool ParticleEffectComponent::setPreviewPaused(const bool paused)
    {
        if (!m_previewActive || m_previewSeeking || GameSession::instance().state() != PlayState::Editing)
        {
            LOG_ERROR("[ParticleEffect] Preview cannot be paused or resumed in its current state.");
            return false;
        }
        m_previewPaused = paused;
        resetEmitterMotion();
        return true;
    }

    bool ParticleEffectComponent::stepPreview()
    {
        if (!m_previewActive || !m_previewPaused || m_previewSeeking || getGameObject() == nullptr
            || GameSession::instance().state() != PlayState::Editing)
        {
            LOG_ERROR("[ParticleEffect] Preview step requires a paused editor preview.");
            return false;
        }
        if (m_prewarmRemaining > 0.0)
            advancePrewarm(1);
        else
        {
            simulate(SIMULATION_STEP, getGameObject()->getWorldMatrix());
            m_previewElapsed += SIMULATION_STEP;
        }
        return true;
    }

    bool ParticleEffectComponent::setPreviewSpeed(const float speed)
    {
        if (!std::isfinite(speed) || speed < 0.0f || speed > 4.0f)
        {
            LOG_ERROR("[ParticleEffect] Preview speed must be in [0, 4].");
            return false;
        }
        m_previewSpeed = speed;
        return true;
    }

    bool ParticleEffectComponent::seekPreview(const float seconds)
    {
        constexpr float maxSeekSeconds = 60.0f;
        if (!std::isfinite(seconds) || seconds < 0.0f || seconds > maxSeekSeconds
            || GameSession::instance().state() != PlayState::Editing || getGameObject() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Preview seek requires an editor object and a time in [0, 60].");
            return false;
        }
        if (!play())
            return false;
        m_previewSeekWorld = getGameObject()->getWorldMatrix();
        m_previewSeekRemaining = seconds;
        m_previewSeeking = seconds > 0.0f || m_prewarmRemaining > 0.0;
        m_previewSeekFailed = false;
        m_previewActive = true;
        m_previewPaused = true;
        m_previewElapsed = 0.0;
        m_previewRemainder = 0.0;
        m_previewSeekTime = seconds;
        return true;
    }

    void ParticleEffectComponent::onImGui()
    {
        const bool playing = GameSession::instance().isPlaying();
        auto& manager = ParticleEffectAssetManager::instance();
        const bool assetMissing = m_asset.isValid() && !manager.get(m_asset);
        if (assetMissing)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1),
                "Effect asset reference is unavailable; choose Inline or another asset.");
        else if (!playing && !synchronizeAsset())
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Effect asset synchronization failed; see the engine log.");
        ImGui::BeginDisabled((!playing && m_previewSeeking) || m_prewarmRemaining > 0.0);
        ImGui::BeginDisabled(playing);
        const auto snapshot = manager.get(m_asset);
        const auto path = manager.getPath(m_asset);
        const auto label = !snapshot ? (m_asset.isValid() ? "<Missing>" : "<Inline>") : assetLabel(path);
        if (ImGui::BeginCombo("Effect Asset", label.c_str()))
        {
            if (ImGui::Selectable("<Inline>", !m_asset.isValid()))
                static_cast<void>(setAsset({}));
            for (const auto candidate : manager.getAllHandles())
            {
                const auto candidatePath = manager.getPath(candidate);
                const auto candidateName = assetLabel(candidatePath);
                ImGui::PushID(static_cast<int>(candidate.index));
                if (ImGui::Selectable(candidateName.c_str(), candidate == m_asset))
                    static_cast<void>(setAsset(candidate));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("New Asset"))
        {
            const auto created = manager.create(m_settings);
            if (created.isValid())
                static_cast<void>(setAsset(created));
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Asset..."))
        {
            const GameObject* const object = getGameObject();
            ObjectGUID sceneGuid;
            if (object)
            {
                SceneManager::instance().forEachScene([&](Scene& scene)
                {
                    if (scene.find(object->getGUID()) == object)
                        sceneGuid = scene.getGUID();
                });
            }
            if (object && sceneGuid.isValid())
                requestOpenAsset(sceneGuid, object->getGUID(),
                    static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw));
            else
                LOG_ERROR("[ParticleEffect] Cannot open an asset without a managed scene owner.");
        }
        if (m_asset.isValid())
        {
            const auto assetPath = manager.getPath(m_asset);
            const auto utf8 = String::fromWide(assetPath.c_str());
            ImGui::TextWrapped("%s", assetPath.empty() ? "Unsaved asset: save before saving the Scene/Prefab." : utf8.c_str());
            if (manager.isDirty(m_asset))
                ImGui::TextDisabled("Asset has unsaved changes.");
            ImGui::BeginDisabled(assetPath.empty());
            if (ImGui::Button("Save Asset"))
                static_cast<void>(manager.save(m_asset));
            ImGui::SameLine();
            if (ImGui::Button("Reload Asset"))
                static_cast<void>(manager.reload(m_asset));
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!assetPath.empty());
            if (ImGui::Button("Save Asset As..."))
                requestSaveAsset(m_asset, static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw));
            ImGui::EndDisabled();
            if (!assetPath.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Use Make Unique to save a separate asset.");
            ImGui::SameLine();
            if (ImGui::Button("Make Unique"))
            {
                const auto created = manager.create(m_settings);
                if (created.isValid())
                    static_cast<void>(setAsset(created));
            }
            ImGui::TextDisabled("Edits affect all components sharing this asset.");
        }
        ImGui::EndDisabled();
        if (playing && m_asset.isValid())
            ImGui::TextDisabled("Play Mode edits are local and discarded on Stop.");
        ParticleEffectSettings value = m_settings;
        bool changed = false;
        const auto textureLabel = value.texturePath.empty()
            ? String("<Procedural circle>") : String::fromWide(value.texturePath.c_str());
        ImGui::TextWrapped("Texture: %s", textureLabel.c_str());
        if (ImGui::Button("Select Texture..."))
        {
            const GameObject* const object = getGameObject();
            ObjectGUID sceneGuid;
            if (object)
                SceneManager::instance().forEachScene([&](Scene& scene)
                {
                    if (scene.find(object->getGUID()) == object)
                        sceneGuid = scene.getGUID();
                });
            if (object && sceneGuid.isValid())
                requestTexture(sceneGuid, object->getGUID(),
                    static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), &ParticleEffectSettings::texturePath);
            else
                LOG_ERROR("[ParticleEffect] Cannot select a texture without a managed scene owner.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Texture"))
        {
            value.texturePath.clear();
            changed = true;
        }
        const auto editAuxiliaryTexture = [&](const char* label, std::filesystem::path ParticleEffectSettings::* field,
            const bool model = false)
        {
            ImGui::PushID(label);
            const auto& path = value.*field;
            const auto text = path.empty() ? String("<None>") : String::fromWide(path.c_str());
            ImGui::TextWrapped("%s: %s", label, text.c_str());
            if (ImGui::Button("Select..."))
            {
                const GameObject* const object = getGameObject();
                ObjectGUID sceneGuid;
                if (object)
                    SceneManager::instance().forEachScene([&](Scene& scene)
                    {
                        if (scene.find(object->getGUID()) == object)
                            sceneGuid = scene.getGUID();
                    });
                if (object && sceneGuid.isValid())
                    requestTexture(sceneGuid, object->getGUID(),
                        static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), field, model);
                else
                    LOG_ERROR("[ParticleEffect] Cannot select a texture without a managed scene owner.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                (value.*field).clear();
                changed = true;
            }
            ImGui::PopID();
        };
        editAuxiliaryTexture("Secondary Texture", &ParticleEffectSettings::secondaryTexturePath);
        editAuxiliaryTexture("Mask Texture", &ParticleEffectSettings::maskTexturePath);
        editAuxiliaryTexture("Distortion Texture", &ParticleEffectSettings::distortionTexturePath);
        editAuxiliaryTexture("Mesh Model", &ParticleEffectSettings::meshPath, true);
        changed |= ImGui::DragFloat("UV Distortion", &value.uvDistortion, 0.01f, -10.0f, 10.0f);
        changed |= ImGui::DragFloat("Refraction (pixels)", &value.refractionPixels, 0.1f, -1024.0f, 1024.0f);
        int blendMode = static_cast<int>(value.blendMode);
        if (ImGui::Combo("Blend", &blendMode, "Alpha\0Additive\0Premultiplied Alpha\0Multiply\0Screen\0"))
        {
            value.blendMode = static_cast<ParticleBlendMode>(blendMode);
            changed = true;
        }
        int orientation = static_cast<int>(value.orientation);
        if (ImGui::Combo("Orientation", &orientation, "Camera\0Velocity\0Axis\0Horizontal\0Direction\0Stretched\0"))
        {
            value.orientation = static_cast<ParticleOrientation>(orientation);
            changed = true;
        }
        if (value.orientation == ParticleOrientation::Axis || value.orientation == ParticleOrientation::Direction)
            changed |= ImGui::DragFloat3("Orientation Direction (World)", &value.orientationDirection.x, 0.01f);
        if (value.orientation == ParticleOrientation::Stretched)
            changed |= ImGui::DragFloat("Stretch By Speed", &value.stretchBySpeed, 0.01f, 0.0f, 10000.0f);
        changed |= ImGui::DragFloat("Soft Particle Distance", &value.softDistance, 0.01f, 0.0f, 10000.0f);
        changed |= ImGui::DragFloat2("UV Scroll (cycles/sec)", &value.uvScroll.x, 0.01f, -10000.0f, 10000.0f);
        int columns = static_cast<int>(value.flipbookColumns);
        int rows = static_cast<int>(value.flipbookRows);
        if (ImGui::SliderInt("Flipbook Columns", &columns, 1, 64))
        {
            value.flipbookColumns = static_cast<std::uint32_t>(columns);
            changed = true;
        }
        if (ImGui::SliderInt("Flipbook Rows", &rows, 1, 64))
        {
            value.flipbookRows = static_cast<std::uint32_t>(rows);
            changed = true;
        }
        int flipbookPlayback = static_cast<int>(value.flipbookPlayback);
        if (ImGui::Combo("Flipbook Playback", &flipbookPlayback, "Lifetime\0Frames Per Second\0Custom Timings\0"))
        {
            value.flipbookPlayback = static_cast<ParticleFlipbookPlayback>(flipbookPlayback);
            if (value.flipbookPlayback == ParticleFlipbookPlayback::Custom && value.flipbookFrames.empty())
                for (std::uint32_t frame = 0; frame < value.flipbookColumns * value.flipbookRows; ++frame)
                    value.flipbookFrames.push_back({ frame, 1.0f / value.flipbookFramesPerSecond });
            changed = true;
        }
        if (value.flipbookPlayback == ParticleFlipbookPlayback::FramesPerSecond)
            changed |= ImGui::DragFloat("Flipbook FPS", &value.flipbookFramesPerSecond, 0.1f, 0.01f, 1000.0f);
        if (value.flipbookPlayback != ParticleFlipbookPlayback::Lifetime)
        {
            int loopCount = static_cast<int>(value.flipbookLoopCount);
            if (ImGui::DragInt("Flipbook Loop Count (0 = infinite)", &loopCount, 1.0f, 0, 10000))
            {
                value.flipbookLoopCount = static_cast<std::uint32_t>(loopCount);
                changed = true;
            }
        }
        changed |= ImGui::Checkbox("Random Flipbook Start", &value.flipbookRandomStartFrame);
        changed |= ImGui::Checkbox("Blend Flipbook Frames", &value.flipbookFrameBlending);
        if (value.flipbookPlayback == ParticleFlipbookPlayback::Custom)
        {
            ImGui::TextDisabled("Each frame has an atlas index and duration in seconds.");
            for (std::size_t index = 0; index < value.flipbookFrames.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                int frame = static_cast<int>(value.flipbookFrames[index].frame);
                changed |= ImGui::DragInt("Frame", &frame, 1.0f, 0,
                    static_cast<int>(value.flipbookColumns * value.flipbookRows - 1));
                value.flipbookFrames[index].frame = static_cast<std::uint32_t>(frame);
                changed |= ImGui::DragFloat("Duration (sec)", &value.flipbookFrames[index].duration,
                    0.005f, 0.001f, 3600.0f, "%.3f");
                if (ImGui::SmallButton("Remove Frame"))
                {
                    value.flipbookFrames.erase(value.flipbookFrames.begin()
                        + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(value.flipbookFrames.size() >= MAX_FLIPBOOK_FRAMES);
            if (ImGui::Button("Add Flipbook Frame"))
            {
                const std::uint32_t count = value.flipbookColumns * value.flipbookRows;
                const std::uint32_t frame = value.flipbookFrames.empty() ? 0
                    : (value.flipbookFrames.back().frame + 1) % count;
                value.flipbookFrames.push_back({ frame, 1.0f / value.flipbookFramesPerSecond });
                changed = true;
            }
            ImGui::EndDisabled();
        }
        changed |= ImGui::Checkbox("Trail", &value.trailEnabled);
        if (value.trailEnabled)
        {
            changed |= ImGui::DragFloat("Trail Lifetime", &value.trailLifetime, 0.01f, 0.01f, 60.0f);
            changed |= ImGui::DragFloat("Trail Width", &value.trailWidth, 0.01f, 0.001f, 10000.0f);
            changed |= ImGui::DragFloat("Trail Min Distance", &value.trailMinDistance, 0.01f, 0.0f, 10000.0f);
            ImGui::TextDisabled("New particles record world-space trails using the selected Blend.");
        }
        changed |= ImGui::Checkbox("Play On Awake", &value.playOnAwake);
        changed |= ImGui::Checkbox("Looping", &value.looping);
        if (!value.looping)
            value.prewarm = false;
        changed |= ImGui::Checkbox("Local Space", &value.localSpace);
        const char* backendLabels[] = { "CPU", "GPU" };
        int backend = static_cast<int>(value.simulationBackend);
        if (ImGui::Combo("Simulation Backend", &backend, backendLabels, 2))
        {
            value.simulationBackend = static_cast<ParticleSimulationBackend>(backend);
            changed = true;
        }
        if (value.simulationBackend == ParticleSimulationBackend::GPU)
        {
            if (const char* reason = gpuSimulationFallbackReason(value, m_previewActive))
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "CPU fallback: %s", reason);
            else
                ImGui::TextDisabled("GPU simulation is enabled for supported settings.");
        }
        changed |= ImGui::SliderFloat("Simulation Speed", &value.simulationSpeed, 0.0f, 4.0f, "%.2fx");
        changed |= ImGui::Checkbox("Clear On Stop", &value.clearOnStop);
        int maxParticles = static_cast<int>(value.maxParticles);
        if (ImGui::SliderInt("Max Particles", &maxParticles, 1, static_cast<int>(MAX_PARTICLES)))
        {
            value.maxParticles = static_cast<std::uint32_t>(maxParticles);
            changed = true;
        }
        changed |= ImGui::DragFloat("Emission Rate", &value.emissionRate, 0.1f, 0.0f, 10000.0f);
        if (ImGui::TreeNode("Emission Rate Over Emitter Cycle"))
        {
            ImGui::TextDisabled("Keys set absolute particles/second over normalized emitter-cycle time.");
            changed |= editScalarGraph("emissionRate", value.emissionRateKeys, 10000.0f, false);
            for (std::size_t index = 0; index < value.emissionRateKeys.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(value.emissionRateKeys, index);
                changed |= ImGui::DragFloat("Particles / sec", &value.emissionRateKeys[index].value,
                    0.1f, 0.0f, 10000.0f);
                if (ImGui::SmallButton("Remove Rate Key"))
                {
                    value.emissionRateKeys.erase(value.emissionRateKeys.begin()
                        + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(value.emissionRateKeys.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Rate Key"))
            {
                addCurveKey(value.emissionRateKeys, value.emissionRate);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        changed |= ImGui::DragFloat("Distance Per Particle", &value.distancePerParticle,
            0.01f, 0.0f, 10000.0f, "%.4f");
        if (value.distancePerParticle > 0.0f && value.distancePerParticle < 0.0001f)
            value.distancePerParticle = 0.0001f;
        changed |= ImGui::SliderFloat("Inherit Emitter Velocity", &value.inheritVelocity, 0.0f, 4.0f, "%.2f");
        int burstCount = static_cast<int>(value.burstCount);
        if (ImGui::SliderInt("Burst Count", &burstCount, 0, static_cast<int>(MAX_PARTICLES)))
        {
            value.burstCount = static_cast<std::uint32_t>(burstCount);
            changed = true;
        }
        if (ImGui::DragFloat("Duration", &value.duration, 0.05f, 0.0f, 3600.0f))
        {
            for (auto& burst : value.bursts)
            {
                burst.time = std::min(burst.time, value.duration);
                if (burst.repeatCount > 1)
                    burst.repeatInterval = std::min(burst.repeatInterval,
                        (value.duration - burst.time) / static_cast<float>(burst.repeatCount - 1));
                if (burst.repeatInterval <= 0.0f)
                    burst.repeatCount = 1;
            }
            changed = true;
        }
        changed |= ImGui::DragFloat("Start Delay", &value.startDelay, 0.01f, 0.0f, 3600.0f);
        ImGui::BeginDisabled(!value.looping || value.duration <= 0.0f);
        changed |= ImGui::Checkbox("Prewarm", &value.prewarm);
        ImGui::EndDisabled();
        if (value.duration <= 0.0f)
            value.prewarm = false;
        if (ImGui::TreeNode("Scheduled Bursts"))
        {
            ImGui::TextDisabled("Times are seconds within the emitter cycle.");
            for (std::size_t index = 0; index < value.bursts.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                auto& burst = value.bursts[index];
                if (ImGui::SliderFloat("Time", &burst.time, 0.0f, value.duration, "%.3f"))
                {
                    if (burst.repeatCount > 1)
                        burst.repeatInterval = std::min(burst.repeatInterval,
                            (value.duration - burst.time) / static_cast<float>(burst.repeatCount - 1));
                    if (burst.repeatInterval <= 0.0f)
                        burst.repeatCount = 1;
                    changed = true;
                }
                if (ImGui::Checkbox("Random Count", &burst.randomCount))
                {
                    if (burst.randomCount)
                        burst.countMin = burst.countMax = burst.count;
                    changed = true;
                }
                if (burst.randomCount)
                {
                    int minimum = static_cast<int>(burst.countMin);
                    int maximum = static_cast<int>(burst.countMax);
                    if (ImGui::DragIntRange2("Count Range", &minimum, &maximum, 1, 0, static_cast<int>(MAX_PARTICLES)))
                    {
                        burst.countMin = static_cast<std::uint32_t>(minimum);
                        burst.countMax = static_cast<std::uint32_t>(maximum);
                        changed = true;
                    }
                }
                else
                {
                    int count = static_cast<int>(burst.count);
                    if (ImGui::SliderInt("Count", &count, 0, static_cast<int>(MAX_PARTICLES)))
                    {
                        burst.count = static_cast<std::uint32_t>(count);
                        changed = true;
                    }
                }
                changed |= ImGui::SliderFloat("Probability", &burst.probability, 0.0f, 1.0f);
                int repeats = static_cast<int>(burst.repeatCount);
                if (ImGui::SliderInt("Occurrences", &repeats, 1, 128))
                {
                    burst.repeatCount = static_cast<std::uint32_t>(repeats);
                    if (burst.repeatCount > 1)
                        burst.repeatInterval = (value.duration - burst.time) / static_cast<float>(burst.repeatCount - 1);
                    if (burst.repeatInterval <= 0.0f)
                        burst.repeatCount = 1;
                    changed = true;
                }
                if (burst.repeatCount > 1)
                    changed |= ImGui::DragFloat("Repeat Interval", &burst.repeatInterval, 0.001f,
                        std::numeric_limits<float>::min(),
                        (value.duration - burst.time) / static_cast<float>(burst.repeatCount - 1), "%.4f");
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    value.bursts.erase(value.bursts.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(value.bursts.size() >= MAX_BURSTS);
            if (ImGui::Button("Add Burst"))
            {
                value.bursts.push_back({ 0.0f, 1 });
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        changed |= ImGui::DragFloatRange2("Lifetime", &value.lifetimeMin, &value.lifetimeMax,
            0.01f, 0.01f, 3600.0f, "%.2f");
        int shape = static_cast<int>(value.shape);
        if (ImGui::Combo("Emission Shape", &shape, "Sphere\0Box\0Disk\0"))
        {
            value.shape = static_cast<ParticleEmissionShape>(shape);
            changed = true;
        }
        if (value.shape == ParticleEmissionShape::Box)
            changed |= ImGui::DragFloat3("Box Half Extents", &value.boxExtents.x, 0.01f, 0.0f, 10000.0f);
        else
            changed |= ImGui::DragFloat("Shape Radius", &value.shapeRadius, 0.01f, 0.0f, 10000.0f);
        changed |= ImGui::DragFloat3("Direction", &value.direction.x, 0.01f, -1.0f, 1.0f);
        changed |= ImGui::SliderFloat("Spread Angle", &value.spreadAngleDegrees, 0.0f, 180.0f);
        changed |= ImGui::DragFloatRange2("Speed", &value.speedMin, &value.speedMax,
            0.01f, 0.0f, 10000.0f, "%.2f");
        changed |= ImGui::DragFloat3("Gravity", &value.gravity.x, 0.1f, -10000.0f, 10000.0f);
        changed |= ImGui::DragFloat("Drag", &value.drag, 0.01f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("Start Size", &value.startSize, 0.01f, 0.0f, 10000.0f);
        changed |= ImGui::DragFloat("End Size", &value.endSize, 0.01f, 0.0f, 10000.0f);
        changed |= ImGui::DragFloat2("Initial Width / Height Scale", &value.initialSizeScale.x, 0.01f, 0.0f, 100.0f);
        changed |= ImGui::ColorEdit4("Start Color", &value.startColor.x, ImGuiColorEditFlags_HDR);
        changed |= ImGui::ColorEdit4("End Color", &value.endColor.x, ImGuiColorEditFlags_HDR);
        changed |= ImGui::Checkbox("Random Initial Size and Color", &value.randomInitial);
        if (value.randomInitial)
        {
            changed |= ImGui::DragFloatRange2("Initial Size Range", &value.initialSizeMin, &value.initialSizeMax,
                0.01f, 0.0f, 10000.0f);
            if (ImGui::ColorEdit4("Initial Color Min", &value.initialColorMin.x, ImGuiColorEditFlags_HDR))
            {
                value.initialColorMin.x = std::clamp(value.initialColorMin.x, 0.0f, value.initialColorMax.x);
                value.initialColorMin.y = std::clamp(value.initialColorMin.y, 0.0f, value.initialColorMax.y);
                value.initialColorMin.z = std::clamp(value.initialColorMin.z, 0.0f, value.initialColorMax.z);
                value.initialColorMin.w = std::clamp(value.initialColorMin.w, 0.0f, value.initialColorMax.w);
                changed = true;
            }
            if (ImGui::ColorEdit4("Initial Color Max", &value.initialColorMax.x, ImGuiColorEditFlags_HDR))
            {
                value.initialColorMax.x = std::clamp(value.initialColorMax.x, value.initialColorMin.x, 100.0f);
                value.initialColorMax.y = std::clamp(value.initialColorMax.y, value.initialColorMin.y, 100.0f);
                value.initialColorMax.z = std::clamp(value.initialColorMax.z, value.initialColorMin.z, 100.0f);
                value.initialColorMax.w = std::clamp(value.initialColorMax.w, value.initialColorMin.w, 1.0f);
                changed = true;
            }
        }
        changed |= ImGui::DragFloatRange2("Initial Rotation (rad)", &value.initialRotationMin,
            &value.initialRotationMax, 0.01f, -10000.0f, 10000.0f);
        changed |= ImGui::DragFloatRange2("Angular Velocity (rad/s)", &value.angularVelocityMin,
            &value.angularVelocityMax, 0.01f, -10000.0f, 10000.0f);
        m_curveEditIndex = std::clamp(m_curveEditIndex, 0, static_cast<int>(value.curveVariants.size()));
        ImGui::SliderInt("Curve Set", &m_curveEditIndex, 0, static_cast<int>(value.curveVariants.size()));
        ImGui::SameLine();
        ImGui::BeginDisabled(value.curveVariants.size() >= 3);
        if (ImGui::Button("Add Curve Set"))
        {
            value.curveVariants.push_back(value.curves);
            m_curveEditIndex = static_cast<int>(value.curveVariants.size());
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_curveEditIndex == 0);
        if (ImGui::Button("Remove Curve Set"))
        {
            value.curveVariants.erase(value.curveVariants.begin() + m_curveEditIndex - 1);
            m_curveEditIndex = 0;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Each new particle chooses one curve set uniformly.");
        auto& curves = m_curveEditIndex == 0 ? value.curves : value.curveVariants[m_curveEditIndex - 1];
        changed |= ImGui::Checkbox("Smooth Lifetime Curves", &curves.smooth);
        if (ImGui::TreeNode("Color Over Lifetime"))
        {
            ImGui::TextDisabled("Keys override Start/End Color; no keys use the original interpolation.");
            changed |= editGradient(curves.color, curves.smooth);
            for (std::size_t index = 0; index < curves.color.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(curves.color, index);
                changed |= ImGui::ColorEdit4("Color", &curves.color[index].value.x, ImGuiColorEditFlags_HDR);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    curves.color.erase(curves.color.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(curves.color.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Color Key"))
            {
                addCurveKey(curves.color, value.startColor);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Size Over Lifetime"))
        {
            ImGui::TextDisabled("Keys override Start/End Size; no keys use the original interpolation.");
            changed |= editScalarGraph("size", curves.size, 10000.0f, curves.smooth);
            for (std::size_t index = 0; index < curves.size.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(curves.size, index);
                changed |= ImGui::DragFloat("Size", &curves.size[index].value, 0.01f, 0.0f, 10000.0f);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    curves.size.erase(curves.size.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(curves.size.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Size Key"))
            {
                addCurveKey(curves.size, value.startSize);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        const auto editAxisCurve = [&](const char* title, std::vector<ParticleScalarKey>& keys)
        {
            if (!ImGui::TreeNode(title))
                return;
            ImGui::TextDisabled("Keys multiply the base value; no keys mean a multiplier of 1.");
            changed |= editScalarGraph("axis", keys, 100.0f, curves.smooth);
            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(keys, index);
                changed |= ImGui::DragFloat("Multiplier", &keys[index].value, 0.01f, 0.0f, 100.0f);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    keys.erase(keys.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(keys.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Key"))
            {
                addCurveKey(keys, 1.0f);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        };
        editAxisCurve("Width Over Lifetime", curves.width);
        editAxisCurve("Height Over Lifetime", curves.height);
        editAxisCurve("Gravity Multiplier Over Lifetime", curves.gravity);
        editAxisCurve("Drag Multiplier Over Lifetime", curves.drag);
        const auto editVectorCurve = [&](const char* title, std::vector<ParticleVectorKey>& keys)
        {
            if (!ImGui::TreeNode(title))
                return;
            ImGui::TextDisabled("Values are in simulation space. Empty means zero.");
            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(keys, index);
                changed |= ImGui::DragFloat3("Value", &keys[index].value.x, 0.01f, -10000.0f, 10000.0f);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    keys.erase(keys.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(keys.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Key"))
            {
                addCurveKey(keys, Vector3::Zero);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        };
        editVectorCurve("Acceleration Over Lifetime", curves.acceleration);
        editVectorCurve("Velocity Over Lifetime", curves.velocity);
        if (ImGui::TreeNode("Speed Over Lifetime"))
        {
            ImGui::TextDisabled("Keys multiply particle movement; no keys mean a multiplier of 1.");
            changed |= editScalarGraph("speed", curves.speed, 100.0f, curves.smooth);
            for (std::size_t index = 0; index < curves.speed.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(curves.speed, index);
                changed |= ImGui::DragFloat("Multiplier", &curves.speed[index].value, 0.01f, 0.0f, 100.0f);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    curves.speed.erase(curves.speed.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(curves.speed.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Speed Key"))
            {
                addCurveKey(curves.speed, 1.0f);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Angular Speed Over Lifetime"))
        {
            ImGui::TextDisabled("Keys multiply angular velocity; no keys mean a multiplier of 1.");
            for (std::size_t index = 0; index < curves.angularSpeed.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= editCurveTime(curves.angularSpeed, index);
                changed |= ImGui::DragFloat("Multiplier", &curves.angularSpeed[index].value,
                    0.01f, -100.0f, 100.0f);
                const bool remove = ImGui::Button("Remove");
                ImGui::PopID();
                if (remove)
                {
                    curves.angularSpeed.erase(curves.angularSpeed.begin() + index);
                    changed = true;
                    break;
                }
            }
            ImGui::BeginDisabled(curves.angularSpeed.size() >= MAX_CURVE_KEYS);
            if (ImGui::Button("Add Angular Speed Key"))
            {
                addCurveKey(curves.angularSpeed, 1.0f);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        changed |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &value.seed);

        const ParticleEffectSettings previous = m_settings;
        if (changed && setSettings(value) && m_asset.isValid() && !playing)
        {
            if (manager.update(m_asset, value))
                m_assetSnapshot = manager.get(m_asset);
            else if (!setSettings(previous))
                LOG_ERROR("[ParticleEffect] Failed to restore settings after an asset update failure.");
        }
        ImGui::EndDisabled();

        if (m_settings.simulationBackend == ParticleSimulationBackend::GPU
            && gpuSimulationFallbackReason(m_settings) == nullptr && playing)
            ImGui::TextDisabled("GPU particle, trail and drop counts are not read back.");
        else
        {
            ImGui::Text("Live Particles: %u", static_cast<unsigned>(m_particles.size()));
            ImGui::Text("Live Trails: %zu | Submitted Segments: %zu", m_activeTrails, m_renderTrails.size());
            ImGui::Text("Dropped Particles: %llu | Dropped Trails: %llu",
                static_cast<unsigned long long>(m_droppedParticles), static_cast<unsigned long long>(m_droppedTrails));
        }
        ImGui::Text("Effect CPU last update + snapshot: %.3f ms", m_cpuMilliseconds);
        if (m_prewarmRemaining > 0.0)
            ImGui::TextDisabled("Prewarming: %.2f seconds remaining.", m_prewarmRemaining);
        if (m_prewarmFailed)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Prewarm interrupted; restart the effect.");
        const auto gpuStats = ParticleRenderSubmissionQueue::instance().gpuStatistics();
        if (gpuStats.frameId != 0)
        {
            ImGui::Text("All particles GPU: %.3f ms | Draw submissions: %zu (frame %llu)",
                gpuStats.milliseconds, gpuStats.drawCalls, static_cast<unsigned long long>(gpuStats.frameId));
            ImGui::Text("CPU-simulated particle instances: %zu | Trail segments: %zu | Independent ribbons: %zu",
                gpuStats.cpuParticles, gpuStats.trailSegments, gpuStats.ribbons);
        }
        else
            ImGui::TextDisabled("All particles GPU: no completed measurement.");
        if (m_submissionFailed)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Particle render submission failed; see the engine log.");
        if (m_trailBudgetReported)
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Trail capacity exhausted; some particles have no trail.");
        const auto reportResource = [](const char* label, const std::filesystem::path& path, const ParticleResourceKind kind)
        {
            if (path.empty())
                return;
            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(path, error);
            if (error || !exists || ParticleRenderSubmissionQueue::instance().resourceFailed(path, kind))
                ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s unavailable; see the engine log.", label);
        };
        reportResource("Particle texture", m_settings.texturePath, ParticleResourceKind::ColorTexture);
        reportResource("Secondary texture", m_settings.secondaryTexturePath, ParticleResourceKind::ColorTexture);
        reportResource("Mask texture", m_settings.maskTexturePath, ParticleResourceKind::LinearTexture);
        reportResource("Distortion texture", m_settings.distortionTexturePath, ParticleResourceKind::LinearTexture);
        reportResource("Mesh model", m_settings.meshPath, ParticleResourceKind::Mesh);
        if (!playing)
        {
            if (ImGui::Button("Preview Play"))
                static_cast<void>(startPreview());
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_previewActive);
            ImGui::BeginDisabled(m_previewSeeking);
            if (ImGui::Button(m_previewPaused ? "Preview Resume" : "Preview Pause"))
                static_cast<void>(setPreviewPaused(!m_previewPaused));
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Preview Stop"))
                stopPreview();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!m_previewActive || !m_previewPaused || m_previewSeeking);
            if (ImGui::Button("Preview Step"))
                static_cast<void>(stepPreview());
            ImGui::EndDisabled();
            float previewSpeed = m_previewSpeed;
            if (ImGui::SliderFloat("Preview Speed", &previewSpeed, 0.0f, 4.0f, "%.2fx"))
                static_cast<void>(setPreviewSpeed(previewSpeed));
            ImGui::Text("Preview Time: %.3f s", m_previewElapsed);
            if (m_previewSeeking)
                ImGui::TextDisabled("Seeking...");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat("Seek (seconds)", &m_previewSeekTime, 0.1f, 1.0f, "%.3f");
            ImGui::SameLine();
            if (ImGui::Button("Seek"))
                m_previewSeekFailed = !seekPreview(m_previewSeekTime);
            if (m_previewSeekFailed)
                ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Seek failed or was interrupted; see the engine log.");
            ImGui::TextDisabled("Seek replays at the current transform; range 0-60 s.");
        }
        else
        {
            if (ImGui::Button("Discard Runtime Settings"))
            {
                m_applySucceeded = getGameObject() != nullptr
                    && GameSession::instance().discardParticleEffectSettings(getGameObject()->getGUID(), *this);
                m_applyStatusVisible = true;
                m_statusWasDiscard = true;
                if (!m_applySucceeded)
                    LOG_ERROR("[ParticleEffect] Could not restore editor settings.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply Settings to Editor"))
            {
                m_applySucceeded = getGameObject() != nullptr
                    && GameSession::instance().applyParticleEffectSettings(getGameObject()->getGUID(), m_settings);
                m_applyStatusVisible = true;
                m_statusWasDiscard = false;
            }
            if (m_applyStatusVisible)
                ImGui::TextDisabled("%s", m_applySucceeded
                    ? (m_statusWasDiscard ? "Runtime settings restored from editor."
                        : "Applied as independent editor settings; save the scene to persist.")
                    : (m_statusWasDiscard ? "Discard failed; see the engine log." : "Apply failed; see the engine log."));
            if (ImGui::Button("Play"))
                static_cast<void>(play());
            ImGui::SameLine();
            if (ImGui::Button(m_settings.clearOnStop ? "Stop and Clear" : "Stop Emission"))
                stop();
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                clear();
        }
    }

    std::uint32_t ParticleEffectComponent::nextRandom() noexcept
    {
        std::uint32_t value = m_randomState;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        m_randomState = value;
        return value;
    }

    float ParticleEffectComponent::randomUnit() noexcept
    {
        return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
    }

    Vector4 ParticleEffectComponent::particleColor(const Particle& particle) const noexcept
    {
        const float age = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
        return particle.curves && !particle.curves->color.empty()
            ? evaluateCurve(particle.curves->color, age, particle.curves->smooth)
            : particle.startColor + (particle.endColor - particle.startColor) * age;
    }

    void ParticleEffectComponent::appendTrailPoint(Trail& trail, const TrailPoint& point) noexcept
    {
        if (trail.count == TRAIL_POINT_CAPACITY)
        {
            trail.first = (trail.first + 1) % TRAIL_POINT_CAPACITY;
            --trail.count;
        }
        trail.points[(trail.first + trail.count) % TRAIL_POINT_CAPACITY] = point;
        ++trail.count;
    }

    void ParticleEffectComponent::detachTrail(Trail& trail) noexcept
    {
        if (!trail.attached)
            return;
        if (trail.count != 0)
        {
            TrailPoint& last = trail.points[(trail.first + trail.count - 1) % TRAIL_POINT_CAPACITY];
            if ((trail.tip.position - last.position).LengthSquared() <= 1e-10f)
                last = trail.tip;
            else
                appendTrailPoint(trail, trail.tip);
        }
        else
            appendTrailPoint(trail, trail.tip);
        trail.attached = false;
    }

    void ParticleEffectComponent::updateTrail(Particle& particle, const Matrix& worldMatrix, const bool detach) noexcept
    {
        if (particle.trailIndex == NO_TRAIL)
            return;
        Trail& trail = m_trails[particle.trailIndex];
        const Vector3 position = m_settings.localSpace
            ? Vector3::Transform(particle.position, worldMatrix) : particle.position;
        const Vector4 color = particleColor(particle);
        if (!isFinite(position) || !isFinite(color))
        {
            LOG_ERROR("[ParticleEffect] Cannot update a trail with invalid particle data.");
            if (detach)
                detachTrail(trail);
            return;
        }
        trail.tip = { position, color, 0.0f };
        if (trail.count == 0)
            appendTrailPoint(trail, trail.tip);
        else
        {
            const TrailPoint& last = trail.points[(trail.first + trail.count - 1) % TRAIL_POINT_CAPACITY];
            const float distanceSquared = (position - last.position).LengthSquared();
            if (std::isfinite(distanceSquared) && distanceSquared > 1e-10f
                && distanceSquared >= trail.minDistance * trail.minDistance)
                appendTrailPoint(trail, trail.tip);
        }
        if (detach)
            detachTrail(trail);
    }

    void ParticleEffectComponent::clearTrails() noexcept
    {
        m_trails.clear();
        m_freeTrails.clear();
        m_renderTrails.clear();
        m_activeTrails = 0;
        m_trailBudgetReported = false;
    }

    void ParticleEffectComponent::emitBurst(const ParticleBurst& burst, const double occurrences,
        const Matrix& worldMatrix)
    {
        if (occurrences <= 0.0 || burst.probability == 0.0f)
            return;
        const auto emitCount = [&](const double requested)
        {
            const std::size_t available = m_settings.maxParticles - m_particles.size();
            const auto count = static_cast<std::uint32_t>(std::min(static_cast<double>(available), requested));
            const double dropped = std::max(0.0, requested - count);
            const auto remainingCapacity = UINT64_MAX - m_droppedParticles;
            if (dropped >= static_cast<double>(remainingCapacity))
                m_droppedParticles = UINT64_MAX;
            else
                m_droppedParticles += static_cast<std::uint64_t>(dropped);
            for (std::uint32_t index = 0; index < count; ++index)
                emitParticle(worldMatrix);
        };
        if (!burst.randomCount && burst.probability == 1.0f)
        {
            emitCount(occurrences * burst.count);
            return;
        }
        for (std::uint32_t event = 0; event < static_cast<std::uint32_t>(occurrences); ++event)
        {
            if (burst.probability < 1.0f && randomUnit() >= burst.probability)
                continue;
            const auto count = burst.randomCount
                ? burst.countMin + static_cast<std::uint32_t>((static_cast<std::uint64_t>(nextRandom())
                    * (burst.countMax - burst.countMin + 1)) >> 32)
                : burst.count;
            emitCount(count);
        }
    }

    void ParticleEffectComponent::emitParticle(const Matrix& worldMatrix, const Vector3& worldOffset)
    {
        if (m_particles.size() >= m_settings.maxParticles)
        {
            if (m_droppedParticles != UINT64_MAX)
                ++m_droppedParticles;
            return;
        }

        Vector3 spawnOffset;
        if (m_settings.shape == ParticleEmissionShape::Sphere)
        {
            const float azimuth = randomUnit() * (2.0f * PARTICLE_PI);
            const float sphereZ = randomUnit() * 2.0f - 1.0f;
            const float radius = m_settings.shapeRadius * std::cbrt(randomUnit());
            spawnOffset = randomUnitVector(azimuth, sphereZ) * radius;
        }
        else if (m_settings.shape == ParticleEmissionShape::Box)
        {
            spawnOffset = Vector3((randomUnit() * 2.0f - 1.0f) * m_settings.boxExtents.x,
                (randomUnit() * 2.0f - 1.0f) * m_settings.boxExtents.y,
                (randomUnit() * 2.0f - 1.0f) * m_settings.boxExtents.z);
        }
        else
        {
            const float azimuth = randomUnit() * (2.0f * PARTICLE_PI);
            const float radius = m_settings.shapeRadius * std::sqrt(randomUnit());
            spawnOffset = Vector3(std::cos(azimuth) * radius, 0.0f, std::sin(azimuth) * radius);
        }
        const Vector3 axis = m_settings.direction / std::sqrt(m_settings.direction.LengthSquared());
        const float coneCosine = std::cos(m_settings.spreadAngleDegrees * (PARTICLE_PI / 180.0f));
        const float coneZ = 1.0f - randomUnit() * (1.0f - coneCosine);
        const float coneAzimuth = randomUnit() * (2.0f * PARTICLE_PI);
        const float coneRadial = std::sqrt(std::max(0.0f, 1.0f - coneZ * coneZ));
        const Vector3 helper = std::abs(axis.y) < 0.999f ? Vector3::UnitY : Vector3::UnitX;
        Vector3 tangent = axis.Cross(helper);
        tangent.Normalize();
        const Vector3 bitangent = axis.Cross(tangent);
        Vector3 velocityDirection = axis * coneZ
            + tangent * (coneRadial * std::cos(coneAzimuth))
            + bitangent * (coneRadial * std::sin(coneAzimuth));
        velocityDirection.Normalize();

        Vector3 position = spawnOffset;
        if (!m_settings.localSpace)
        {
            position = Vector3::Transform(position, worldMatrix) + worldOffset;
            velocityDirection = Vector3::TransformNormal(velocityDirection, worldMatrix);
            const float directionLengthSquared = velocityDirection.LengthSquared();
            if (!std::isfinite(directionLengthSquared) || directionLengthSquared < 1e-8f)
            {
                LOG_ERROR("[ParticleEffect] Cannot transform the particle emission direction.");
                return;
            }
            velocityDirection /= std::sqrt(directionLengthSquared);
        }
        else if (worldOffset != Vector3::Zero)
            position += Vector3::TransformNormal(worldOffset, m_motionInverse);

        const Vector3 velocity = velocityDirection * std::lerp(m_settings.speedMin, m_settings.speedMax, randomUnit())
            + m_emitterVelocity * m_settings.inheritVelocity;
        if (!isFinite(position) || !isFinite(velocity))
        {
            LOG_ERROR("[ParticleEffect] Particle birth position or velocity is invalid.");
            return;
        }
        m_particles.push_back({
            .position = position,
            .velocity = velocity,
            .startColor = m_settings.startColor,
            .endColor = m_settings.endColor,
            .age = 0.0f,
            .lifetime = std::lerp(m_settings.lifetimeMin, m_settings.lifetimeMax, randomUnit()),
            .startSize = m_settings.startSize,
            .endSize = m_settings.endSize,
            .rotation = std::lerp(m_settings.initialRotationMin, m_settings.initialRotationMax, randomUnit()),
            .angularVelocity = 0.0f,
            .curves = m_lifetimeCurves,
            .sizeScale = m_settings.initialSizeScale,
        });
        Particle& particle = m_particles.back();
        if (m_settings.flipbookRandomStartFrame)
        {
            const std::uint32_t frameCount = m_settings.flipbookPlayback == ParticleFlipbookPlayback::Custom
                ? static_cast<std::uint32_t>(m_settings.flipbookFrames.size())
                : m_settings.flipbookColumns * m_settings.flipbookRows;
            particle.flipbookStartFrame = std::min(static_cast<std::uint32_t>(randomUnit() * frameCount), frameCount - 1);
        }
        if (!m_curveVariants.empty())
        {
            const auto selected = static_cast<std::size_t>(randomUnit()
                * static_cast<float>(m_curveVariants.size() + 1));
            if (selected > 0)
                particle.curves = m_curveVariants[selected - 1];
        }
        if (m_settings.randomInitial)
        {
            particle.startSize = std::lerp(m_settings.initialSizeMin, m_settings.initialSizeMax, randomUnit());
            const auto& low = m_settings.initialColorMin;
            const auto& high = m_settings.initialColorMax;
            const float red = std::lerp(low.x, high.x, randomUnit());
            const float green = std::lerp(low.y, high.y, randomUnit());
            const float blue = std::lerp(low.z, high.z, randomUnit());
            const float alpha = std::lerp(low.w, high.w, randomUnit());
            particle.startColor = Vector4(red, green, blue, alpha);
        }
        particle.angularVelocity = m_settings.angularVelocityMin == m_settings.angularVelocityMax
            ? m_settings.angularVelocityMin
            : std::lerp(m_settings.angularVelocityMin, m_settings.angularVelocityMax, randomUnit());
        if (m_settings.trailEnabled)
        {
            std::uint32_t index = NO_TRAIL;
            if (!m_freeTrails.empty())
            {
                index = m_freeTrails.back();
                m_freeTrails.pop_back();
            }
            else if (m_trails.size() < m_settings.maxParticles)
            {
                index = static_cast<std::uint32_t>(m_trails.size());
                m_trails.emplace_back();
            }
            if (index == NO_TRAIL)
            {
                if (m_droppedTrails != UINT64_MAX)
                    ++m_droppedTrails;
                if (!m_trailBudgetReported)
                    LOG_WARNING("[ParticleEffect] Trail capacity is exhausted; newly emitted particles have no trail.");
                m_trailBudgetReported = true;
            }
            else
            {
                Trail& trail = m_trails[index];
                trail = {};
                trail.active = true;
                trail.attached = true;
                trail.lifetime = m_settings.trailLifetime;
                trail.width = m_settings.trailWidth;
                trail.minDistance = m_settings.trailMinDistance;
                trail.blendMode = m_settings.blendMode;
                ++m_activeTrails;
                m_particles.back().trailIndex = index;
                updateTrail(m_particles.back(), worldMatrix, false);
            }
        }
    }

    void ParticleEffectComponent::advancePrewarm(const int maxSteps)
    {
        for (int step = 0; step < maxSteps && m_prewarmRemaining > 0.0; ++step)
        {
            const float delta = static_cast<float>(std::min(m_prewarmRemaining,
                static_cast<double>(SIMULATION_STEP)));
            simulate(delta, m_prewarmWorld);
            m_prewarmRemaining -= delta;
        }
        if (m_prewarmRemaining <= 0.0)
        {
            m_prewarmRemaining = 0.0;
            m_delayRemaining = m_settings.startDelay;
        }
    }

    bool ParticleEffectComponent::sampleEmitterMotion(const float elapsed, const Matrix& worldMatrix)
    {
        m_motionValid = false;
        m_emitterVelocity = Vector3::Zero;
        if (m_settings.distancePerParticle == 0.0f && m_settings.inheritVelocity == 0.0f)
            return false;
        const Vector3 position = worldMatrix.Translation();
        if (!isFinite(position) || !std::isfinite(elapsed) || elapsed <= 0.0f)
        {
            m_hasEmitterPosition = false;
            if (!isFinite(position) || !std::isfinite(elapsed) || elapsed < 0.0f)
                LOG_ERROR("[ParticleEffect] Cannot sample invalid emitter motion.");
            return false;
        }
        if (!m_hasEmitterPosition)
        {
            m_previousEmitterPosition = position;
            m_hasEmitterPosition = true;
            return false;
        }
        m_motionStart = m_previousEmitterPosition;
        m_motionEnd = position;
        m_previousEmitterPosition = position;
        const Vector3 velocity = (m_motionEnd - m_motionStart) / elapsed;
        if (!isFinite(velocity))
        {
            LOG_ERROR("[ParticleEffect] Emitter velocity is not finite.");
            m_hasEmitterPosition = false;
            return false;
        }
        if (m_settings.localSpace)
        {
            const float determinant = worldMatrix.Determinant();
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
            {
                LOG_ERROR("[ParticleEffect] Local emitter motion requires an invertible transform.");
                m_hasEmitterPosition = false;
                return false;
            }
            m_motionInverse = worldMatrix.Invert();
            m_emitterVelocity = Vector3::TransformNormal(velocity, m_motionInverse);
        }
        else
            m_emitterVelocity = velocity;
        if (!isFinite(m_emitterVelocity * m_settings.inheritVelocity))
        {
            LOG_ERROR("[ParticleEffect] Inherited particle velocity is not finite.");
            m_emitterVelocity = Vector3::Zero;
            m_hasEmitterPosition = false;
            return false;
        }
        m_motionValid = true;
        return true;
    }

    void ParticleEffectComponent::emitByDistance(const float simulationDelta, const double initialDelay,
        const double initialElapsed, const bool wasEmitting, const Matrix& worldMatrix)
    {
        if (!m_motionValid || !wasEmitting || m_settings.distancePerParticle <= 0.0f || simulationDelta <= 0.0f)
            return;
        const double start = std::min(initialDelay / simulationDelta, 1.0);
        const double availableTime = std::max(0.0, static_cast<double>(simulationDelta) - initialDelay);
        const double activeTime = m_settings.looping ? availableTime
            : std::min(availableTime, std::max(0.0, static_cast<double>(m_settings.duration) - initialElapsed));
        if (activeTime <= 0.0)
            return;
        const double end = std::min(1.0, start + activeTime / simulationDelta);
        const Vector3 movement = m_motionEnd - m_motionStart;
        const Vector3 segmentStart = m_motionStart + movement * static_cast<float>(start);
        const Vector3 segment = movement * static_cast<float>(end - start);
        const double distance = segment.Length();
        if (!std::isfinite(distance))
        {
            LOG_ERROR("[ParticleEffect] Emitter travel distance is not finite.");
            return;
        }
        if (distance <= 0.0)
            return;
        const double spacing = m_settings.distancePerParticle;
        const double total = m_distanceRemainder + distance;
        const double requested = std::floor(total / spacing);
        const double first = spacing - m_distanceRemainder;
        m_distanceRemainder = std::fmod(total, spacing);
        const std::size_t available = m_settings.maxParticles - m_particles.size();
        const auto count = static_cast<std::uint32_t>(std::min(static_cast<double>(available), requested));
        const double dropped = requested - count;
        if (dropped >= static_cast<double>(UINT64_MAX - m_droppedParticles))
            m_droppedParticles = UINT64_MAX;
        else
            m_droppedParticles += static_cast<std::uint64_t>(dropped);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const float fraction = static_cast<float>(std::min(1.0, (first + spacing * index) / distance));
            const Vector3 position = segmentStart + segment * fraction;
            emitParticle(worldMatrix, position - m_motionEnd);
        }
    }

    void ParticleEffectComponent::simulate(const float deltaTime, const Matrix& worldMatrix)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            LOG_ERROR("[ParticleEffect] Simulation delta must be finite and non-negative.");
            return;
        }
        if (deltaTime == 0.0f)
            return;
        if (deltaTime > MAX_SIMULATION_DELTA)
        {
            if (!m_deltaCappedReported)
                LOG_WARNING("[ParticleEffect] Simulation delta was capped at {} seconds.", MAX_SIMULATION_DELTA);
            m_deltaCappedReported = true;
        }
        else
            m_deltaCappedReported = false;

        float remaining = std::min(deltaTime, MAX_SIMULATION_DELTA);
        if (m_delayRemaining > 0.0)
        {
            const double delay = std::min(m_delayRemaining, static_cast<double>(remaining));
            m_delayRemaining -= delay;
            remaining -= static_cast<float>(delay);
        }
        while (remaining > 0.0f)
        {
            const float step = std::min(remaining, SIMULATION_STEP);
            for (std::size_t index = 0; index < m_trails.size(); ++index)
            {
                Trail& trail = m_trails[index];
                if (!trail.active)
                    continue;
                for (std::uint32_t point = 0; point < trail.count; ++point)
                    trail.points[(trail.first + point) % TRAIL_POINT_CAPACITY].age += step;
                while (trail.count > 0 && trail.points[trail.first].age >= trail.lifetime)
                {
                    trail.first = (trail.first + 1) % TRAIL_POINT_CAPACITY;
                    --trail.count;
                }
                if (!trail.attached && trail.count == 0)
                {
                    trail.active = false;
                    --m_activeTrails;
                    m_freeTrails.push_back(static_cast<std::uint32_t>(index));
                }
            }
            for (Particle& particle : m_particles)
            {
                const float age = std::clamp((particle.age + step * 0.5f) / particle.lifetime, 0.0f, 1.0f);
                const auto* curves = particle.curves.get();
                const float gravityScale = curves && !curves->gravity.empty()
                    ? evaluateCurve(curves->gravity, age, curves->smooth) : 1.0f;
                const float dragScale = curves && !curves->drag.empty()
                    ? evaluateCurve(curves->drag, age, curves->smooth) : 1.0f;
                const Vector3 acceleration = curves && !curves->acceleration.empty()
                    ? evaluateCurve(curves->acceleration, age, curves->smooth) : Vector3::Zero;
                const Vector3 velocityOffset = curves && !curves->velocity.empty()
                    ? evaluateCurve(curves->velocity, age, curves->smooth) : Vector3::Zero;
                particle.velocity += (m_settings.gravity * gravityScale + acceleration) * step;
                particle.velocity *= std::exp(-m_settings.drag * dragScale * step);
                const float speedMultiplier = particle.curves && !particle.curves->speed.empty()
                    ? evaluateCurve(particle.curves->speed, age, particle.curves->smooth) : 1.0f;
                particle.position += (particle.velocity + velocityOffset) * (step * speedMultiplier);
                const float angularMultiplier = particle.curves && !particle.curves->angularSpeed.empty()
                    ? evaluateCurve(particle.curves->angularSpeed, age, particle.curves->smooth) : 1.0f;
                particle.rotation += particle.angularVelocity * angularMultiplier * step;
                particle.age += step;
                updateTrail(particle, worldMatrix, particle.age >= particle.lifetime);
            }
            std::erase_if(m_particles, [](const Particle& particle)
            {
                return particle.age >= particle.lifetime;
            });

            if (m_burstPending)
            {
                for (std::uint32_t index = 0; index < m_settings.burstCount; ++index)
                    emitParticle(worldMatrix);
                for (const ParticleBurst& burst : m_settings.bursts)
                    if (burst.time == 0.0f)
                        emitBurst(burst, 1.0, worldMatrix);
                m_burstPending = false;
            }
            if (m_emitting)
            {
                const double duration = m_settings.duration;
                if (m_settings.looping && duration > 0.0)
                    m_emissionElapsed = std::fmod(m_emissionElapsed, duration);
                const double activeStep = m_settings.looping ? static_cast<double>(step)
                    : std::min(static_cast<double>(step), std::max(0.0, duration - m_emissionElapsed));
                const double end = m_emissionElapsed + activeStep;
                for (const ParticleBurst& burst : m_settings.bursts)
                {
                    for (std::uint32_t repeat = 0; repeat < burst.repeatCount; ++repeat)
                    {
                        const double eventTime = std::min(duration, static_cast<double>(burst.time)
                            + static_cast<double>(repeat) * burst.repeatInterval);
                        double occurrences = 0.0;
                        if (m_settings.looping && duration > 0.0)
                            occurrences = std::floor((end - eventTime) / duration)
                                - std::floor((m_emissionElapsed - eventTime) / duration);
                        else if (eventTime > m_emissionElapsed && eventTime <= end)
                            occurrences = 1.0;
                        emitBurst(burst, std::max(0.0, occurrences), worldMatrix);
                    }
                }
                const double emitted = integrateEmissionRate(
                    m_settings, m_emissionElapsed, duration, activeStep);
                double remainder = m_emissionRemainder + emitted;
                while (remainder >= 1.0)
                {
                    emitParticle(worldMatrix);
                    remainder -= 1.0;
                }
                m_emissionRemainder = remainder;
                m_emissionElapsed = end;
                if (!m_settings.looping && m_emissionElapsed >= duration)
                    m_emitting = false;
            }
            remaining -= step;
        }
    }

    bool ParticleEffectComponent::buildRenderSnapshot(const Matrix& worldMatrix)
    {
        m_renderParticles.clear();
        m_renderTrails.clear();
        if (m_particles.empty() && m_activeTrails == 0)
            return true;

        const Vector3 scaleX(worldMatrix._11, worldMatrix._12, worldMatrix._13);
        const Vector3 scaleY(worldMatrix._21, worldMatrix._22, worldMatrix._23);
        const Vector3 scaleZ(worldMatrix._31, worldMatrix._32, worldMatrix._33);
        const float worldScale = std::max({ scaleX.Length(), scaleY.Length(), scaleZ.Length() });
        if (!std::isfinite(worldScale))
        {
            LOG_ERROR("[ParticleEffect] GameObject world scale is invalid.");
            return false;
        }

        for (const Particle& particle : m_particles)
        {
            const float normalizedAge = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
            const Vector4 color = particleColor(particle);
            const float size = (particle.curves && !particle.curves->size.empty()
                ? evaluateCurve(particle.curves->size, normalizedAge, particle.curves->smooth)
                : std::lerp(particle.startSize, particle.endSize, normalizedAge))
                * (m_settings.localSpace ? worldScale : 1.0f);
            const Vector3 position = m_settings.localSpace
                ? Vector3::Transform(particle.position, worldMatrix) : particle.position;
            const Vector3 velocityOffset = particle.curves && !particle.curves->velocity.empty()
                ? evaluateCurve(particle.curves->velocity, normalizedAge, particle.curves->smooth) : Vector3::Zero;
            const float speedMultiplier = particle.curves && !particle.curves->speed.empty()
                ? evaluateCurve(particle.curves->speed, normalizedAge, particle.curves->smooth) : 1.0f;
            Vector3 velocity = (particle.velocity + velocityOffset) * speedMultiplier;
            if (m_settings.localSpace)
                velocity = Vector3::TransformNormal(velocity, worldMatrix);
            const float widthMultiplier = particle.curves && !particle.curves->width.empty()
                ? evaluateCurve(particle.curves->width, normalizedAge, particle.curves->smooth) : 1.0f;
            const float heightMultiplier = particle.curves && !particle.curves->height.empty()
                ? evaluateCurve(particle.curves->height, normalizedAge, particle.curves->smooth) : 1.0f;
            const Vector2 sizeScale(particle.sizeScale.x * widthMultiplier, particle.sizeScale.y * heightMultiplier);
            if (!isFinite(position) || !isFinite(velocity) || !isFinite(color) || !std::isfinite(size)
                || !std::isfinite(sizeScale.x) || !std::isfinite(sizeScale.y))
            {
                LOG_ERROR("[ParticleEffect] Simulation produced invalid render data.");
                return false;
            }
            if (particle.trailIndex != NO_TRAIL)
                m_trails[particle.trailIndex].tip = { position, color, 0.0f };
            if (size > 0.0f && sizeScale.x > 0.0f && sizeScale.y > 0.0f && color.w > 0.0f)
            {
                const FlipbookSample sample = sampleFlipbook(
                    m_settings, particle.age, particle.lifetime, particle.flipbookStartFrame);
                const float width = 1.0f / static_cast<float>(m_settings.flipbookColumns);
                const float height = 1.0f / static_cast<float>(m_settings.flipbookRows);
                const auto uvBounds = [&](const std::uint32_t frame)
                {
                    return Vector2(static_cast<float>(frame % m_settings.flipbookColumns) * width,
                        static_cast<float>(frame / m_settings.flipbookColumns) * height);
                };
                const Vector2 uvMin = uvBounds(sample.current);
                const Vector2 nextUvMin = uvBounds(sample.next);
                const Vector2 uvMax(std::min(1.0f, uvMin.x + width), std::min(1.0f, uvMin.y + height));
                const Vector2 nextUvMax(std::min(1.0f, nextUvMin.x + width),
                    std::min(1.0f, nextUvMin.y + height));
                const Vector2 uvOffset(
                    static_cast<float>(std::fmod(static_cast<double>(particle.age) * m_settings.uvScroll.x, 1.0)),
                    static_cast<float>(std::fmod(static_cast<double>(particle.age) * m_settings.uvScroll.y, 1.0)));
                m_renderParticles.push_back({ position, color, size, particle.rotation,
                    uvMin, uvMax, m_renderVisual, sizeScale, velocity, uvOffset, nextUvMin, nextUvMax, sample.blend });
            }
        }
        for (const Trail& trail : m_trails)
        {
            if (!trail.active)
                continue;
            std::array<TrailPoint, TRAIL_POINT_CAPACITY + 1> points;
            std::size_t count = trail.count;
            for (std::size_t index = 0; index < count; ++index)
                points[index] = trail.points[(trail.first + index) % TRAIL_POINT_CAPACITY];
            if (trail.attached)
            {
                if (count != 0 && (trail.tip.position - points[count - 1].position).LengthSquared() <= 1e-10f)
                    points[count - 1] = trail.tip;
                else
                    points[count++] = trail.tip;
            }
            for (std::size_t index = 0; index + 1 < count; ++index)
            {
                const TrailPoint& start = points[index];
                const TrailPoint& end = points[index + 1];
                const float lengthSquared = (end.position - start.position).LengthSquared();
                if (!std::isfinite(lengthSquared))
                {
                    LOG_ERROR("[ParticleEffect] Trail segment has invalid positions.");
                    return false;
                }
                if (lengthSquared <= 1e-10f)
                    continue;
                const float startFade = std::clamp(1.0f - start.age / trail.lifetime, 0.0f, 1.0f);
                const float endFade = std::clamp(1.0f - end.age / trail.lifetime, 0.0f, 1.0f);
                Vector4 startColor = start.color;
                Vector4 endColor = end.color;
                startColor.w *= startFade;
                endColor.w *= endFade;
                if (startColor.w <= 0.0f && endColor.w <= 0.0f)
                    continue;
                m_renderTrails.push_back({
                    index == 0 ? start.position : points[index - 1].position,
                    start.position, end.position,
                    index + 2 < count ? points[index + 2].position : end.position,
                    startColor, endColor, trail.width * startFade, trail.width * endFade, trail.blendMode,
                });
            }
        }
        return true;
    }
}
