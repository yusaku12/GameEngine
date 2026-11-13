#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\AnimatorComponent_generated.h"
#include "Components\Animation\AnimatorComponent.h"
#include "Components\Animation\FootIKComponent.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Model\ModelManager.h"
#include "Components\Rendering\ModelRendererComponent.h"
#include "Components\Physics\Character\CharacterControllerComponent.h"
#include "Components\Physics\Rigidbody\RigidbodyComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Serialization\SerializationVersions.h"
#include <imgui.h>
#include "Core\CoreDefines.h"
#include "Core\Logging\Logger.h"
#include "Core\GameObject\Component.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    namespace
    {
        const ComponentTypeID animatorType = ComponentRegistry::instance().registerType<AnimatorComponent>(
            "Animator", false, false, true,
            [](const GameObject& object) -> std::string_view
            {
                return object.hasComponent<ModelRendererComponent>()
                    ? std::string_view{} : "Add a Model Renderer before adding an Animator.";
            });
    }

    AnimatorComponent::AnimatorComponent() noexcept
    {
        GE_UNUSED(animatorType);
    }

    bool AnimatorComponent::setAnimation(const SkeletonHandle skeleton, const AnimationClipHandle clip)
    {
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const auto skeletonAsset = assets.getSkeleton(skeleton);
        const auto clipAsset = assets.getClip(clip);
        if (skeletonAsset == nullptr || clipAsset == nullptr
            || clipAsset->skeletonGuid != skeletonAsset->guid
            || clipAsset->skeletonSignature != skeletonAsset->signature
            || clipAsset->animation.num_tracks() != skeletonAsset->skeleton.num_joints())
        {
            LOG_ERROR("[Animator] Skeleton and Animation Clip are missing or incompatible.");
            return false;
        }

        const SkeletonHandle previousSkeleton = m_skeleton;
        const AnimationClipHandle previousClip = m_clip;
        const AssetGUID previousSkeletonGuid = m_skeletonGuid;
        const AssetGUID previousClipGuid = m_clipGuid;
        const std::filesystem::path previousSkeletonPath = m_skeletonPath;
        const std::filesystem::path previousClipPath = m_clipPath;
        const bool previousPlaying = m_restorePending ? m_pendingPlaying : isPlaying();
        const float previousTime = m_restorePending ? m_pendingNormalizedTime : getNormalizedTime();
        m_skeleton = skeleton;
        m_clip = clip;
        m_skeletonGuid = skeletonAsset->guid;
        m_clipGuid = clipAsset->guid;
        m_skeletonPath = assets.getSkeletonPath(skeleton);
        m_clipPath = assets.getClipPath(clip);
        registerClip(clip);
        applyClipConfiguration();
        m_restorePending = false;
        m_bindingDirty = true;
        m_evaluationErrorLogged = false;
        if (bindAssets())
        {
            m_startupPending = false;
            return true;
        }

        m_skeleton = previousSkeleton;
        m_clip = previousClip;
        m_skeletonGuid = previousSkeletonGuid;
        m_clipGuid = previousClipGuid;
        m_skeletonPath = previousSkeletonPath;
        m_clipPath = previousClipPath;
        applyClipConfiguration();
        m_pendingPlaying = previousPlaying;
        m_pendingNormalizedTime = previousTime;
        m_restorePending = previousClipGuid.isValid();
        m_bindingDirty = true;
        if (previousClip.isValid())
            bindAssets();
        else
            clearAnimationClip();
        return false;
    }

    AnimatorComponent::ClipConfiguration* AnimatorComponent::findClipConfiguration(const AssetGUID& guid) noexcept
    {
        const auto found = std::find_if(m_clipConfigurations.begin(), m_clipConfigurations.end(),
            [&guid](const ClipConfiguration& configuration) { return configuration.guid == guid; });
        return found != m_clipConfigurations.end() ? &*found : nullptr;
    }

    const AnimatorComponent::ClipConfiguration* AnimatorComponent::findClipConfiguration(const AssetGUID& guid) const noexcept
    {
        const auto found = std::find_if(m_clipConfigurations.begin(), m_clipConfigurations.end(),
            [&guid](const ClipConfiguration& configuration) { return configuration.guid == guid; });
        return found != m_clipConfigurations.end() ? &*found : nullptr;
    }

    void AnimatorComponent::registerClip(const AnimationClipHandle clip)
    {
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const auto asset = assets.getClip(clip);
        if (asset == nullptr)
            return;
        if (std::find(m_availableClips.begin(), m_availableClips.end(), clip) == m_availableClips.end())
            m_availableClips.push_back(clip);
        if (ClipConfiguration* const configuration = findClipConfiguration(asset->guid))
        {
            if (configuration->name.empty())
                configuration->name = asset->name.empty() ? "Animation" : asset->name;
            if (!configuration->wrapMode.has_value())
                configuration->wrapMode = asset->wrapMode;
            const auto path = assets.getClipPath(clip);
            if (!path.empty())
                configuration->path = path;
        }
        else
            m_clipConfigurations.push_back({ asset->guid, assets.getClipPath(clip),
                asset->name.empty() ? "Animation" : asset->name, 1.0f, false, asset->wrapMode });
    }

    void AnimatorComponent::refreshModelClips()
    {
        const GameObject* const object = getGameObject();
        const auto* renderer = object != nullptr ? object->getComponent<ModelRendererComponent>() : nullptr;
        const auto model = renderer != nullptr ? ModelManager::instance().get(renderer->getModel()) : nullptr;
        if (model != nullptr)
            for (const AssetGUID& guid : model->animationClipGuids)
                registerClip(AnimationAssetManager::instance().findClipByGuid(guid));
    }

    void AnimatorComponent::applyClipConfiguration() noexcept
    {
        const ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        m_instance.setSpeed(configuration != nullptr ? configuration->speed : 1.0f);
        m_applyRootMotion = configuration != nullptr && configuration->applyRootMotion;
        m_rootMotionPending = false;
    }

    void AnimatorComponent::setSpeed(const float speed)
    {
        ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        if (configuration == nullptr || !std::isfinite(speed))
        {
            LOG_ERROR("[Animator] Select an animation and provide a finite playback speed.");
            return;
        }
        configuration->speed = speed;
        m_instance.setSpeed(speed);
    }

    bool AnimatorComponent::setAnimationName(const std::string_view name)
    {
        ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        if (configuration == nullptr || name.find_first_not_of(" \t\r\n") == std::string_view::npos
            || name.find('\0') != std::string_view::npos)
        {
            LOG_ERROR("[Animator] Select an animation and provide a nonblank name without null characters.");
            return false;
        }
        configuration->name = name;
        return true;
    }

    std::string_view AnimatorComponent::getAnimationName() const noexcept
    {
        const ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        return configuration != nullptr ? std::string_view(configuration->name) : std::string_view{};
    }

    void AnimatorComponent::setApplyRootMotion(const bool apply)
    {
        ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        if (configuration == nullptr)
        {
            LOG_ERROR("[Animator] Select an animation before setting root motion.");
            return;
        }
        configuration->applyRootMotion = apply;
        m_applyRootMotion = apply;
        m_rootMotionPending = false;
    }

    bool AnimatorComponent::setRootMotionAxes(const RootMotionAxes axes)
    {
        ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        if (configuration == nullptr)
        {
            LOG_ERROR("[Animator] Select an animation before setting root motion axes.");
            return false;
        }
        configuration->rootMotionAxes = axes;
        m_rootMotionPending = false;
        m_rootMotionErrorLogged = false;
        return true;
    }

    RootMotionAxes AnimatorComponent::getRootMotionAxes() const noexcept
    {
        const ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        return configuration != nullptr ? configuration->rootMotionAxes : RootMotionAxes{};
    }

    bool AnimatorComponent::useModelDefaultAnimation()
    {
        const GameObject* const gameObject = getGameObject();
        const ModelRendererComponent* const renderer = gameObject != nullptr
            ? gameObject->getComponent<ModelRendererComponent>() : nullptr;
        const std::shared_ptr<const ModelResource> model = renderer != nullptr
            ? ModelManager::instance().get(renderer->getModel()) : nullptr;
        if (model == nullptr || !model->skeletonAssetGuid.isValid() || model->animationClipGuids.empty())
            return false;

        AnimationAssetManager& assets = AnimationAssetManager::instance();
        if (!setAnimation(assets.findSkeletonByGuid(model->skeletonAssetGuid),
            assets.findClipByGuid(model->animationClipGuids.front())))
            return false;
        refreshModelClips();
        return true;
    }

    bool AnimatorComponent::importAnimation(const std::filesystem::path& path)
    {
        const GameObject* const gameObject = getGameObject();
        const ModelRendererComponent* const renderer = gameObject != nullptr
            ? gameObject->getComponent<ModelRendererComponent>() : nullptr;
        if (renderer == nullptr || !renderer->getModel().isValid())
            return false;
        const std::shared_ptr<const ModelResource> model = ModelManager::instance().get(renderer->getModel());
        if (model == nullptr || !model->skeletonAssetGuid.isValid())
            return false;
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const SkeletonHandle skeleton = assets.findSkeletonByGuid(model->skeletonAssetGuid);
        if (!skeleton.isValid())
            return false;

        const std::vector<AnimationClipHandle> clips =
            ModelManager::instance().importAnimationClips(renderer->getModel(), path);
        if (clips.empty())
            return false;
        const std::filesystem::path modelPath = ModelManager::instance().getPath(renderer->getModel());
        std::string modelExtension = modelPath.extension().string();
        std::transform(modelExtension.begin(), modelExtension.end(), modelExtension.begin(),
            [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if ((modelExtension == ".model" || modelExtension == ".mdl")
            && !ModelManager::instance().save(renderer->getModel()))
            return false;
        if (!setAnimation(skeleton, clips.front()))
            return false;
        for (const AnimationClipHandle clip : clips)
            registerClip(clip);
        return true;
    }

    bool AnimatorComponent::play(const float normalizedTime)
    {
        if (!seek(normalizedTime))
            return false;
        return m_instance.play(normalizedTime);
    }

    bool AnimatorComponent::seek(const float normalizedTime)
    {
        if (!std::isfinite(normalizedTime) || !m_clipGuid.isValid())
        {
            LOG_ERROR("[Animator] Select an animation and provide a finite seek time.");
            return false;
        }
        if (m_restorePending)
        {
            AnimationAssetManager& assets = AnimationAssetManager::instance();
            if (!m_skeleton.isValid()) m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
            if (!m_clip.isValid()) m_clip = assets.findClipByGuid(m_clipGuid);
        }
        if ((m_restorePending || m_bindingDirty) && !bindAssets())
            return false;
        if (!m_instance.configureRootMotion(m_applyRootMotion, m_rootMotionJoint, getRootMotionAxes())
            || !m_instance.seek(normalizedTime))
            return false;
        m_rootMotionPending = false;
        m_startupPending = false;
        m_evaluationErrorLogged = false;
        return true;
    }

    bool AnimatorComponent::stepFrame(const bool forward)
    {
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const auto clip = assets.getClip(m_restorePending ? assets.findClipByGuid(m_clipGuid) : m_clip);
        if (clip == nullptr || !std::isfinite(clip->duration) || clip->duration <= 0.0f)
        {
            LOG_ERROR("[Animator] Frame stepping requires a loaded animation with a valid duration.");
            return false;
        }
        double interval = 1.0 / (30.0 * clip->duration);
        const auto samples = clip->animation.timepoints();
        if (samples.size() > 2 && samples[0] == 0.0f && samples[1] > 0.0f)
        {
            const double candidate = samples[1];
            constexpr double sampleTolerance = 8.0 * std::numeric_limits<float>::epsilon();
            const double lastInterval = static_cast<double>(samples[samples.size() - 1])
                - samples[samples.size() - 2];
            bool uniform = lastInterval > 0.0 && lastInterval <= candidate + sampleTolerance;
            // The final sample can be a shorter interval at the exact Clip endpoint.
            for (std::size_t index = 2; index + 1 < samples.size(); ++index)
                if (std::abs(static_cast<double>(samples[index]) - candidate * static_cast<double>(index))
                    > sampleTolerance)
                {
                    uniform = false;
                    break;
                }
            if (uniform)
                interval = candidate;
        }
        const double target = static_cast<double>(getNormalizedTime()) + (forward ? interval : -interval);
        if (!seek(static_cast<float>(std::clamp(target, 0.0, 1.0))))
            return false;
        pause();
        return true;
    }

    bool AnimatorComponent::resume()
    {
        if (!m_clipGuid.isValid())
        {
            LOG_ERROR("[Animator] Select an animation before resuming.");
            return false;
        }
        if (m_restorePending || m_bindingDirty)
        {
            AnimationAssetManager& assets = AnimationAssetManager::instance();
            if (!m_skeleton.isValid()) m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
            if (!m_clip.isValid()) m_clip = assets.findClipByGuid(m_clipGuid);
            if (!bindAssets())
                return false;
        }
        const float time = getNormalizedTime();
        if (getWrapMode() == AnimationWrapMode::Once
            && ((getSpeed() > 0.0f && time >= 1.0f) || (getSpeed() < 0.0f && time <= 0.0f)))
            return play(getSpeed() < 0.0f ? 1.0f : 0.0f);
        if (!m_instance.resume())
        {
            LOG_ERROR("[Animator] Cannot resume without bound animation assets.");
            return false;
        }
        m_startupPending = false;
        return true;
    }

    bool AnimatorComponent::stop()
    {
        if (!seek(0.0f))
            return false;
        pause();
        return true;
    }

    bool AnimatorComponent::setWrapMode(const AnimationWrapMode mode)
    {
        ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        if (configuration == nullptr || mode > AnimationWrapMode::PingPong)
        {
            LOG_ERROR("[Animator] Select an animation and provide a valid Wrap Mode.");
            return false;
        }
        if (!m_restorePending && m_clip.isValid() && !m_instance.setWrapMode(mode))
            return false;
        configuration->wrapMode = mode;
        m_rootMotionPending = false;
        return true;
    }

    AnimationWrapMode AnimatorComponent::getWrapMode() const noexcept
    {
        const ClipConfiguration* const configuration = findClipConfiguration(m_clipGuid);
        return configuration != nullptr && configuration->wrapMode.has_value()
            ? *configuration->wrapMode : m_instance.getWrapMode();
    }

    bool AnimatorComponent::setInitialAnimation(const AnimationClipHandle clip)
    {
        if (!clip.isValid())
        {
            m_initialClipGuid = {};
            return true;
        }
        const auto asset = AnimationAssetManager::instance().getClip(clip);
        const auto skeleton = AnimationAssetManager::instance().getSkeleton(m_skeleton);
        if (asset == nullptr || asset->skeletonGuid != m_skeletonGuid
            || (skeleton != nullptr && (asset->skeletonSignature != skeleton->signature
                || asset->animation.num_tracks() != skeleton->skeleton.num_joints())))
        {
            LOG_ERROR("[Animator] Initial animation must belong to this component's Skeleton.");
            return false;
        }
        registerClip(clip);
        m_initialClipGuid = asset->guid;
        return true;
    }

    bool AnimatorComponent::setRootMotionJoint(const std::string_view jointName)
    {
        const auto skeleton = AnimationAssetManager::instance().getSkeleton(m_skeleton);
        if (skeleton != nullptr && !jointName.empty()
            && !skeleton->jointLookup.contains(std::string(jointName)))
        {
            LOG_ERROR("[Animator] Root motion joint '{}' does not exist.", jointName);
            return false;
        }
        m_rootMotionJoint = jointName;
        m_rootMotionPending = false;
        m_rootMotionErrorLogged = false;
        return true;
    }

    std::uint32_t AnimatorComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_ANIMATOR_COMPONENT_VERSION;
    }

    bool AnimatorComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder(256);
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const std::filesystem::path skeletonPath = m_skeleton.isValid()
            ? assets.getSkeletonPath(m_skeleton) : m_skeletonPath;
        const std::filesystem::path clipPath = m_clip.isValid()
            ? assets.getClipPath(m_clip) : m_clipPath;

        const GameObject* const gameObject = getGameObject();
        const ModelRendererComponent* const renderer = gameObject != nullptr
            ? gameObject->getComponent<ModelRendererComponent>() : nullptr;
        const std::shared_ptr<const ModelResource> model = renderer != nullptr
            ? ModelManager::instance().get(renderer->getModel()) : nullptr;
        const std::filesystem::path modelPath = renderer != nullptr
            ? ModelManager::instance().getPath(renderer->getModel()) : std::filesystem::path{};
        std::string modelExtension = modelPath.extension().string();
        std::transform(modelExtension.begin(), modelExtension.end(), modelExtension.begin(),
            [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const bool hasPersistentModel = model != nullptr && (modelExtension == ".model" || modelExtension == ".mdl");
        const bool skeletonInModel = hasPersistentModel && model->skeletonAssetGuid == m_skeletonGuid;
        const auto clipInModel = [&model, hasPersistentModel](const AssetGUID& guid)
            {
                return hasPersistentModel
                    && std::find(model->animationClipGuids.begin(), model->animationClipGuids.end(), guid)
                        != model->animationClipGuids.end();
            };
        const bool currentClipInModel = clipInModel(m_clipGuid);
        if ((m_skeletonGuid.isValid() && skeletonPath.empty() && !skeletonInModel)
            || (m_clipGuid.isValid() && clipPath.empty() && !currentClipInModel))
        {
            LOG_ERROR("[Animator] Scene保存前にModelまたはAnimation Assetを保存してください");
            return false;
        }

        std::vector<flatbuffers::Offset<Serialization::AnimatorClipConfiguration>> clips;
        clips.reserve(m_clipConfigurations.size());
        for (const ClipConfiguration& configuration : m_clipConfigurations)
        {
            const auto path = assets.getClipPath(assets.findClipByGuid(configuration.guid));
            const auto& storedPath = path.empty() ? configuration.path : path;
            if (!configuration.guid.isValid() || !std::isfinite(configuration.speed)
                || (configuration.wrapMode.has_value() && *configuration.wrapMode > AnimationWrapMode::PingPong)
                || configuration.name.find_first_not_of(" \t\r\n") == std::string::npos
                || configuration.name.find('\0') != std::string::npos
                || (storedPath.empty() && !clipInModel(configuration.guid)))
            {
                LOG_ERROR("[Animator] Clip settings are invalid or the Clip/Model must be saved before the Scene.");
                return false;
            }
            const Serialization::AssetGuid guid(configuration.guid.high, configuration.guid.low);
            clips.push_back(Serialization::CreateAnimatorClipConfiguration(builder, &guid,
                builder.CreateString(storedPath.generic_string()), builder.CreateString(configuration.name),
                configuration.speed, configuration.applyRootMotion,
                configuration.wrapMode.has_value()
                    ? flatbuffers::Optional<Serialization::AnimationWrapMode>(
                        static_cast<Serialization::AnimationWrapMode>(*configuration.wrapMode))
                    : flatbuffers::nullopt,
                configuration.rootMotionAxes.translationX, configuration.rootMotionAxes.translationY,
                configuration.rootMotionAxes.translationZ, configuration.rootMotionAxes.rotation));
        }
        if (m_initialClipGuid.isValid() && findClipConfiguration(m_initialClipGuid) == nullptr)
        {
            LOG_ERROR("[Animator] Initial animation has no Clip configuration.");
            return false;
        }

        const Serialization::AssetGuid skeletonGuid(m_skeletonGuid.high, m_skeletonGuid.low);
        const Serialization::AssetGuid clipGuid(m_clipGuid.high, m_clipGuid.low);
        const Serialization::AssetGuid initialGuid(m_initialClipGuid.high, m_initialClipGuid.low);
        const bool playing = m_restorePending ? m_pendingPlaying : isPlaying();
        const float normalizedTime = m_restorePending ? m_pendingNormalizedTime : getNormalizedTime();
        const auto root = Serialization::CreateAnimatorComponentPayload(builder, &skeletonGuid, &clipGuid,
            nullptr, getSpeed(), m_applyRootMotion, playing, normalizedTime,
            builder.CreateString(skeletonPath.generic_string()),
            builder.CreateString(clipPath.generic_string()), builder.CreateString(m_rootMotionJoint),
            builder.CreateVector(clips), &initialGuid, m_autoPlay);
        Serialization::FinishAnimatorComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool AnimatorComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        if (version < Serialization::MINIMUM_SUPPORTED_ANIMATOR_COMPONENT_VERSION
            || version > Serialization::CURRENT_ANIMATOR_COMPONENT_VERSION || payload.size() < 8
            || !Serialization::AnimatorComponentPayloadBufferHasIdentifier(payload.data()))
        {
            LOG_ERROR("[Animator] Invalid or unsupported Component payload.");
            return false;
        }
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (!Serialization::VerifyAnimatorComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Animator] Component payload verification failed.");
            return false;
        }
        const Serialization::AnimatorComponentPayload* source =
            Serialization::GetAnimatorComponentPayload(payload.data());
        if (source == nullptr || source->skeleton_guid() == nullptr || source->clip_guid() == nullptr
            || !std::isfinite(source->speed())
            || !std::isfinite(source->normalized_time()))
        {
            LOG_ERROR("[Animator] Component payload has invalid playback settings or missing references.");
            return false;
        }

        m_skeletonGuid = { source->skeleton_guid()->high(), source->skeleton_guid()->low() };
        m_clipGuid = { source->clip_guid()->high(), source->clip_guid()->low() };
        if (const auto* legacy = source->legacy_controller_guid();
            legacy != nullptr && (legacy->high() != 0 || legacy->low() != 0))
            LOG_WARNING("[Animator] Legacy Controller binding was removed; select a Clip for playback.");
        m_skeletonPath = source->skeleton_path() != nullptr
            ? std::filesystem::path(source->skeleton_path()->str()) : std::filesystem::path{};
        m_clipPath = source->clip_path() != nullptr
            ? std::filesystem::path(source->clip_path()->str()) : std::filesystem::path{};
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        std::vector<ClipConfiguration> configurations;
        if (const auto* clips = source->clips())
        {
            configurations.reserve(clips->size());
            for (const Serialization::AnimatorClipConfiguration* configuration : *clips)
            {
                if (configuration == nullptr || configuration->guid() == nullptr
                    || configuration->name() == nullptr || !std::isfinite(configuration->speed()))
                {
                    LOG_ERROR("[Animator] Invalid serialized Clip settings.");
                    return false;
                }
                ClipConfiguration value;
                value.guid = { configuration->guid()->high(), configuration->guid()->low() };
                value.path = configuration->path() != nullptr
                    ? std::filesystem::path(configuration->path()->str()) : std::filesystem::path{};
                value.name = configuration->name()->str();
                value.speed = configuration->speed();
                value.applyRootMotion = configuration->apply_root_motion();
                value.rootMotionAxes = { configuration->root_translation_x(), configuration->root_translation_y(),
                    configuration->root_translation_z(), configuration->root_rotation() };
                if (const auto mode = configuration->wrap_mode())
                {
                    if (*mode > Serialization::AnimationWrapMode_PingPong)
                    {
                        LOG_ERROR("[Animator] Invalid serialized Wrap Mode.");
                        return false;
                    }
                    value.wrapMode = static_cast<AnimationWrapMode>(*mode);
                }
                if (!value.guid.isValid() || value.name.find_first_not_of(" \t\r\n") == std::string::npos
                    || value.name.find('\0') != std::string::npos
                    || std::any_of(configurations.begin(), configurations.end(),
                        [&value](const ClipConfiguration& existing) { return existing.guid == value.guid; }))
                {
                    LOG_ERROR("[Animator] Invalid name, GUID or duplicate serialized Clip settings.");
                    return false;
                }
                configurations.push_back(std::move(value));
            }
        }
        m_instance.clear();
        m_clipConfigurations = std::move(configurations);
        m_initialClipGuid = source->initial_clip_guid() != nullptr
            ? AssetGUID{ source->initial_clip_guid()->high(), source->initial_clip_guid()->low() } : AssetGUID{};
        if (m_initialClipGuid.isValid() && findClipConfiguration(m_initialClipGuid) == nullptr)
        {
            LOG_ERROR("[Animator] Serialized initial animation has no Clip configuration.");
            return false;
        }
        m_autoPlay = source->auto_play();
        m_startupPending = false;
        m_rootMotionJoint = source->root_motion_joint() != nullptr ? source->root_motion_joint()->str() : "";
        m_rootMotionPending = false;
        m_rootMotionErrorLogged = false;
        m_pendingPlaying = source->playing();
        m_pendingNormalizedTime = std::clamp(source->normalized_time(), 0.0f, 1.0f);
        m_skeleton = !m_skeletonPath.empty() ? assets.loadSkeleton(m_skeletonPath)
            : assets.findSkeletonByGuid(m_skeletonGuid);
        m_clip = !m_clipPath.empty() ? assets.loadClip(m_clipPath) : assets.findClipByGuid(m_clipGuid);
        m_availableClips.clear();
        const std::shared_ptr<const SkeletonAsset> skeleton = assets.getSkeleton(m_skeleton);
        const std::shared_ptr<const AnimationClipAsset> clip = assets.getClip(m_clip);
        if (version < 3 && m_clipGuid.isValid() && findClipConfiguration(m_clipGuid) == nullptr)
            m_clipConfigurations.push_back({ m_clipGuid, m_clipPath,
                clip != nullptr ? clip->name : "",
                source->speed(), source->apply_root_motion(), std::nullopt });
        if (m_clipGuid.isValid() && findClipConfiguration(m_clipGuid) == nullptr)
        {
            LOG_ERROR("[Animator] Selected Clip has no serialized settings.");
            return false;
        }
        applyClipConfiguration();
        if ((!m_skeletonPath.empty() && (skeleton == nullptr || skeleton->guid != m_skeletonGuid))
            || (!m_clipPath.empty() && (clip == nullptr || clip->guid != m_clipGuid)))
        {
            LOG_ERROR("[Animator] Serialized Skeleton or Clip path does not match its GUID.");
            return false;
        }
        if (m_clip.isValid())
            registerClip(m_clip);
        for (const ClipConfiguration& configuration : m_clipConfigurations)
        {
            const AnimationClipHandle handle = configuration.path.empty()
                ? assets.findClipByGuid(configuration.guid) : assets.loadClip(configuration.path);
            const auto asset = assets.getClip(handle);
            if (!configuration.path.empty() && (asset == nullptr || asset->guid != configuration.guid))
            {
                LOG_ERROR("[Animator] Serialized Clip settings reference a missing or mismatched Clip.");
                return false;
            }
            if (asset != nullptr && std::find(m_availableClips.begin(), m_availableClips.end(), handle) == m_availableClips.end())
                m_availableClips.push_back(handle);
        }
        m_bindingDirty = false;
        m_restorePending = m_clipGuid.isValid();
        m_evaluationErrorLogged = false;
        return true;
    }

    bool AnimatorComponent::copyConfigurationTo(Component& destination) const
    {
        auto* target = dynamic_cast<AnimatorComponent*>(&destination);
        if (target == nullptr)
        {
            LOG_ERROR("[Play] Animator clone type mismatch.");
            return false;
        }
        target->m_skeleton = m_skeleton;
        target->m_clip = m_clip;
        target->m_skeletonGuid = m_skeletonGuid;
        target->m_clipGuid = m_clipGuid;
        target->m_skeletonPath = m_skeletonPath;
        target->m_clipPath = m_clipPath;
        target->m_clipConfigurations = m_clipConfigurations;
        target->m_initialClipGuid = m_initialClipGuid;
        target->m_autoPlay = m_autoPlay;
        target->m_availableClips = m_availableClips;
        target->m_applyRootMotion = m_applyRootMotion;
        target->m_rootMotionJoint = m_rootMotionJoint;
        target->m_instance.setSpeed(getSpeed());
        target->m_pendingPlaying = m_restorePending ? m_pendingPlaying : isPlaying();
        target->m_pendingNormalizedTime = m_restorePending ? m_pendingNormalizedTime : getNormalizedTime();
        target->m_restorePending = m_clipGuid.isValid();
        target->m_bindingDirty = false;
        return true;
    }

    void AnimatorComponent::clearAnimationClip() noexcept
    {
        m_instance.clear();
        m_clip = AnimationClipHandle::Invalid();
        m_clipGuid = {};
        m_clipPath.clear();
        applyClipConfiguration();
        m_pendingNormalizedTime = 0.0f;
        m_pendingPlaying = true;
        m_restorePending = false;
        m_bindingDirty = false;
        m_evaluationErrorLogged = false;
        m_evaluationPending = false;
        m_rootMotionPending = false;
        m_footIKPending = false;
        m_footIKRequested = false;
        m_rootMotionErrorLogged = false;
    }

    void AnimatorComponent::onAwake()
    {
        m_startupPending = true;
        pause();
        preparePendingUpdate();
        m_evaluationPending = false;
    }

    bool AnimatorComponent::prepareStartup()
    {
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        if (!m_skeletonGuid.isValid() && !m_skeleton.isValid() && !m_clipGuid.isValid())
            useModelDefaultAnimation();
        if (!m_skeleton.isValid())
            m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
        const AssetGUID& initialGuid = m_initialClipGuid.isValid() ? m_initialClipGuid : m_clipGuid;
        const AnimationClipHandle clip = assets.findClipByGuid(initialGuid);
        const auto asset = assets.getClip(clip);
        const auto skeleton = assets.getSkeleton(m_skeleton);
        if (skeleton == nullptr || asset == nullptr || asset->skeletonGuid != m_skeletonGuid
            || asset->skeletonSignature != skeleton->signature
            || asset->animation.num_tracks() != skeleton->skeleton.num_joints())
        {
            if (initialGuid.isValid() && !m_evaluationErrorLogged)
                LOG_ERROR("[Animator] Startup animation or Skeleton is missing or incompatible; playback is waiting.");
            m_evaluationErrorLogged = initialGuid.isValid();
            return false;
        }
        m_clip = clip;
        m_clipGuid = asset->guid;
        m_clipPath = assets.getClipPath(clip);
        registerClip(clip);
        m_pendingNormalizedTime = 0.0f;
        m_pendingPlaying = m_autoPlay;
        m_restorePending = true;
        m_bindingDirty = true;
        m_startupPending = false;
        return true;
    }

    void AnimatorComponent::preparePendingUpdate()
    {
        m_evaluationPending = false;
        m_rootMotionPending = false;
        m_footIKPending = false;
        m_footIKRequested = false;

        if (m_startupPending && !prepareStartup())
            return;
        if (!m_restorePending && !m_skeletonGuid.isValid() && !m_skeleton.isValid() && !m_clip.isValid())
            useModelDefaultAnimation();
        if (m_restorePending)
        {
            AnimationAssetManager& assets = AnimationAssetManager::instance();
            if (!m_skeleton.isValid()) m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
            if (!m_clip.isValid()) m_clip = assets.findClipByGuid(m_clipGuid);
            if (m_skeleton.isValid() && m_clip.isValid())
                m_bindingDirty = true;
        }
        if (m_bindingDirty && !bindAssets())
        {
            m_bindingDirty = false;
            m_restorePending = false;
            return;
        }
        if (!m_restorePending && !m_skeletonGuid.isValid() && !m_skeleton.isValid() && !m_clip.isValid())
            useModelDefaultAnimation();
        if (!m_skeleton.isValid() || !m_clip.isValid())
            return;

        if (!m_instance.configureRootMotion(m_applyRootMotion, m_rootMotionJoint, getRootMotionAxes()))
            return;
        const auto* object = getGameObject();
        const auto* footIK = object ? object->getComponent<FootIKComponent>() : nullptr;
        m_footIKRequested = footIK && footIK->isEnabled();
        m_evaluationPending = true;
    }

    void AnimatorComponent::evaluatePendingUpdate(const float deltaTime)
    {
        if (!m_evaluationPending)
            return;

        m_evaluationPending = false;
        const bool succeeded = m_instance.update(deltaTime, !m_footIKRequested);
        m_footIKPending = succeeded && m_footIKRequested;
        m_rootMotionPending = succeeded && m_applyRootMotion
            && m_instance.getRootMotionDelta() != Matrix::Identity;
        if (!succeeded && !m_evaluationErrorLogged)
        {
            LOG_ERROR("[Animator] Animation evaluation failed");
            m_evaluationErrorLogged = true;
        }
    }

    void AnimatorComponent::applyPendingRootMotion(const float deltaTime)
    {
        if (!std::exchange(m_rootMotionPending, false) || !m_applyRootMotion)
            return;
        GameObject* const object = getGameObject();
        if (object == nullptr || object->isDestroyRequested() || !isEnabled()
            || !object->isActiveInHierarchy())
            return;
        const auto fail = [this](const char* reason)
            {
                if (!m_rootMotionErrorLogged)
                    LOG_ERROR("[Animator] Root motion could not be applied: {}", reason);
                m_rootMotionErrorLogged = true;
            };
        const auto* body = object->getComponent<RigidbodyComponent>();
        auto* controller = object->getComponent<CharacterControllerComponent>();
        if (body != nullptr && body->isEnabled()
            && (!body->isKinematic() || (controller != nullptr && controller->isEnabled())))
        {
            fail("requires no Dynamic Rigidbody and no Rigidbody/CharacterController combination.");
            return;
        }
        Transform delta;
        Transform* const local = object->getTransform();
        Transform validatedLocal;
        const Matrix world = object->getWorldMatrix();
        const float determinant = world.Determinant();
        if (local == nullptr || !Transform::tryFromMatrix(m_instance.getRootMotionDelta(), delta)
            || !Transform::tryFromMatrix(local->toMatrix(), validatedLocal)
            || !std::isfinite(determinant) || determinant == 0.0f)
        {
            fail("invalid or singular object transform.");
            return;
        }
        const Vector3 displacement = Vector3::TransformNormal(delta.getPosition(), world);
        Matrix rotationMatrix = Matrix::CreateFromQuaternion(delta.getRotation())
            * Matrix::CreateFromQuaternion(object->getWorldRotation());
        Vector3 position = object->getWorldPosition() + displacement;
        if (const GameObject* const parent = object->getParent())
        {
            const Matrix parentWorld = parent->getWorldMatrix();
            const float parentDeterminant = parentWorld.Determinant();
            if (!std::isfinite(parentDeterminant) || parentDeterminant == 0.0f)
            {
                fail("invalid or singular parent transform.");
                return;
            }
            position = Vector3::Transform(position, parentWorld.Invert());
            // Transform::combine uses parent-before-local quaternion composition.
            rotationMatrix = Matrix::CreateFromQuaternion(parent->getWorldRotation()).Transpose() * rotationMatrix;
        }
        Quaternion rotation = Quaternion::CreateFromRotationMatrix(rotationMatrix);
        rotation.Normalize();
        const auto finiteVector = [](const Vector3& value)
            { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
        if (!finiteVector(displacement) || !finiteVector(position)
            || !std::isfinite(rotation.x) || !std::isfinite(rotation.y)
            || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
        {
            fail("movement overflowed finite transform coordinates.");
            return;
        }
        if (controller != nullptr && controller->isEnabled())
        {
            if (!controller->tryMove(displacement, std::abs(deltaTime)))
            {
                fail("CharacterController movement was rejected; physics must be running.");
                return;
            }
        }
        else
            local->setPosition(position);
        local->setRotation(rotation);
        m_rootMotionErrorLogged = false;
    }

    void AnimatorComponent::applyPendingFootIK(const float deltaTime)
    {
        if (!std::exchange(m_footIKPending, false))
            return;
        GameObject* const object = getGameObject();
        if (!object || object->isDestroyRequested() || !object->isActiveInHierarchy() || !isEnabled())
            return;
        if (auto* footIK = object->getComponent<FootIKComponent>(); footIK && footIK->isEnabled()
            && footIK->evaluate(m_instance, deltaTime))
            return;
        if (!m_instance.publishCurrentPose())
            LOG_ERROR("[FootIK] Could not publish the uncorrected animation pose after IK was unavailable.");
    }

    void AnimatorComponent::onImGui()
    {
        if (ImGui::Button("Import FBX Animation..."))
        {
            const GameObject* const gameObject = getGameObject();
            if (gameObject != nullptr)
            {
                const ObjectGUID objectGuid = gameObject->getGUID();
                const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
                MainThreadDispatcher::instance().post([objectGuid, ownerWindow]
                    {
                        SceneManager& sceneManager = SceneManager::instance();
                        GameObject* object = nullptr;
                        if (Scene* scene = sceneManager.getActiveScene())
                            object = scene->find(objectGuid);
                        if (object == nullptr)
                            object = sceneManager.getPersistentScene()->find(objectGuid);
                        AnimatorComponent* component = object != nullptr
                            ? object->getComponent<AnimatorComponent>() : nullptr;
                        if (component == nullptr)
                            return;

                        static constexpr std::array filters = {
                            FileDialogFilter{ L"FBX Animation", L"*.fbx;*.dae;*.gltf;*.glb" },
                            FileDialogFilter{ L"All Files", L"*.*" },
                        };
                        std::vector<std::filesystem::path> paths;
                        const DialogResult result = Dialog::openFile(
                            paths, L"アニメーションを追加", "Assets/Model", filters, false, ownerWindow);
                        if (result != DialogResult::Ok || paths.empty() || !component->importAnimation(paths.front()))
                            LOG_ERROR("[Animator] 外部アニメーションの読み込みに失敗しました: {}", paths.empty() ? "" : paths.front().string());
                    });
            }
        }
        const GameObject* const gameObject = getGameObject();
        const ModelRendererComponent* const renderer = gameObject != nullptr
            ? gameObject->getComponent<ModelRendererComponent>() : nullptr;
        const std::shared_ptr<const ModelResource> assignedModel = renderer != nullptr
            ? ModelManager::instance().get(renderer->getModel()) : nullptr;
        refreshModelClips();
        const std::shared_ptr<const AnimationClipAsset> assignedClip =
            AnimationAssetManager::instance().getClip(m_clip);
        bool clipStoredInModel = false;
        if (assignedModel != nullptr && assignedClip != nullptr)
        {
            const auto found = std::find(assignedModel->animationClipGuids.begin(),
                assignedModel->animationClipGuids.end(), assignedClip->guid);
            clipStoredInModel = found != assignedModel->animationClipGuids.end()
                && static_cast<std::size_t>(found - assignedModel->animationClipGuids.begin())
                    < assignedModel->animations.size();
        }
        if (clipStoredInModel)
        {
            ImGui::TextDisabled("Animation is stored in Model.");
        }
        else
        {
            ImGui::BeginDisabled(!m_clip.isValid() || gameObject == nullptr);
            if (ImGui::Button("Save Clip As..."))
            {
                const ObjectGUID objectGuid = gameObject->getGUID();
                const AnimationClipHandle clip = m_clip;
                const ModelHandle model = renderer != nullptr ? renderer->getModel() : ModelHandle::Invalid();
                const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
                MainThreadDispatcher::instance().post([objectGuid, clip, model, ownerWindow]
                    {
                        static constexpr std::array filters = {
                            FileDialogFilter{ L"Animation Clip", L"*.anim" },
                            FileDialogFilter{ L"All Files", L"*.*" },
                        };
                        std::filesystem::path path;
                        if (Dialog::saveFile(path, L"Animation Clipを保存", "Assets/Animations", L"anim", filters, ownerWindow)
                            != DialogResult::Ok)
                            return;
                        if (!AnimationAssetManager::instance().saveClip(clip, path))
                        {
                            LOG_ERROR("[Animator] Animation Clipの保存に失敗しました: {}", path.string());
                            return;
                        }
                        SceneManager& sceneManager = SceneManager::instance();
                        GameObject* object = nullptr;
                        if (Scene* scene = sceneManager.getActiveScene())
                            object = scene->find(objectGuid);
                        if (object == nullptr)
                            object = sceneManager.getPersistentScene()->find(objectGuid);
                        AnimatorComponent* component = object != nullptr
                            ? object->getComponent<AnimatorComponent>() : nullptr;
                        if (component != nullptr && component->m_clip == clip)
                            component->m_clipPath = AnimationAssetManager::instance().getClipPath(clip);
                        if (model.isValid())
                        {
                            if (!ModelManager::instance().associateAnimationClip(model, clip))
                            {
                                LOG_ERROR("[Animator] ModelへのAnimation Clip関連付けに失敗しました");
                                return;
                            }
                            if (!ModelManager::instance().getPath(model).empty()
                                && !ModelManager::instance().save(model))
                                LOG_ERROR("[Animator] Animation Clip関連付け後のModel保存に失敗しました");
                        }
                    });
            }
                    ImGui::EndDisabled();
        }
        ImGui::Text("Playing: %s", isPlaying() ? "Yes" : "No");
        ImGui::Text("Normalized Time: %.3f", getNormalizedTime());
        if (!m_availableClips.empty() && ImGui::TreeNode("Animation Clips"))
        {
            AnimationAssetManager& assets = AnimationAssetManager::instance();
            AnimationClipHandle clipToRemove = AnimationClipHandle::Invalid();
            for (const AnimationClipHandle clip : m_availableClips)
            {
                const auto asset = assets.getClip(clip);
                if (asset == nullptr)
                    continue;
                ImGui::PushID(static_cast<int>(clip.index));
                const bool selected = clip == m_clip;
                const ClipConfiguration* const configuration = findClipConfiguration(asset->guid);
                const ImVec2 textPosition = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##Clip", selected) && !selected)
                    setAnimation(m_skeleton, clip);
                // Render names literally, including ImGui's reserved label characters.
                ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text),
                    configuration != nullptr ? configuration->name.c_str() : asset->name.c_str());
                if (ImGui::BeginPopupContextItem("AnimationClipMenu"))
                {
                    if (clip == m_clip)
                    {
                        ImGui::TextDisabled("Deletes from the shared Model and saves it.");
                        ImGui::TextDisabled("FBX / unsaved Model: use Save Model As... first.");
                        if (ImGui::MenuItem("Delete Animation"))
                            clipToRemove = clip;
                    }
                    else
                        ImGui::TextDisabled("Select this animation first.");
                    if (ImGui::MenuItem("Cancel"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (clipToRemove.isValid())
            {
                const auto asset = assets.getClip(clipToRemove);
                const bool storedInModel = assignedModel != nullptr && asset != nullptr
                    && std::find(assignedModel->animationClipGuids.begin(),
                        assignedModel->animationClipGuids.end(), asset->guid) != assignedModel->animationClipGuids.end();
                if (!storedInModel || ModelManager::instance().removeAnimationClip(renderer->getModel(), asset->guid))
                {
                    if (asset != nullptr)
                    {
                        if (m_initialClipGuid == asset->guid)
                            m_initialClipGuid = {};
                        std::erase_if(m_clipConfigurations,
                            [&asset](const ClipConfiguration& configuration) { return configuration.guid == asset->guid; });
                    }
                    clearAnimationClip();
                    m_availableClips.erase(
                        std::remove(m_availableClips.begin(), m_availableClips.end(), clipToRemove),
                        m_availableClips.end());
                }
            }
            ImGui::TreePop();
        }
        const ClipConfiguration* const initialConfiguration = findClipConfiguration(m_initialClipGuid);
        const char* const initialLabel = initialConfiguration != nullptr
            ? initialConfiguration->name.c_str() : "<Selected Animation>";
        if (ImGui::BeginCombo("Initial Animation", initialLabel))
        {
            if (ImGui::Selectable("<Selected Animation>", !m_initialClipGuid.isValid()))
                setInitialAnimation(AnimationClipHandle::Invalid());
            for (const AnimationClipHandle clip : m_availableClips)
            {
                const auto asset = AnimationAssetManager::instance().getClip(clip);
                if (asset == nullptr)
                    continue;
                const ClipConfiguration* const configuration = findClipConfiguration(asset->guid);
                ImGui::PushID(static_cast<int>(clip.index));
                const ImVec2 textPosition = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##InitialClip", m_initialClipGuid == asset->guid))
                    setInitialAnimation(clip);
                ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text),
                    configuration != nullptr ? configuration->name.c_str() : asset->name.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Play Automatically", &m_autoPlay);
        ImGui::TextDisabled("Startup settings apply when game Play starts, not to the editor preview.");
        ImGui::BeginDisabled(!m_clip.isValid());
        if (m_clip.isValid())
        {
            const std::string_view name = getAnimationName();
            std::vector<char> buffer(name.begin(), name.end());
            buffer.resize((std::max)(std::size_t{ 256 }, name.size() + 64), '\0');
            ImGui::PushID(static_cast<int>(m_clip.index));
            if (ImGui::InputText("Animation Name", buffer.data(), buffer.size(), ImGuiInputTextFlags_CallbackResize,
                [](ImGuiInputTextCallbackData* data)
                {
                    auto& text = *static_cast<std::vector<char>*>(data->UserData);
                    text.resize(static_cast<std::size_t>(data->BufSize));
                    data->Buf = text.data();
                    return 0;
                }, &buffer))
                setAnimationName(buffer.data());
            ImGui::PopID();
        }
        ImGui::TextDisabled("Settings belong to this component. Save Scene / Prefab to keep them.");
        float speed = getSpeed();
        if (ImGui::DragFloat("Speed", &speed, 0.05f))
            setSpeed(speed);
        static constexpr std::array wrapLabels{ "Once", "Loop", "Ping Pong" };
        int wrapMode = static_cast<int>(getWrapMode());
        if (ImGui::Combo("Playback Mode", &wrapMode, wrapLabels.data(), static_cast<int>(wrapLabels.size())))
            setWrapMode(static_cast<AnimationWrapMode>(wrapMode));
        float normalizedTime = getNormalizedTime();
        if (ImGui::SliderFloat("Seek", &normalizedTime, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        {
            if (seek(normalizedTime))
                pause();
        }
        if (ImGui::Button("Previous Frame"))
            stepFrame(false);
        ImGui::SameLine();
        if (ImGui::Button("Next Frame"))
            stepFrame(true);
        ImGui::TextDisabled("One source sample (1/30s if unknown). Frame stepping pauses playback.");
        ImGui::TextDisabled("Scrubbing pauses playback; seeking never moves the GameObject.");
        bool applyRootMotion = getApplyRootMotion();
        if (ImGui::Checkbox("Apply Root Motion", &applyRootMotion))
            setApplyRootMotion(applyRootMotion);
        if (m_applyRootMotion)
        {
            RootMotionAxes axes = getRootMotionAxes();
            bool axesChanged = ImGui::Checkbox("Root Translation X", &axes.translationX);
            ImGui::SameLine();
            axesChanged |= ImGui::Checkbox("Y", &axes.translationY);
            ImGui::SameLine();
            axesChanged |= ImGui::Checkbox("Z", &axes.translationZ);
            axesChanged |= ImGui::Checkbox("Root Rotation", &axes.rotation);
            if (axesChanged)
                setRootMotionAxes(axes);
            ImGui::TextDisabled("Model-local axes. Disabled components remain in the pose.");
            const auto skeleton = AnimationAssetManager::instance().getSkeleton(m_skeleton);
            const char* const label = m_rootMotionJoint.empty() ? "<First Root>" : m_rootMotionJoint.c_str();
            if (ImGui::BeginCombo("Root Motion Joint", label))
            {
                if (ImGui::Selectable("<First Root>", m_rootMotionJoint.empty()))
                    setRootMotionJoint({});
                if (skeleton != nullptr)
                    for (const std::string& name : skeleton->jointNames)
                        if (ImGui::Selectable(name.c_str(), m_rootMotionJoint == name))
                            setRootMotionJoint(name);
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Editor preview stays in place.");
        }
        if (isPlaying())
        {
            if (ImGui::Button("Pause"))
                pause();
        }
        else if (ImGui::Button("Play"))
        {
            resume();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            stop();
        ImGui::SameLine();
        if (ImGui::Button("Rewind"))
            rewind();
        ImGui::EndDisabled();
    }

    bool AnimatorComponent::bindAssets()
    {
        m_bindingDirty = false;
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const std::shared_ptr<const SkeletonAsset> skeleton = assets.getSkeleton(m_skeleton);
        const std::shared_ptr<const AnimationClipAsset> clip = assets.getClip(m_clip);
        registerClip(m_clip);
        applyClipConfiguration();
        if (!m_instance.setAssets(skeleton, clip))
        {
            LOG_ERROR("[Animator] Skeleton and Animation Clip are missing or incompatible");
            return false;
        }
        if (!m_instance.setWrapMode(getWrapMode()))
            return false;
        if (m_restorePending)
        {
            m_instance.play(m_pendingNormalizedTime);
            if (!m_pendingPlaying) m_instance.pause();
            m_restorePending = false;
        }
        if (!m_instance.configureRootMotion(m_applyRootMotion, m_rootMotionJoint, getRootMotionAxes())
            || !m_instance.update(0.0f))
            return false;
        m_evaluationErrorLogged = false;
        return true;
    }
}