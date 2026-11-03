#include "Pch.h"
#include "Editor\PostProcess\PostProcessInspector.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\Component\PostProcessVolumeComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Containers\String.h"
#include "Graphics\Texture\TextureManager.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Engine
{
    namespace
    {
        bool drawSettings(PostProcessSettings& settings)
        {
            bool changed = false;
            const auto drawSwitch = [&changed, &settings](const std::size_t index)
            {
                const auto& descriptor = POST_PROCESS_SWITCHES[index];
                auto& parameter = settings.*descriptor.member;
                ImGui::PushID(descriptor.name);
                changed |= ImGui::Checkbox("##Override", &parameter.overrideState);
                ImGui::SameLine();
                ImGui::BeginDisabled(!parameter.overrideState);
                changed |= ImGui::Checkbox(descriptor.name, &parameter.value);
                ImGui::EndDisabled();
                ImGui::PopID();
            };
            const auto drawFloats = [&changed, &settings](const std::size_t first, const std::size_t end)
            {
                for (std::size_t index = first; index < end; ++index)
                {
                    const auto& descriptor = POST_PROCESS_FLOATS[index];
                    auto& parameter = settings.*descriptor.member;
                    ImGui::PushID(descriptor.name);
                    changed |= ImGui::Checkbox("##Override", &parameter.overrideState);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!parameter.overrideState);
                    changed |= ImGui::SliderFloat(descriptor.name, &parameter.value,
                        descriptor.minimum, descriptor.maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
            };
            const auto drawColor = [&changed, &settings](const std::size_t index)
            {
                const auto& descriptor = POST_PROCESS_COLORS[index];
                auto& parameter = settings.*descriptor.member;
                ImGui::PushID(descriptor.name);
                changed |= ImGui::Checkbox("##Override", &parameter.overrideState);
                ImGui::SameLine();
                ImGui::BeginDisabled(!parameter.overrideState);
                changed |= ImGui::SliderFloat3(descriptor.name, &parameter.value.x,
                    descriptor.minimum, descriptor.maximum,
                    "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();
                ImGui::PopID();
            };
            ImGui::TextDisabled("Left checkbox: override. RGB values are linear.");
            if (ImGui::TreeNode("Bloom"))
            {
                drawSwitch(0); drawFloats(0, 4);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Exposure"))
            {
                drawSwitch(1); drawFloats(4, 5);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Color Grading"))
            {
                drawSwitch(2); drawFloats(11, 16); drawColor(0); drawColor(1);
                if (ImGui::TreeNode("Shadows"))
                {
                    drawFloats(16, 20); drawColor(2);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Midtones"))
                {
                    drawFloats(20, 24); drawColor(3);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Highlights"))
                {
                    drawFloats(24, 28); drawColor(4);
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Vignette"))
            {
                drawSwitch(3); drawFloats(28, 31); drawColor(5);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Chromatic Aberration"))
            {
                drawSwitch(4); drawFloats(31, 33);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Film Grain"))
            {
                drawSwitch(5); drawFloats(33, 35);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("FXAA"))
            {
                drawSwitch(6); drawFloats(35, 38);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Temporal Anti-Aliasing"))
            {
                drawSwitch(7); drawFloats(42, 43);
                ImGui::TextDisabled("Jittered history reconstruction takes precedence over FXAA.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Motion Blur"))
            {
                drawSwitch(8); drawFloats(43, 45);
                ImGui::TextDisabled("Requires the deferred velocity buffer.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Depth of Field"))
            {
                drawSwitch(9); drawFloats(38, 41);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Color Grading LUT"))
            {
                drawSwitch(10); drawFloats(41, 42);
                auto& lut = settings.colorGradingLutAsset;
                changed |= ImGui::Checkbox("Override LUT Asset", &lut.overrideState);
                ImGui::BeginDisabled(!lut.overrideState);
                const auto path = lut.value.isValid()
                    ? TextureManager::instance().getAssetPath(AssetGUID{ lut.value.high, lut.value.low })
                    : std::filesystem::path{};
                const auto pathText = String::fromWide(path.c_str());
                ImGui::TextWrapped("%s", path.empty() ? "No LUT asset assigned." : pathText.c_str());
                if (ImGui::Button("Select LUT Asset..."))
                {
                    constexpr std::array filters{ FileDialogFilter{ L"Linear LUT Texture", L"*.dds;*.png;*.tga" } };
                    std::vector<std::filesystem::path> paths;
                    const auto owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
                    const auto result = Dialog::openFile(paths, L"Select Color Grading LUT", L"Assets", filters, false, owner);
                    if (result == DialogResult::Ok && !paths.empty())
                    {
                        const AssetGUID guid = TextureManager::instance().registerAssetPath(paths.front());
                        if (guid.isValid())
                        {
                            lut.value = { guid.high, guid.low };
                            lut.overrideState = true;
                            changed = true;
                        }
                        else
                            LOG_ERROR("[PostProcessInspector] Failed to register the selected LUT texture.");
                    }
                    else if (result != DialogResult::Cancel)
                        LOG_ERROR("[PostProcessInspector] LUT asset dialog failed.");
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("Use a linear 16-64 size strip: width = height squared.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Automatic Exposure"))
            {
                drawSwitch(11); drawFloats(5, 11);
                ImGui::TextDisabled("Separate brighten/darken rates adapt per Camera over time.");
                ImGui::TreePop();
            }
            if (ImGui::Button("Reset Overrides"))
            {
                settings = {};
                changed = true;
            }
            return changed;
        }

        template<class T>
        T* findProfileOwner(const ObjectGUID guid)
        {
            auto& scenes = SceneManager::instance();
            GameObject* object = nullptr;
            if (auto* const scene = scenes.getActiveScene())
                object = scene->find(guid);
            if (!object)
                object = scenes.getPersistentScene()->find(guid);
            return object ? object->getComponent<T>() : nullptr;
        }

        template<class T>
        void requestOpenProfile(const T& camera)
        {
            const auto* const object = camera.getGameObject();
            if (!object)
                return;
            const auto guid = object->getGUID();
            const auto owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([guid, owner]
            {
                constexpr std::array filters{ FileDialogFilter{ L"PostEffect Profile", L"*.postprofile" } };
                std::vector<std::filesystem::path> paths;
                const auto result = Dialog::openFile(paths, L"Open PostEffect Profile", L"Assets\\PostProcess", filters, false, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok || paths.empty())
                {
                    LOG_ERROR("[PostProcessInspector] Profile open dialog failed.");
                    return;
                }
                auto* const target = findProfileOwner<T>(guid);
                if (!target)
                    return;
                const auto handle = PostProcessProfileManager::instance().load(paths.front());
                if (handle.isValid())
                    target->setPostProcessProfile(handle);
            });
        }

        void requestSaveProfile(const PostProcessProfileHandle handle)
        {
            const auto owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([handle, owner]
            {
                constexpr std::array filters{ FileDialogFilter{ L"PostEffect Profile", L"*.postprofile" } };
                std::filesystem::path path;
                const auto result = Dialog::saveFile(path, L"Save PostEffect Profile", L"Assets\\PostProcess",
                    L"postprofile", filters, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok)
                {
                    LOG_ERROR("[PostProcessInspector] Profile save dialog failed.");
                    return;
                }
                PostProcessProfileManager::instance().save(handle, path);
            });
        }

        constexpr std::size_t MAX_EFFECT_PARAMETERS = std::tuple_size_v<decltype(PostProcessEffectInstance::parameters)>;
        constexpr std::array EFFECT_ASSET_FILTERS{ FileDialogFilter{ L"PostEffect Asset", L"*.posteffect" } };
        constexpr std::array EFFECT_SOURCE_FILTERS{ FileDialogFilter{ L"HLSL Source", L"*.hlsl" } };
        constexpr ImVec4 ERROR_TEXT_COLOR{ 1.0f, 0.4f, 0.4f, 1.0f };

        /** @brief Uncompiled asset metadata edits; compiling on every keystroke would stall the Editor. */
        struct EffectDraft
        {
            std::shared_ptr<const PostProcessEffectAsset> base; //!< Published snapshot the draft was copied from.
            PostProcessEffectAsset asset; //!< Pending metadata edits.
            bool modified = false; //!< Whether the draft has pending user edits.
            bool failed = false; //!< Whether the last Apply/Recompile/Reload was rejected.
        };

        // Editor UI and dispatched dialog tasks both run on the main thread.
        std::unordered_map<std::uint64_t, EffectDraft>& effectDrafts()
        {
            static std::unordered_map<std::uint64_t, EffectDraft> drafts;
            return drafts;
        }

        std::uint64_t effectKey(const PostProcessEffectHandle handle) noexcept
        {
            return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
        }

        PostProcessEffectHandle effectHandleFromKey(const std::uint64_t key) noexcept
        {
            return { static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key) };
        }

        HWND dialogOwner()
        {
            return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        }

        std::string toUtf8(const std::filesystem::path& path)
        {
            const auto text = String::fromWide(path.c_str());
            return text.size() == 0 ? std::string{} : std::string(text.c_str(), text.size());
        }

        bool inputString(const char* label, std::string& value)
        {
            std::vector<char> buffer(std::max<std::size_t>(256, value.size() + 1), '\0');
            std::copy(value.begin(), value.end(), buffer.begin());
            if (!ImGui::InputText(label, buffer.data(), buffer.size()))
                return false;
            value = buffer.data();
            return true;
        }

        const char* stageName(const PostProcessStage stage) noexcept
        {
            return stage == PostProcessStage::AfterToneMapping ? "After Tone Mapping (LDR)" : "Before Tone Mapping (HDR)";
        }

        std::string effectLabel(const PostProcessEffectHandle handle, const PostProcessEffectAsset& asset)
        {
            auto label = asset.name;
            if (PostProcessEffectAssetManager::instance().getPath(handle).empty())
                label += " (Unsaved)";
            return label;
        }

        // Values are slot-indexed, so only leading parameters whose names still match keep their authored values.
        std::size_t matchingParameterPrefix(const PostProcessEffectAsset& previous, const PostProcessEffectAsset& current) noexcept
        {
            std::size_t count = 0;
            while (count < previous.parameters.size() && count < current.parameters.size()
                && previous.parameters[count].name == current.parameters[count].name)
                ++count;
            return count;
        }

        void conformEffectInstance(PostProcessEffectInstance& instance, const PostProcessEffectAsset& asset,
            const std::size_t preservedCount)
        {
            const auto count = std::min(asset.parameters.size(), instance.parameters.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& definition = asset.parameters[index];
                auto& value = instance.parameters[index];
                if (index >= preservedCount || !std::isfinite(value))
                    value = definition.defaultValue;
                else
                    value = std::clamp(value, definition.minimum, definition.maximum);
            }
        }

        PostProcessEffectInstance makeEffectInstance(const PostProcessEffectHandle handle, const PostProcessEffectAsset& asset)
        {
            PostProcessEffectInstance instance;
            instance.effect = handle;
            conformEffectInstance(instance, asset, 0);
            return instance;
        }

        /** @brief Keeps every registered Profile valid after an effect's parameter metadata changes. */
        void conformProfilesToEffect(const PostProcessEffectHandle effect, const PostProcessEffectAsset& previous)
        {
            const auto asset = PostProcessEffectAssetManager::instance().get(effect);
            if (!asset)
                return;
            const auto preserved = matchingParameterPrefix(previous, *asset);
            auto& profiles = PostProcessProfileManager::instance();
            for (const auto handle : profiles.getAllHandles())
            {
                const auto profile = profiles.get(handle);
                if (!profile)
                    continue;
                auto conformed = *profile;
                for (auto& instance : conformed.customEffects)
                    if (instance.effect == effect)
                        conformEffectInstance(instance, *asset, preserved);
                if (conformed != *profile && !profiles.update(handle, std::move(conformed)))
                    LOG_ERROR("[PostProcessInspector] Could not conform Profile '{}' to effect '{}'.", profile->name, asset->name);
            }
        }

        bool applyEffectAsset(const PostProcessEffectHandle handle, PostProcessEffectAsset asset)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            const auto previous = effects.get(handle);
            if (!previous || !effects.update(handle, std::move(asset)))
                return false;
            conformProfilesToEffect(handle, *previous);
            return true;
        }

        bool reloadEffectAsset(const PostProcessEffectHandle handle)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            const auto previous = effects.get(handle);
            if (!previous || !effects.reload(handle))
                return false;
            conformProfilesToEffect(handle, *previous);
            return true;
        }

        void appendEffectToProfile(const PostProcessProfileHandle profileHandle, const PostProcessEffectHandle effect)
        {
            auto& profiles = PostProcessProfileManager::instance();
            const auto profile = profiles.get(profileHandle);
            const auto asset = PostProcessEffectAssetManager::instance().get(effect);
            if (!profile || !asset)
            {
                LOG_ERROR("[PostProcessInspector] Profile or effect was unloaded before the effect could be added.");
                return;
            }
            auto edited = *profile;
            edited.customEffects.push_back(makeEffectInstance(effect, *asset));
            profiles.update(profileHandle, std::move(edited));
        }

        /** @brief Shows Save As for an effect asset; must run from a dispatched main-thread task. */
        void saveEffectAs(const PostProcessEffectHandle effect, const HWND owner)
        {
            std::filesystem::path path;
            const auto result = Dialog::saveFile(path, L"Save PostEffect Asset", L"Assets\\PostProcess",
                L"posteffect", EFFECT_ASSET_FILTERS, owner);
            if (result == DialogResult::Cancel)
            {
                LOG_WARNING("[PostProcessInspector] Effect asset remains unsaved; save it before saving Profiles that use it.");
                return;
            }
            if (result != DialogResult::Ok)
            {
                LOG_ERROR("[PostProcessInspector] Effect save dialog failed.");
                return;
            }
            PostProcessEffectAssetManager::instance().save(effect, path);
        }

        void requestSaveEffectAs(const PostProcessEffectHandle effect)
        {
            const auto owner = dialogOwner();
            MainThreadDispatcher::instance().post([effect, owner]
            {
                saveEffectAs(effect, owner);
            });
        }

        bool openEffectSource(std::filesystem::path& source, const HWND owner)
        {
            std::vector<std::filesystem::path> paths;
            const auto result = Dialog::openFile(paths, L"Select PostEffect HLSL Source", L"Assets\\Shaders",
                EFFECT_SOURCE_FILTERS, false, owner);
            if (result == DialogResult::Cancel)
                return false;
            if (result != DialogResult::Ok || paths.empty())
            {
                LOG_ERROR("[PostProcessInspector] Effect source dialog failed.");
                return false;
            }
            source = std::move(paths.front());
            return true;
        }

        void requestCreateEffect(const PostProcessProfileHandle profileHandle)
        {
            const auto owner = dialogOwner();
            MainThreadDispatcher::instance().post([profileHandle, owner]
            {
                PostProcessEffectAsset asset;
                if (!openEffectSource(asset.sourcePath, owner))
                    return;
                if (auto name = toUtf8(asset.sourcePath.stem()); !name.empty())
                    asset.name = std::move(name);
                const auto effect = PostProcessEffectAssetManager::instance().create(std::move(asset));
                if (!effect.isValid())
                    return;
                saveEffectAs(effect, owner);
                appendEffectToProfile(profileHandle, effect);
            });
        }

        void requestOpenEffect(const PostProcessProfileHandle profileHandle)
        {
            const auto owner = dialogOwner();
            MainThreadDispatcher::instance().post([profileHandle, owner]
            {
                std::vector<std::filesystem::path> paths;
                const auto result = Dialog::openFile(paths, L"Open PostEffect Asset", L"Assets\\PostProcess",
                    EFFECT_ASSET_FILTERS, false, owner);
                if (result == DialogResult::Cancel)
                    return;
                if (result != DialogResult::Ok || paths.empty())
                {
                    LOG_ERROR("[PostProcessInspector] Effect open dialog failed.");
                    return;
                }
                const auto effect = PostProcessEffectAssetManager::instance().load(paths.front());
                if (effect.isValid())
                    appendEffectToProfile(profileHandle, effect);
            });
        }

        void requestChangeEffectSource(const PostProcessEffectHandle effect)
        {
            const auto owner = dialogOwner();
            MainThreadDispatcher::instance().post([effect, owner]
            {
                std::filesystem::path source;
                if (!openEffectSource(source, owner))
                    return;
                const auto current = PostProcessEffectAssetManager::instance().get(effect);
                if (!current)
                {
                    LOG_ERROR("[PostProcessInspector] Effect was unloaded before its source could be changed.");
                    return;
                }
                auto& draft = effectDrafts()[effectKey(effect)];
                if (!draft.modified)
                {
                    draft.base = current;
                    draft.asset = *current;
                }
                draft.asset.sourcePath = std::move(source);
                draft.modified = true;
                draft.failed = false;
            });
        }

        bool drawParameterDefinitions(std::vector<PostProcessEffectParameter>& parameters)
        {
            if (!ImGui::TreeNode("Parameter Definitions"))
                return false;
            ImGui::TextDisabled("Parameter i is float i of the b1 constant buffer (max %zu).", MAX_EFFECT_PARAMETERS);
            bool changed = false;
            std::optional<std::size_t> removed;
            for (std::size_t index = 0; index < parameters.size(); ++index)
            {
                auto& parameter = parameters[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::Text("[%zu]", index);
                ImGui::SameLine();
                changed |= inputString("Name", parameter.name);
                float range[2]{ parameter.minimum, parameter.maximum };
                if (ImGui::InputFloat2("Min / Max", range, "%.3f"))
                {
                    parameter.minimum = range[0];
                    parameter.maximum = range[1];
                    changed = true;
                }
                changed |= ImGui::InputFloat("Default", &parameter.defaultValue, 0, 0, "%.3f");
                if (parameter.minimum > parameter.maximum || parameter.defaultValue < parameter.minimum
                    || parameter.defaultValue > parameter.maximum)
                    ImGui::TextColored(ERROR_TEXT_COLOR, "Requires Min <= Default <= Max.");
                if (ImGui::SmallButton("Remove Parameter"))
                    removed = index;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (removed)
            {
                parameters.erase(parameters.begin() + static_cast<std::ptrdiff_t>(*removed));
                changed = true;
            }
            ImGui::BeginDisabled(parameters.size() >= MAX_EFFECT_PARAMETERS);
            if (ImGui::Button("Add Parameter"))
            {
                PostProcessEffectParameter parameter;
                for (std::size_t suffix = parameters.size(); ; ++suffix)
                {
                    parameter.name = "param" + std::to_string(suffix);
                    if (std::none_of(parameters.begin(), parameters.end(),
                        [&parameter](const PostProcessEffectParameter& other) { return other.name == parameter.name; }))
                        break;
                }
                parameters.push_back(std::move(parameter));
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
            return changed;
        }

        void drawEffectAssetEditor(const PostProcessEffectHandle handle, const std::shared_ptr<const PostProcessEffectAsset>& current)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            auto& draft = effectDrafts()[effectKey(handle)];
            if (!draft.modified && draft.base != current)
            {
                draft.base = current;
                draft.asset = *current;
            }
            ImGui::TextDisabled("Asset edits affect every Profile using this effect.");
            const auto path = effects.getPath(handle);
            const auto pathText = toUtf8(path);
            ImGui::TextWrapped("Asset: %s", path.empty()
                ? "Unsaved - Save Effect As before saving Profiles that use it." : pathText.c_str());
            bool edited = inputString("Effect Name", draft.asset.name);
            ImGui::TextWrapped("Source: %s", toUtf8(draft.asset.sourcePath).c_str());
            if (ImGui::Button("Change Source..."))
                requestChangeEffectSource(handle);
            edited |= inputString("Entry Point", draft.asset.entryPoint);
            if (ImGui::BeginCombo("Stage", stageName(draft.asset.stage)))
            {
                for (const auto stage : { PostProcessStage::BeforeToneMapping, PostProcessStage::AfterToneMapping })
                    if (ImGui::Selectable(stageName(stage), draft.asset.stage == stage))
                    {
                        draft.asset.stage = stage;
                        edited = true;
                    }
                ImGui::EndCombo();
            }
            int divisor = static_cast<int>(draft.asset.resolutionDivisor);
            if (ImGui::SliderInt("Resolution Divisor", &divisor, 1, 16, "1/%d", ImGuiSliderFlags_AlwaysClamp))
            {
                draft.asset.resolutionDivisor = static_cast<std::uint32_t>(divisor);
                edited = true;
            }
            edited |= drawParameterDefinitions(draft.asset.parameters);
            if (edited)
            {
                draft.modified = true;
                draft.failed = false;
            }
            if (draft.modified)
                ImGui::TextDisabled("Pending asset edits - Apply compiles them before publishing.");
            if (draft.failed)
                ImGui::TextColored(ERROR_TEXT_COLOR, "Compile/validation failed; the previous shader is retained. See the log.");
            ImGui::BeginDisabled(!draft.modified);
            if (ImGui::Button("Apply & Compile"))
            {
                draft.failed = !applyEffectAsset(handle, draft.asset);
                draft.modified = draft.failed;
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert Edits"))
            {
                draft.base = nullptr;
                draft.modified = false;
                draft.failed = false;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Recompile Source"))
                draft.failed = !applyEffectAsset(handle, *current);
            ImGui::BeginDisabled(draft.modified);
            ImGui::BeginDisabled(path.empty());
            if (ImGui::Button("Save Effect"))
                effects.save(handle);
            ImGui::SameLine();
            if (ImGui::Button("Reload Effect (discard edits)"))
                draft.failed = !reloadEffectAsset(handle);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Save Effect As..."))
                requestSaveEffectAs(handle);
            ImGui::EndDisabled();
            if (draft.modified)
                ImGui::TextDisabled("Apply or Revert pending edits before saving or reloading.");
        }

        bool drawEffectSelector(PostProcessEffectInstance& instance)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            const auto current = effects.get(instance.effect);
            const auto preview = current ? effectLabel(instance.effect, *current) : std::string("<Missing>");
            bool changed = false;
            if (ImGui::BeginCombo("Effect", preview.c_str()))
            {
                for (const auto candidate : effects.getAllHandles())
                {
                    const auto asset = effects.get(candidate);
                    if (!asset)
                        continue;
                    ImGui::PushID(static_cast<int>(candidate.index));
                    if (ImGui::Selectable(effectLabel(candidate, *asset).c_str(), candidate == instance.effect)
                        && candidate != instance.effect)
                    {
                        const bool enabled = instance.enabled;
                        instance = makeEffectInstance(candidate, *asset);
                        instance.enabled = enabled;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        using EffectBaseline = std::vector<std::pair<PostProcessEffectHandle, std::shared_ptr<const PostProcessEffectAsset>>>;

        /** @brief Records the effect snapshots a Profile copy was authored against before this frame's asset edits. */
        EffectBaseline captureEffectBaseline(const PostProcessProfile& profile)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            EffectBaseline baseline;
            baseline.reserve(profile.customEffects.size());
            for (const auto& instance : profile.customEffects)
                if (std::none_of(baseline.begin(), baseline.end(), [&instance](const auto& entry) { return entry.first == instance.effect; }))
                    baseline.emplace_back(instance.effect, effects.get(instance.effect));
            return baseline;
        }

        /**
         * @brief Rebases a Profile candidate onto the latest effect definitions before it is committed.
         * @details Effects republished during this frame keep only name-matching leading values; others are clamped.
         */
        void rebaseProfileEffects(PostProcessProfile& profile, const EffectBaseline& baseline)
        {
            auto& effects = PostProcessEffectAssetManager::instance();
            for (auto& instance : profile.customEffects)
            {
                const auto latest = effects.get(instance.effect);
                if (!latest)
                    continue;
                auto preserved = latest->parameters.size();
                const auto found = std::find_if(baseline.begin(), baseline.end(),
                    [&instance](const auto& entry) { return entry.first == instance.effect; });
                if (found != baseline.end() && found->second && found->second != latest)
                    preserved = matchingParameterPrefix(*found->second, *latest);
                conformEffectInstance(instance, *latest, preserved);
            }
        }

        /** @brief Draws the ordered custom effect list of a Profile copy; returns whether the copy changed. */
        bool drawCustomEffects(std::vector<PostProcessEffectInstance>& instances, const PostProcessProfileHandle profileHandle)
        {
            if (!ImGui::TreeNode("Custom Effects"))
                return false;
            auto& effects = PostProcessEffectAssetManager::instance();
            std::erase_if(effectDrafts(), [&effects](const auto& entry)
            {
                return !effects.get(effectHandleFromKey(entry.first));
            });
            ImGui::TextDisabled("Custom passes run in list order within their stage.");
            enum class ListAction { None, MoveUp, MoveDown, Remove };
            auto action = ListAction::None;
            std::size_t target = 0;
            bool changed = false;
            bool hasMissing = false;
            for (std::size_t index = 0; index < instances.size(); ++index)
            {
                auto& instance = instances[index];
                ImGui::PushID(static_cast<int>(index));
                changed |= ImGui::Checkbox("##Enabled", &instance.enabled);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Enabled");
                ImGui::SameLine();
                ImGui::BeginDisabled(index == 0);
                if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
                {
                    action = ListAction::MoveUp;
                    target = index;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(index + 1 == instances.size());
                if (ImGui::ArrowButton("##Down", ImGuiDir_Down))
                {
                    action = ListAction::MoveDown;
                    target = index;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    action = ListAction::Remove;
                    target = index;
                }
                ImGui::SameLine();
                const auto asset = effects.get(instance.effect);
                if (!asset)
                {
                    hasMissing = true;
                    ImGui::TextColored(ERROR_TEXT_COLOR, "%zu: <Missing effect>", index);
                    ImGui::PopID();
                    continue;
                }
                const auto label = effectLabel(instance.effect, *asset);
                if (ImGui::TreeNode("##Instance", "%zu: %s", index, label.c_str()))
                {
                    changed |= drawEffectSelector(instance);
                    if (const auto selected = effects.get(instance.effect))
                    {
                        ImGui::TextDisabled("%s, 1/%u resolution", stageName(selected->stage), selected->resolutionDivisor);
                        const auto count = std::min(selected->parameters.size(), instance.parameters.size());
                        if (count == 0)
                            ImGui::TextDisabled("This effect declares no parameters.");
                        for (std::size_t parameterIndex = 0; parameterIndex < count; ++parameterIndex)
                        {
                            const auto& definition = selected->parameters[parameterIndex];
                            ImGui::PushID(static_cast<int>(parameterIndex));
                            changed |= ImGui::SliderFloat(definition.name.c_str(), &instance.parameters[parameterIndex],
                                definition.minimum, definition.maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                            ImGui::PopID();
                        }
                        if (count > 0 && ImGui::Button("Reset Parameters"))
                        {
                            conformEffectInstance(instance, *selected, 0);
                            changed = true;
                        }
                        if (ImGui::TreeNode("Effect Asset"))
                        {
                            drawEffectAssetEditor(instance.effect, selected);
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            switch (action)
            {
            case ListAction::MoveUp:
                std::swap(instances[target - 1], instances[target]);
                changed = true;
                break;
            case ListAction::MoveDown:
                std::swap(instances[target], instances[target + 1]);
                changed = true;
                break;
            case ListAction::Remove:
                instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(target));
                changed = true;
                break;
            case ListAction::None:
                break;
            }
            if (hasMissing)
            {
                ImGui::TextColored(ERROR_TEXT_COLOR, "Missing effects block Profile edits and saves until removed.");
                if (ImGui::Button("Remove Missing Effects"))
                {
                    std::erase_if(instances, [&effects](const PostProcessEffectInstance& instance)
                    {
                        return !effects.get(instance.effect);
                    });
                    changed = true;
                }
            }
            if (instances.empty())
                ImGui::TextDisabled("No custom effects.");
            if (ImGui::BeginCombo("Add Effect", "Select a registered effect..."))
            {
                for (const auto candidate : effects.getAllHandles())
                {
                    const auto asset = effects.get(candidate);
                    if (!asset)
                        continue;
                    ImGui::PushID(static_cast<int>(candidate.index));
                    if (ImGui::Selectable(effectLabel(candidate, *asset).c_str(), false))
                    {
                        instances.push_back(makeEffectInstance(candidate, *asset));
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("New Effect from HLSL..."))
                requestCreateEffect(profileHandle);
            ImGui::SameLine();
            if (ImGui::Button("Open Effect..."))
                requestOpenEffect(profileHandle);
            ImGui::TreePop();
            return changed;
        }    }

    template<class T>
    void drawSharedProfileInspector(T& camera)
    {
        auto& manager = PostProcessProfileManager::instance();
        auto handle = camera.getPostProcessProfile();
        const auto current = manager.get(handle);
        if (ImGui::BeginCombo("Shared Profile", current ? current->name.c_str() : (handle.isValid() ? "<Missing>" : "<None>")))
        {
            if (ImGui::Selectable("<None>", !handle.isValid()))
                camera.setPostProcessProfile({});
            for (const auto candidate : manager.getAllHandles())
            {
                const auto profile = manager.get(candidate);
                if (!profile)
                    continue;
                ImGui::PushID(static_cast<int>(candidate.index));
                if (ImGui::Selectable(profile->name.c_str(), candidate == handle))
                    camera.setPostProcessProfile(candidate);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("New Profile"))
        {
            const auto created = manager.create({});
            if (created.isValid())
                camera.setPostProcessProfile(created);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Profile..."))
            requestOpenProfile(camera);
        handle = camera.getPostProcessProfile();
        const auto profile = manager.get(handle);
        if (profile && ImGui::TreeNode("Edit Shared Profile"))
        {
            ImGui::TextDisabled("Edits affect all Cameras and Volumes using this Profile.");
            const auto path = manager.getPath(handle);
            const auto pathText = String::fromWide(path.c_str());
            ImGui::TextWrapped("%s", path.empty() ? "Unsaved - save this Profile before saving the Scene/Prefab." : pathText.c_str());
            if (manager.isDirty(handle))
                ImGui::TextDisabled("Unsaved Profile edits - Save Profile to persist them.");
            PostProcessProfile edited = *profile;
            const auto effectBaseline = captureEffectBaseline(*profile);
            std::vector<char> name(std::max<std::size_t>(256, edited.name.size() + 1), '\0');
            std::copy(edited.name.begin(), edited.name.end(), name.begin());
            bool changed = ImGui::InputText("Name", name.data(), name.size());
            if (changed)
                edited.name = name.data();
            changed |= drawSettings(edited.settings);
            changed |= drawCustomEffects(edited.customEffects, handle);
            if (changed)
            {
                rebaseProfileEffects(edited, effectBaseline);
                manager.update(handle, std::move(edited));
            }
            ImGui::BeginDisabled(path.empty());
            if (ImGui::Button("Save Profile"))
                manager.save(handle);
            ImGui::SameLine();
            if (ImGui::Button("Reload (discard edits)"))
                manager.reload(handle);
            ImGui::EndDisabled();
            if (ImGui::Button("Save Profile As..."))
                requestSaveProfile(handle);
            ImGui::SameLine();
            if (ImGui::Button("Make Unique"))
            {
                if (const auto latest = manager.get(handle))
                {
                    auto unique = *latest;
                    unique.guid = {};
                    unique.name += " (Copy)";
                    const auto created = manager.create(std::move(unique));
                    if (created.isValid())
                        camera.setPostProcessProfile(created);
                }
                else
                    LOG_ERROR("[PostProcessInspector] Profile was unloaded before duplication.");
            }
            ImGui::TreePop();
        }
    }

    void drawPostProcessInspector(CameraComponent& camera)
    {
        if (!ImGui::CollapsingHeader("Post Effects"))
            return;
        ImGui::PushID("PostEffects");
        ImGui::TextDisabled("Deferred: Bloom / Exposure / Color / Vignette / FXAA. UI is unaffected.");
        bool enabled = camera.isPostProcessEnabled();
        if (ImGui::Checkbox("Enable Post Effects", &enabled))
            camera.setPostProcessEnabled(enabled);
        float weight = camera.getPostProcessWeight();
        if (ImGui::SliderFloat("Weight", &weight, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp))
            camera.setPostProcessWeight(weight);
        // Keep the existing Profile controls before new controls for stable Inspector layout.
        drawSharedProfileInspector(camera);
        auto mask = camera.getPostProcessVolumeMask();
        if (ImGui::InputScalar("Volume Layer Mask", ImGuiDataType_U32, &mask, nullptr, nullptr, "%08X",
            ImGuiInputTextFlags_CharsHexadecimal))
            camera.setPostProcessVolumeMask(mask);
        ImGui::TextDisabled("Volumes -> Camera Profile -> Camera Overrides. Mask 0 disables Volumes only.");
        if (ImGui::TreeNode("Camera Overrides"))
        {
            ImGui::TextDisabled("Overrides affect this camera only; they are saved in its Scene/Prefab.");
            auto settings = camera.getPostProcessOverrides();
            if (drawSettings(settings))
                camera.setPostProcessOverrides(settings);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void drawPostProcessVolumeInspector(PostProcessVolumeComponent& volume)
    {
        ImGui::PushID("VolumeProfile");
        drawSharedProfileInspector(volume);
        ImGui::PopID();
    }
}
