#include "Pch.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
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
        m_skeleton = skeleton;
        m_clip = clip;
        m_controller = AnimatorControllerHandle::Invalid();
        m_bindingDirty = true;
        m_evaluationErrorLogged = false;
        return bindAssets();
    }

    bool AnimatorComponent::setController(const SkeletonHandle skeleton, const AnimatorControllerHandle controller)
    {
        m_skeleton = skeleton;
        m_clip = AnimationClipHandle::Invalid();
        m_controller = controller;
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

    void AnimatorComponent::onAwake()
    {
        if (!m_skeleton.isValid() && !m_clip.isValid())
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
            m_evaluationErrorLogged = false;
            return true;
        }
        const std::shared_ptr<const AnimationClipAsset> clip = assets.getClip(m_clip);
        if (!m_instance.setAssets(skeleton, clip))
        {
            LOG_ERROR("[Animator] Skeleton and Animation Clip are missing or incompatible");
            return false;
        }
        m_evaluationErrorLogged = false;
        return true;
    }
}