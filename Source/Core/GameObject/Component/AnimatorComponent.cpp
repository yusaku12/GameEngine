#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\AnimatorComponent_generated.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Serialization\SerializationVersions.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const ComponentTypeID animatorType = ComponentRegistry::instance().registerType<AnimatorComponent>(
            "Animator", false, false, true);
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
            return false;

        const SkeletonHandle previousSkeleton = m_skeleton;
        const AnimationClipHandle previousClip = m_clip;
        const AnimatorControllerHandle previousController = m_controller;
        const AssetGUID previousSkeletonGuid = m_skeletonGuid;
        const AssetGUID previousClipGuid = m_clipGuid;
        const AssetGUID previousControllerGuid = m_controllerGuid;
        const std::filesystem::path previousSkeletonPath = m_skeletonPath;
        const std::filesystem::path previousClipPath = m_clipPath;
        const std::filesystem::path previousControllerPath = m_controllerPath;
        const std::vector<PersistedClipReference> previousControllerClipReferences = m_controllerClipReferences;
        m_skeleton = skeleton;
        m_clip = clip;
        m_controller = AnimatorControllerHandle::Invalid();
        m_skeletonGuid = skeletonAsset == nullptr ? AssetGUID{} : skeletonAsset->guid;
        m_clipGuid = clipAsset == nullptr ? AssetGUID{} : clipAsset->guid;
        m_controllerGuid = {};
        m_skeletonPath = assets.getSkeletonPath(skeleton);
        m_clipPath = assets.getClipPath(clip);
        m_controllerPath.clear();
        m_controllerClipReferences.clear();
        m_restorePending = false;
        m_bindingDirty = true;
        m_evaluationErrorLogged = false;
        if (bindAssets())
        {
            if (std::find(m_availableClips.begin(), m_availableClips.end(), clip) == m_availableClips.end())
                m_availableClips.push_back(clip);
            return true;
        }

        m_skeleton = previousSkeleton;
        m_clip = previousClip;
        m_controller = previousController;
        m_skeletonGuid = previousSkeletonGuid;
        m_clipGuid = previousClipGuid;
        m_controllerGuid = previousControllerGuid;
        m_skeletonPath = previousSkeletonPath;
        m_clipPath = previousClipPath;
        m_controllerPath = previousControllerPath;
        m_controllerClipReferences = previousControllerClipReferences;
        m_bindingDirty = true;
        bindAssets();
        return false;
    }

    bool AnimatorComponent::setController(const SkeletonHandle skeleton, const AnimatorControllerHandle controller)
    {
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        m_skeleton = skeleton;
        m_clip = AnimationClipHandle::Invalid();
        m_controller = controller;
        const auto skeletonAsset = assets.getSkeleton(skeleton);
        const auto controllerAsset = assets.getController(controller);
        m_skeletonGuid = skeletonAsset == nullptr ? AssetGUID{} : skeletonAsset->guid;
        m_clipGuid = {};
        m_controllerGuid = controllerAsset == nullptr ? AssetGUID{} : controllerAsset->guid;
        m_skeletonPath = assets.getSkeletonPath(skeleton);
        m_clipPath.clear();
        m_controllerPath = assets.getControllerPath(controller);
        m_controllerClipReferences.clear();
        if (controllerAsset != nullptr)
        {
            m_controllerClipReferences.reserve(controllerAsset->states.size());
            for (const AnimatorState& state : controllerAsset->states)
            {
                const AnimationClipHandle clip = assets.findClipByGuid(state.clipGuid);
                m_controllerClipReferences.push_back({ state.clipGuid, assets.getClipPath(clip) });
            }
        }
        m_restorePending = false;
        m_bindingDirty = true;
        m_evaluationErrorLogged = false;
        return bindAssets();
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
        for (const AssetGUID& guid : model->animationClipGuids)
        {
            const AnimationClipHandle clip = assets.findClipByGuid(guid);
            if (clip.isValid() && std::find(m_availableClips.begin(), m_availableClips.end(), clip) == m_availableClips.end())
                m_availableClips.push_back(clip);
        }
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
            if (std::find(m_availableClips.begin(), m_availableClips.end(), clip) == m_availableClips.end())
                m_availableClips.push_back(clip);
        return true;
    }

    bool AnimatorComponent::play(const float normalizedTime) noexcept
    {
        return m_instance.play(normalizedTime);
    }

    bool AnimatorComponent::play(const AnimatorStateID stateId, const float normalizedTime) noexcept
    {
        return m_instance.play(stateId, normalizedTime);
    }

    bool AnimatorComponent::crossFade(const AnimatorStateID stateId, const float duration) noexcept
    {
        return m_instance.crossFade(stateId, duration);
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
        const std::filesystem::path controllerPath = m_controller.isValid()
            ? assets.getControllerPath(m_controller) : m_controllerPath;

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
            || (m_clipGuid.isValid() && clipPath.empty() && !currentClipInModel)
            || (m_controllerGuid.isValid() && controllerPath.empty()))
        {
            LOG_ERROR("[Animator] Scene保存前にModelまたはAnimation Assetを保存してください");
            return false;
        }

        std::vector<flatbuffers::Offset<Serialization::AnimatorParameterValueData>> parameters;
        std::vector<flatbuffers::Offset<Serialization::AnimatorClipReference>> controllerClips;
        const auto controller = assets.getController(m_controller);
        if (controller != nullptr)
        {
            parameters.reserve(controller->parameters.size());
            for (const AnimatorParameter& parameter : controller->parameters)
            {
                float floatValue = parameter.defaultFloat;
                std::int32_t intValue = parameter.defaultInt;
                bool boolValue = parameter.defaultBool;
                if (parameter.type == AnimatorParameterType::Float) m_instance.getFloat(parameter.id, floatValue);
                else if (parameter.type == AnimatorParameterType::Int) m_instance.getInt(parameter.id, intValue);
                else m_instance.getBool(parameter.id, boolValue);
                parameters.push_back(Serialization::CreateAnimatorParameterValueData(builder, parameter.id,
                    static_cast<Serialization::AnimatorParameterValueType>(parameter.type), floatValue, intValue, boolValue));
            }
        }
        else
        {
            parameters.reserve(m_pendingParameters.size());
            for (const PersistedParameterValue& parameter : m_pendingParameters)
                parameters.push_back(Serialization::CreateAnimatorParameterValueData(builder, parameter.id,
                    static_cast<Serialization::AnimatorParameterValueType>(parameter.type), parameter.floatValue,
                    parameter.intValue, parameter.boolValue));
        }

        std::vector<PersistedClipReference> clipReferences = m_controllerClipReferences;
        if (controller != nullptr)
        {
            clipReferences.clear();
            clipReferences.reserve(controller->states.size());
            for (const AnimatorState& state : controller->states)
            {
                const AnimationClipHandle handle = assets.findClipByGuid(state.clipGuid);
                clipReferences.push_back({ state.clipGuid, assets.getClipPath(handle) });
            }
        }
        controllerClips.reserve(clipReferences.size());
        for (const PersistedClipReference& reference : clipReferences)
        {
            if (!reference.guid.isValid() || (reference.path.empty() && !clipInModel(reference.guid)))
            {
                LOG_ERROR("[Animator] Scene保存前にControllerのClip Assetを保存してください");
                return false;
            }
            const Serialization::AssetGuid guid(reference.guid.high, reference.guid.low);
            controllerClips.push_back(Serialization::CreateAnimatorClipReference(builder, &guid,
                builder.CreateString(reference.path.generic_string())));
        }

        const Serialization::AssetGuid skeletonGuid(m_skeletonGuid.high, m_skeletonGuid.low);
        const Serialization::AssetGuid clipGuid(m_clipGuid.high, m_clipGuid.low);
        const Serialization::AssetGuid controllerGuid(m_controllerGuid.high, m_controllerGuid.low);
        const bool playing = m_restorePending ? m_pendingPlaying : isPlaying();
        const AnimatorStateID currentState = m_restorePending ? m_pendingState : getCurrentState();
        const float normalizedTime = m_restorePending ? m_pendingNormalizedTime : getNormalizedTime();
        const auto root = Serialization::CreateAnimatorComponentPayload(builder, &skeletonGuid, &clipGuid,
            &controllerGuid, getSpeed(), m_applyRootMotion, playing, currentState, normalizedTime,
            builder.CreateVector(parameters), builder.CreateString(skeletonPath.generic_string()),
            builder.CreateString(clipPath.generic_string()), builder.CreateString(controllerPath.generic_string()),
            builder.CreateVector(controllerClips));
        Serialization::FinishAnimatorComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool AnimatorComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        if (version < Serialization::MINIMUM_SUPPORTED_ANIMATOR_COMPONENT_VERSION
            || version > Serialization::CURRENT_ANIMATOR_COMPONENT_VERSION || payload.empty()
            || !Serialization::AnimatorComponentPayloadBufferHasIdentifier(payload.data()))
            return false;
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (!Serialization::VerifyAnimatorComponentPayloadBuffer(verifier))
            return false;
        const Serialization::AnimatorComponentPayload* source =
            Serialization::GetAnimatorComponentPayload(payload.data());
        if (source == nullptr || source->skeleton_guid() == nullptr || source->clip_guid() == nullptr
            || source->controller_guid() == nullptr || !std::isfinite(source->speed())
            || !std::isfinite(source->normalized_time()))
            return false;

        m_skeletonGuid = { source->skeleton_guid()->high(), source->skeleton_guid()->low() };
        m_clipGuid = { source->clip_guid()->high(), source->clip_guid()->low() };
        m_controllerGuid = { source->controller_guid()->high(), source->controller_guid()->low() };
        m_skeletonPath = source->skeleton_path() != nullptr
            ? std::filesystem::path(source->skeleton_path()->str()) : std::filesystem::path{};
        m_clipPath = source->clip_path() != nullptr
            ? std::filesystem::path(source->clip_path()->str()) : std::filesystem::path{};
        m_controllerPath = source->controller_path() != nullptr
            ? std::filesystem::path(source->controller_path()->str()) : std::filesystem::path{};
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        m_controllerClipReferences.clear();
        if (const auto* clipReferences = source->controller_clips())
        {
            m_controllerClipReferences.reserve(clipReferences->size());
            for (const Serialization::AnimatorClipReference* reference : *clipReferences)
            {
                if (reference == nullptr || reference->guid() == nullptr || reference->path() == nullptr)
                    return false;
                const AssetGUID guid{ reference->guid()->high(), reference->guid()->low() };
                const std::filesystem::path path(reference->path()->str());
                if (!guid.isValid())
                    return false;
                m_controllerClipReferences.push_back({ guid, path });
                if (!path.empty())
                {
                    const AnimationClipHandle handle = assets.loadClip(path);
                    const std::shared_ptr<const AnimationClipAsset> asset = assets.getClip(handle);
                    if (asset == nullptr || asset->guid != guid)
                        return false;
                }
            }
        }
        m_instance.setSpeed(source->speed());
        m_applyRootMotion = source->apply_root_motion();
        m_pendingPlaying = source->playing();
        m_pendingState = source->current_state();
        m_pendingNormalizedTime = std::clamp(source->normalized_time(), 0.0f, 1.0f);
        m_pendingParameters.clear();
        if (const auto* parameters = source->parameters())
        {
            m_pendingParameters.reserve(parameters->size());
            for (const Serialization::AnimatorParameterValueData* parameter : *parameters)
            {
                if (parameter == nullptr || parameter->id() == 0
                    || parameter->type() > Serialization::AnimatorParameterValueType_Trigger
                    || !std::isfinite(parameter->float_value()))
                    return false;
                m_pendingParameters.push_back({ parameter->id(),
                    static_cast<AnimatorParameterType>(parameter->type()), parameter->float_value(),
                    parameter->int_value(), parameter->bool_value() });
            }
        }
        m_skeleton = !m_skeletonPath.empty() ? assets.loadSkeleton(m_skeletonPath)
            : assets.findSkeletonByGuid(m_skeletonGuid);
        m_clip = !m_clipPath.empty() ? assets.loadClip(m_clipPath) : assets.findClipByGuid(m_clipGuid);
        m_controller = !m_controllerPath.empty() ? assets.loadController(m_controllerPath)
            : assets.findControllerByGuid(m_controllerGuid);
        m_availableClips.clear();
        const std::shared_ptr<const SkeletonAsset> skeleton = assets.getSkeleton(m_skeleton);
        const std::shared_ptr<const AnimationClipAsset> clip = assets.getClip(m_clip);
        const std::shared_ptr<const AnimatorControllerAsset> controller = assets.getController(m_controller);
        if ((!m_skeletonPath.empty() && (skeleton == nullptr || skeleton->guid != m_skeletonGuid))
            || (!m_clipPath.empty() && (clip == nullptr || clip->guid != m_clipGuid))
            || (!m_controllerPath.empty() && (controller == nullptr || controller->guid != m_controllerGuid)))
            return false;
        if (m_clip.isValid())
            m_availableClips.push_back(m_clip);
        m_bindingDirty = false;
        m_restorePending = m_skeletonGuid.isValid() || m_clipGuid.isValid() || m_controllerGuid.isValid();
        m_evaluationErrorLogged = false;
        return true;
    }

    void AnimatorComponent::onAwake()
    {
        if (!m_restorePending && !m_skeleton.isValid() && !m_clip.isValid() && !m_controller.isValid())
            useModelDefaultAnimation();
    }

    void AnimatorComponent::onUpdate([[maybe_unused]] const float deltaTime)
    {
        m_evaluationPending = false;

        if (!m_restorePending && !m_skeleton.isValid() && !m_clip.isValid() && !m_controller.isValid())
            useModelDefaultAnimation();
        if (m_restorePending)
        {
            AnimationAssetManager& assets = AnimationAssetManager::instance();
            if (!m_skeleton.isValid()) m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
            if (!m_clip.isValid()) m_clip = assets.findClipByGuid(m_clipGuid);
            if (!m_controller.isValid()) m_controller = assets.findControllerByGuid(m_controllerGuid);
            for (const PersistedClipReference& reference : m_controllerClipReferences)
            {
                if (!assets.findClipByGuid(reference.guid).isValid() && !reference.path.empty())
                {
                    const AnimationClipHandle handle = assets.loadClip(reference.path);
                    const std::shared_ptr<const AnimationClipAsset> asset = assets.getClip(handle);
                    if (asset != nullptr && asset->guid == reference.guid)
                        m_availableClips.push_back(handle);
                }
            }
            bool dependenciesReady = m_skeleton.isValid();
            if (m_controllerGuid.isValid())
            {
                const std::shared_ptr<const AnimatorControllerAsset> controller = assets.getController(m_controller);
                dependenciesReady = dependenciesReady && controller != nullptr;
                if (controller != nullptr)
                {
                    for (const AnimatorState& state : controller->states)
                        dependenciesReady = dependenciesReady && assets.findClipByGuid(state.clipGuid).isValid();
                }
            }
            else if (m_clipGuid.isValid())
            {
                dependenciesReady = dependenciesReady && m_clip.isValid();
            }
            else
            {
                dependenciesReady = false;
            }
            if (dependenciesReady)
                m_bindingDirty = true;
        }
        if (m_bindingDirty && !bindAssets())
        {
            m_bindingDirty = false;
            m_restorePending = false;
            return;
        }
        if (!m_restorePending && !m_skeleton.isValid() && !m_clip.isValid() && !m_controller.isValid())
            useModelDefaultAnimation();
        if (!m_skeleton.isValid() || (!m_clip.isValid() && !m_controller.isValid()))
            return;

        m_evaluationPending = true;
    }

    void AnimatorComponent::evaluatePendingUpdate(const float deltaTime)
    {
        if (!m_evaluationPending)
            return;

        m_evaluationPending = false;
        if (!m_instance.update(deltaTime) && !m_evaluationErrorLogged)
        {
            LOG_ERROR("[Animator] Animation evaluation failed");
            m_evaluationErrorLogged = true;
        }
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
            for (const AnimationClipHandle clip : m_availableClips)
            {
                const auto asset = assets.getClip(clip);
                if (asset == nullptr)
                    continue;
                const bool selected = clip == m_clip;
                if (ImGui::Selectable(asset->name.c_str(), selected) && !selected)
                    setAnimation(m_skeleton, clip);
            }
            ImGui::TreePop();
        }
        if (m_controller.isValid())
        {
            ImGui::Text("Current State: %u", getCurrentState());
            ImGui::Text("Transition: %.3f", getTransitionProgress());
            const auto controller = AnimationAssetManager::instance().getController(m_controller);
            if (controller != nullptr && ImGui::TreeNode("Parameters"))
            {
                for (const AnimatorParameter& parameter : controller->parameters)
                {
                    ImGui::PushID(static_cast<int>(parameter.id));
                    switch (parameter.type)
                    {
                    case AnimatorParameterType::Float:
                    {
                        float value = 0.0f;
                        if (m_instance.getFloat(parameter.id, value) && ImGui::DragFloat(parameter.name.c_str(), &value, 0.05f))
                            setFloat(parameter.id, value);
                        break;
                    }
                    case AnimatorParameterType::Int:
                    {
                        std::int32_t value = 0;
                        if (m_instance.getInt(parameter.id, value) && ImGui::DragInt(parameter.name.c_str(), &value))
                            setInt(parameter.id, value);
                        break;
                    }
                    case AnimatorParameterType::Bool:
                    {
                        bool value = false;
                        if (m_instance.getBool(parameter.id, value) && ImGui::Checkbox(parameter.name.c_str(), &value))
                            setBool(parameter.id, value);
                        break;
                    }
                    case AnimatorParameterType::Trigger:
                        if (ImGui::Button(parameter.name.c_str())) setTrigger(parameter.id);
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        float speed = getSpeed();
        if (ImGui::DragFloat("Speed", &speed, 0.05f))
            setSpeed(speed);
        ImGui::Checkbox("Apply Root Motion", &m_applyRootMotion);
        if (isPlaying())
        {
            if (ImGui::Button("Pause"))
                pause();
        }
        else if (ImGui::Button("Play"))
        {
            play(getNormalizedTime());
        }
    }

    bool AnimatorComponent::bindAssets()
    {
        m_bindingDirty = false;
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const std::shared_ptr<const SkeletonAsset> skeleton = assets.getSkeleton(m_skeleton);
        if (m_controller.isValid())
        {
            const std::shared_ptr<const AnimatorControllerAsset> controller = assets.getController(m_controller);
            if (controller == nullptr)
            {
                LOG_ERROR("[Animator] Animator Controller is missing");
                return false;
            }
            std::vector<std::shared_ptr<const AnimationClipAsset>> clips;
            clips.reserve(controller->states.size());
            for (const AnimatorState& state : controller->states)
                clips.push_back(assets.getClip(assets.findClipByGuid(state.clipGuid)));
            if (!m_instance.setController(skeleton, controller, std::move(clips)))
            {
                LOG_ERROR("[Animator] Skeleton, Controller, or state Clips are missing or incompatible");
                return false;
            }
            if (m_restorePending)
            {
                for (const PersistedParameterValue& parameter : m_pendingParameters)
                {
                    if (parameter.type == AnimatorParameterType::Float) m_instance.setFloat(parameter.id, parameter.floatValue);
                    else if (parameter.type == AnimatorParameterType::Int) m_instance.setInt(parameter.id, parameter.intValue);
                    else if (parameter.type == AnimatorParameterType::Bool) m_instance.setBool(parameter.id, parameter.boolValue);
                    else if (parameter.boolValue) m_instance.setTrigger(parameter.id);
                }
                m_instance.play(m_pendingState, m_pendingNormalizedTime);
                if (!m_pendingPlaying) m_instance.pause();
                m_restorePending = false;
            }
            m_evaluationErrorLogged = false;
            return true;
        }
        const std::shared_ptr<const AnimationClipAsset> clip = assets.getClip(m_clip);
        if (!m_instance.setAssets(skeleton, clip))
        {
            LOG_ERROR("[Animator] Skeleton and Animation Clip are missing or incompatible");
            return false;
        }
        if (m_restorePending)
        {
            m_instance.play(m_pendingNormalizedTime);
            if (!m_pendingPlaying) m_instance.pause();
            m_restorePending = false;
        }
        m_evaluationErrorLogged = false;
        return true;
    }
}