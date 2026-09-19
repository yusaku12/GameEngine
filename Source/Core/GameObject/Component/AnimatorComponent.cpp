#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\AnimatorComponent_generated.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
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
        m_skeleton = skeleton;
        m_clip = clip;
        m_controller = AnimatorControllerHandle::Invalid();
        const auto skeletonAsset = assets.getSkeleton(skeleton);
        const auto clipAsset = assets.getClip(clip);
        m_skeletonGuid = skeletonAsset == nullptr ? AssetGUID{} : skeletonAsset->guid;
        m_clipGuid = clipAsset == nullptr ? AssetGUID{} : clipAsset->guid;
        m_controllerGuid = {};
        m_restorePending = false;
        m_bindingDirty = true;
        m_evaluationErrorLogged = false;
        return bindAssets();
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
        return setAnimation(assets.findSkeletonByGuid(model->skeletonAssetGuid),
            assets.findClipByGuid(model->animationClipGuids.front()));
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
        std::vector<flatbuffers::Offset<Serialization::AnimatorParameterValueData>> parameters;
        const auto controller = AnimationAssetManager::instance().getController(m_controller);
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

        const Serialization::AssetGuid skeletonGuid(m_skeletonGuid.high, m_skeletonGuid.low);
        const Serialization::AssetGuid clipGuid(m_clipGuid.high, m_clipGuid.low);
        const Serialization::AssetGuid controllerGuid(m_controllerGuid.high, m_controllerGuid.low);
        const bool playing = m_restorePending ? m_pendingPlaying : isPlaying();
        const AnimatorStateID currentState = m_restorePending ? m_pendingState : getCurrentState();
        const float normalizedTime = m_restorePending ? m_pendingNormalizedTime : getNormalizedTime();
        const auto root = Serialization::CreateAnimatorComponentPayload(builder, &skeletonGuid, &clipGuid,
            &controllerGuid, getSpeed(), m_applyRootMotion, playing, currentState, normalizedTime,
            builder.CreateVector(parameters));
        Serialization::FinishAnimatorComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool AnimatorComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        if (version != Serialization::CURRENT_ANIMATOR_COMPONENT_VERSION || payload.empty()
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
        AnimationAssetManager& assets = AnimationAssetManager::instance();
        m_skeleton = assets.findSkeletonByGuid(m_skeletonGuid);
        m_clip = assets.findClipByGuid(m_clipGuid);
        m_controller = assets.findControllerByGuid(m_controllerGuid);
        m_bindingDirty = true;
        m_restorePending = true;
        m_evaluationErrorLogged = false;
        return true;
    }

    void AnimatorComponent::onAwake()
    {
        if (!m_restorePending && !m_skeleton.isValid() && !m_clip.isValid() && !m_controller.isValid())
            useModelDefaultAnimation();
    }

    void AnimatorComponent::onUpdate(const float deltaTime)
    {
        if (m_bindingDirty && !bindAssets())
            return;
        if (!m_skeleton.isValid() || (!m_clip.isValid() && !m_controller.isValid()))
            return;
        if (!m_instance.update(deltaTime) && !m_evaluationErrorLogged)
        {
            LOG_ERROR("[Animator] Animation evaluation failed");
            m_evaluationErrorLogged = true;
        }
    }

    void AnimatorComponent::onImGui()
    {
        ImGui::Text("Playing: %s", isPlaying() ? "Yes" : "No");
        ImGui::Text("Normalized Time: %.3f", getNormalizedTime());
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