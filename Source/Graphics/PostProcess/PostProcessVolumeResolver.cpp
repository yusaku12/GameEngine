#include "Pch.h"
#include "Graphics\PostProcess\PostProcessVolumeResolver.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Components\Rendering\PostProcessVolumeComponent.h"
#include "Core\Scene\Scene.h"
#include <tuple>
#include <cmath>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    bool resolvePostProcessVolumes(PostProcessCameraSnapshot& destination, const Vector3& position,
        const std::uint32_t layerMask, const std::span<const Scene* const> scenes,
        std::vector<PostProcessVolumeLayer>& scratch) noexcept
    {
        scratch.clear();
        if (layerMask == 0)
            return true;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            LOG_ERROR("[PostProcessVolume] Camera position must be finite; resolution failed.");
            return false;
        }
        try
        {
            for (std::size_t index = 0; index < scenes.size(); ++index)
            {
                const Scene* const scene = scenes[index];
                if (!scene || std::find(scenes.begin(), scenes.begin() + index, scene) != scenes.begin() + index)
                    continue;
                for (const auto& object : scene->getGameObjects())
                {
                    const auto* const volume = object->getComponent<PostProcessVolumeComponent>();
                    if (!volume || !volume->isEnabled() || !object->isActiveInHierarchy()
                        || object->getLayer() >= 32 || (layerMask & (1u << object->getLayer())) == 0)
                        continue;
                    bool destroying = false;
                    for (const auto* ancestor = object.get(); ancestor; ancestor = ancestor->getParent())
                        destroying |= ancestor->isDestroyRequested();
                    if (destroying)
                        continue;
                    const float weight = volume->influence(position);
                    if (weight <= 0)
                        continue;
                    const auto profile = volume->profileSnapshot();
                    if (profile)
                    {
                        PostProcessVolumeLayer layer{ volume->settings().priority, object->getGUID(),
                            scene->getGUID(), weight, profile->settings };
                        PostProcessCameraSnapshot effectSnapshot;
                        for (const auto& effect : profile->customEffects)
                            if (effect.enabled && !appendPostProcessEffectSnapshot(
                                effectSnapshot, effect.effect, effect.parameters, 1))
                            {
                                scratch.clear();
                                return false;
                            }
                        layer.customEffects = std::move(effectSnapshot.customEffects);
                        scratch.push_back(std::move(layer));
                    }
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcessVolume] Cannot allocate Volume snapshot workspace.");
            scratch.clear();
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcessVolume] Too many Volume snapshots.");
            scratch.clear();
            return false;
        }
        try
        {
            std::sort(scratch.begin(), scratch.end(), [](const auto& left, const auto& right)
            {
                return std::tie(left.priority, left.objectGuid.high, left.objectGuid.low, left.sceneGuid.high, left.sceneGuid.low)
                    < std::tie(right.priority, right.objectGuid.high, right.objectGuid.low, right.sceneGuid.high, right.sceneGuid.low);
            });
            PostProcessCameraSnapshot candidate = destination;
            for (const auto& layer : scratch)
            {
                blendPostProcessLayer(candidate, layer.settings, layer.weight);
                for (auto effect : layer.customEffects)
                {
                    effect.contribution *= layer.weight;
                    candidate.customEffects.push_back(std::move(effect));
                }
            }
            destination = std::move(candidate);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcessVolume] Cannot allocate the resolved Camera snapshot.");
            scratch.clear();
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcessVolume] Resolved Camera snapshot capacity exceeded.");
            scratch.clear();
            return false;
        }
        scratch.clear();
        return true;
    }
}
