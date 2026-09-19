#include "Pch.h"
#include "Animation\Runtime\AnimatorInstance.h"
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/span.h>

namespace Engine
{
    namespace
    {
        Matrix toEngineMatrix(const ozz::math::Float4x4& value) noexcept
        {
            alignas(16) float columns[4][4]{};
            for (std::size_t column = 0; column < 4; ++column)
                ozz::math::StorePtrU(value.cols[column], columns[column]);
            return Matrix(
                columns[0][0], columns[0][1], columns[0][2], columns[0][3],
                columns[1][0], columns[1][1], columns[1][2], columns[1][3],
                columns[2][0], columns[2][1], columns[2][2], columns[2][3],
                columns[3][0], columns[3][1], columns[3][2], columns[3][3]);
        }

        float wrapTime(const float time, const float duration) noexcept
        {
            float wrapped = std::fmod(time, duration);
            if (wrapped < 0.0f)
                wrapped += duration;
            return wrapped;
        }

        bool isConditionCompatible(const AnimatorParameterType type, const AnimatorConditionMode mode) noexcept
        {
            if (type == AnimatorParameterType::Float || type == AnimatorParameterType::Int)
                return mode == AnimatorConditionMode::Greater || mode == AnimatorConditionMode::Less
                    || mode == AnimatorConditionMode::Equals || mode == AnimatorConditionMode::NotEqual;
            if (type == AnimatorParameterType::Bool)
                return mode == AnimatorConditionMode::If || mode == AnimatorConditionMode::IfNot;
            return type == AnimatorParameterType::Trigger && mode == AnimatorConditionMode::Triggered;
        }
    }

    bool AnimatorInstance::setAssets(std::shared_ptr<const SkeletonAsset> skeleton,
        std::shared_ptr<const AnimationClipAsset> clip)
    {
        if (skeleton == nullptr || clip == nullptr
            || clip->skeletonGuid != skeleton->guid
            || clip->skeletonSignature != skeleton->signature
            || clip->animation.num_tracks() != skeleton->skeleton.num_joints()
            || skeleton->inverseBindPoses.size() != static_cast<std::size_t>(skeleton->skeleton.num_joints()))
        {
            clear();
            return false;
        }

        clear();
        const int jointCount = skeleton->skeleton.num_joints();
        m_skeleton = std::move(skeleton);
        m_clip = std::move(clip);
        m_samplingContext = std::make_unique<ozz::animation::SamplingJob::Context>(jointCount);
        m_localTransforms.resize(static_cast<std::size_t>(m_skeleton->skeleton.num_soa_joints()));
        m_modelMatrices.resize(static_cast<std::size_t>(jointCount));
        for (auto& snapshot : m_snapshotRing)
        {
            snapshot = std::make_shared<SkinningPaletteSnapshot>();
            snapshot->skeletonGuid = m_skeleton->guid;
            snapshot->skeletonSignature = m_skeleton->signature;
            snapshot->jointCount = static_cast<std::uint32_t>(jointCount);
            snapshot->constants.boneMatrices.fill(Matrix::Identity);
            snapshot->constants.boneNormalMatrices.fill(Matrix::Identity);
        }
        m_time = 0.0f;
        m_loopCount = 0;
        m_nextSnapshot = 0;
        m_playing = true;
        return evaluate(0.0f);
    }

    bool AnimatorInstance::setController(std::shared_ptr<const SkeletonAsset> skeleton,
        std::shared_ptr<const AnimatorControllerAsset> controller,
        std::vector<std::shared_ptr<const AnimationClipAsset>> clips)
    {
        if (skeleton == nullptr || controller == nullptr || clips.size() != controller->states.size()
            || controller->skeletonGuid != skeleton->guid || controller->skeletonSignature != skeleton->signature
            || controller->layerId == 0 || controller->defaultState == 0 || controller->states.empty()
            || skeleton->inverseBindPoses.size() != static_cast<std::size_t>(skeleton->skeleton.num_joints()))
        {
            clear();
            return false;
        }

        clear();
        const int jointCount = skeleton->skeleton.num_joints();
        const std::size_t soaJointCount = static_cast<std::size_t>(skeleton->skeleton.num_soa_joints());
        m_skeleton = std::move(skeleton);
        m_controller = std::move(controller);
        m_states.reserve(m_controller->states.size());
        for (std::size_t index = 0; index < m_controller->states.size(); ++index)
        {
            const AnimatorState& state = m_controller->states[index];
            const auto& clip = clips[index];
            if (state.id == 0 || !std::isfinite(state.speed) || findState(state.id) != m_states.size()
                || clip == nullptr || clip->guid != state.clipGuid || clip->skeletonGuid != m_skeleton->guid
                || clip->skeletonSignature != m_skeleton->signature || clip->animation.num_tracks() != jointCount)
            {
                clear();
                return false;
            }
            AnimationWrapMode wrapMode = clip->wrapMode;
            if (state.wrapOverride != AnimatorWrapOverride::UseClip)
                wrapMode = static_cast<AnimationWrapMode>(static_cast<std::uint8_t>(state.wrapOverride) - 1u);
            RuntimeState runtimeState;
            runtimeState.id = state.id;
            runtimeState.clip = clip;
            runtimeState.context = std::make_unique<ozz::animation::SamplingJob::Context>(jointCount);
            runtimeState.localTransforms.resize(soaJointCount);
            runtimeState.speed = state.speed;
            runtimeState.wrapMode = wrapMode;
            m_states.push_back(std::move(runtimeState));
        }

        m_parameters.reserve(m_controller->parameters.size());
        for (const AnimatorParameter& parameter : m_controller->parameters)
        {
            if (parameter.id == 0 || !std::isfinite(parameter.defaultFloat)
                || findParameter(parameter.id) != m_parameters.size())
            {
                clear();
                return false;
            }
            m_parameters.push_back({ parameter.id, parameter.type, parameter.defaultFloat,
                parameter.defaultInt, parameter.defaultBool });
        }

        m_transitions.reserve(m_controller->transitions.size());
        for (const AnimatorTransition& transition : m_controller->transitions)
        {
            if (transition.id == 0 || !std::isfinite(transition.duration) || transition.duration < 0.0f
                || !std::isfinite(transition.exitTime) || transition.exitTime < 0.0f)
            {
                clear();
                return false;
            }
            for (const RuntimeTransition& existing : m_transitions)
                if (existing.id == transition.id)
                {
                    clear();
                    return false;
                }
            RuntimeTransition runtimeTransition;
            runtimeTransition.id = transition.id;
            runtimeTransition.sourceStateIndex = transition.anyState ? 0 : findState(transition.sourceState);
            runtimeTransition.destinationStateIndex = findState(transition.destinationState);
            if ((!transition.anyState && runtimeTransition.sourceStateIndex == m_states.size())
                || runtimeTransition.destinationStateIndex == m_states.size())
            {
                clear();
                return false;
            }
            runtimeTransition.duration = transition.duration;
            runtimeTransition.exitTime = transition.exitTime;
            runtimeTransition.hasExitTime = transition.hasExitTime;
            runtimeTransition.anyState = transition.anyState;
            runtimeTransition.conditions.reserve(transition.conditions.size());
            for (const AnimatorCondition& condition : transition.conditions)
            {
                const std::size_t parameterIndex = findParameter(condition.parameterId);
                if (parameterIndex == m_parameters.size())
                {
                    clear();
                    return false;
                }
                if (!isConditionCompatible(m_parameters[parameterIndex].type, condition.mode))
                {
                    clear();
                    return false;
                }
                runtimeTransition.conditions.push_back({ parameterIndex, condition.mode,
                    condition.floatThreshold, condition.intThreshold });
            }
            m_transitions.push_back(std::move(runtimeTransition));
        }

        m_currentStateIndex = findState(m_controller->defaultState);
        if (m_currentStateIndex == m_states.size())
        {
            clear();
            return false;
        }
        m_destinationStateIndex = m_currentStateIndex;
        m_localTransforms.resize(soaJointCount);
        m_blendedTransforms.resize(soaJointCount);
        m_modelMatrices.resize(static_cast<std::size_t>(jointCount));
        for (auto& snapshot : m_snapshotRing)
        {
            snapshot = std::make_shared<SkinningPaletteSnapshot>();
            snapshot->skeletonGuid = m_skeleton->guid;
            snapshot->skeletonSignature = m_skeleton->signature;
            snapshot->jointCount = static_cast<std::uint32_t>(jointCount);
            snapshot->constants.boneMatrices.fill(Matrix::Identity);
            snapshot->constants.boneNormalMatrices.fill(Matrix::Identity);
        }
        m_nextSnapshot = 0;
        m_playing = true;
        return evaluateCurrentPose();
    }

    void AnimatorInstance::clear() noexcept
    {
        m_publishedSnapshot.reset();
        for (auto& snapshot : m_snapshotRing)
            snapshot.reset();
        m_modelMatrices.clear();
        m_blendedTransforms.clear();
        m_localTransforms.clear();
        m_samplingContext.reset();
        m_clip.reset();
        m_transitions.clear();
        m_parameters.clear();
        m_states.clear();
        m_controller.reset();
        m_skeleton.reset();
        m_time = 0.0f;
        m_loopCount = 0;
        m_nextSnapshot = 0;
        m_currentStateIndex = 0;
        m_destinationStateIndex = 0;
        m_transitionTime = 0.0f;
        m_transitionDuration = 0.0f;
        m_transitioning = false;
        m_playing = false;
    }

    bool AnimatorInstance::play(const float normalizedTime) noexcept
    {
        if (m_controller != nullptr)
            return play(getCurrentState(), normalizedTime);
        if (m_clip == nullptr || !std::isfinite(normalizedTime))
            return false;
        m_time = std::clamp(normalizedTime, 0.0f, 1.0f) * m_clip->duration;
        m_loopCount = 0;
        m_playing = true;
        if (m_samplingContext != nullptr)
            m_samplingContext->Invalidate();
        return true;
    }

    bool AnimatorInstance::play(const AnimatorStateID stateId, const float normalizedTime) noexcept
    {
        if (m_controller == nullptr || !std::isfinite(normalizedTime))
            return false;
        const std::size_t stateIndex = findState(stateId);
        if (stateIndex == m_states.size())
            return false;
        m_currentStateIndex = stateIndex;
        m_destinationStateIndex = stateIndex;
        m_transitioning = false;
        RuntimeState& state = m_states[stateIndex];
        state.time = std::clamp(normalizedTime, 0.0f, 1.0f) * state.clip->duration;
        state.loopCount = 0;
        state.context->Invalidate();
        m_playing = true;
        return true;
    }

    bool AnimatorInstance::crossFade(const AnimatorStateID stateId, const float duration) noexcept
    {
        if (m_controller == nullptr || !std::isfinite(duration) || duration < 0.0f)
            return false;
        const std::size_t destination = findState(stateId);
        return destination != m_states.size() && beginTransition(destination, duration);
    }

    bool AnimatorInstance::setFloat(const AnimatorParameterID parameterId, const float value) noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Float
            || !std::isfinite(value)) return false;
        m_parameters[index].floatValue = value;
        return true;
    }

    bool AnimatorInstance::setInt(const AnimatorParameterID parameterId, const std::int32_t value) noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Int) return false;
        m_parameters[index].intValue = value;
        return true;
    }

    bool AnimatorInstance::setBool(const AnimatorParameterID parameterId, const bool value) noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Bool) return false;
        m_parameters[index].boolValue = value;
        return true;
    }

    bool AnimatorInstance::setTrigger(const AnimatorParameterID parameterId) noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Trigger) return false;
        m_parameters[index].boolValue = true;
        return true;
    }

    bool AnimatorInstance::resetTrigger(const AnimatorParameterID parameterId) noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Trigger) return false;
        m_parameters[index].boolValue = false;
        return true;
    }

    bool AnimatorInstance::getFloat(const AnimatorParameterID parameterId, float& value) const noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Float) return false;
        value = m_parameters[index].floatValue;
        return true;
    }

    bool AnimatorInstance::getInt(const AnimatorParameterID parameterId, std::int32_t& value) const noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || m_parameters[index].type != AnimatorParameterType::Int) return false;
        value = m_parameters[index].intValue;
        return true;
    }

    bool AnimatorInstance::getBool(const AnimatorParameterID parameterId, bool& value) const noexcept
    {
        const std::size_t index = findParameter(parameterId);
        if (index == m_parameters.size() || (m_parameters[index].type != AnimatorParameterType::Bool
            && m_parameters[index].type != AnimatorParameterType::Trigger)) return false;
        value = m_parameters[index].boolValue;
        return true;
    }

    AnimatorStateID AnimatorInstance::getCurrentState() const noexcept
    {
        return m_controller != nullptr && m_currentStateIndex < m_states.size() ? m_states[m_currentStateIndex].id : 0;
    }

    float AnimatorInstance::getTransitionProgress() const noexcept
    {
        if (!m_transitioning) return 0.0f;
        return m_transitionDuration <= 0.0f ? 1.0f
            : std::clamp(m_transitionTime / m_transitionDuration, 0.0f, 1.0f);
    }

    void AnimatorInstance::setSpeed(const float speed) noexcept
    {
        if (std::isfinite(speed))
            m_speed = speed;
    }

    float AnimatorInstance::getNormalizedTime() const noexcept
    {
        if (m_controller != nullptr)
            return m_currentStateIndex < m_states.size() ? normalizedTime(m_states[m_currentStateIndex]) : 0.0f;
        if (m_clip == nullptr || m_clip->duration <= 0.0f)
            return 0.0f;
        if (m_clip->wrapMode == AnimationWrapMode::PingPong)
        {
            const float period = m_clip->duration * 2.0f;
            const float sampleTime = m_time <= m_clip->duration ? m_time : period - m_time;
            return std::clamp(sampleTime / m_clip->duration, 0.0f, 1.0f);
        }
        return std::clamp(m_time / m_clip->duration, 0.0f, 1.0f);
    }

    bool AnimatorInstance::update(const float deltaTime)
    {
        if (m_skeleton == nullptr || !std::isfinite(deltaTime))
            return false;

        if (m_controller != nullptr)
        {
            if (m_states.empty()) return false;
            if (m_playing)
            {
                advanceState(m_states[m_currentStateIndex], deltaTime);
                if (m_transitioning)
                {
                    advanceState(m_states[m_destinationStateIndex], deltaTime);
                    m_transitionTime += std::abs(deltaTime);
                    if (m_transitionDuration <= 0.0f || m_transitionTime >= m_transitionDuration)
                    {
                        m_currentStateIndex = m_destinationStateIndex;
                        m_transitioning = false;
                    }
                }
                if (!m_transitioning)
                {
                    bool transitionStarted = false;
                    for (const RuntimeTransition& transition : m_transitions)
                    {
                        if ((!transition.anyState && transition.sourceStateIndex != m_currentStateIndex)
                            || transition.destinationStateIndex == m_currentStateIndex
                            || (transition.hasExitTime
                                && m_states[m_currentStateIndex].loopCount == 0
                                && normalizedTime(m_states[m_currentStateIndex]) < transition.exitTime)
                            || !conditionsPass(transition))
                            continue;
                        consumeTriggers(transition);
                        beginTransition(transition.destinationStateIndex, transition.duration);
                        transitionStarted = true;
                        break;
                    }
                    const RuntimeState& current = m_states[m_currentStateIndex];
                    if (!transitionStarted && current.wrapMode == AnimationWrapMode::Once
                        && ((m_speed * current.speed >= 0.0f && current.time >= current.clip->duration)
                            || (m_speed * current.speed < 0.0f && current.time <= 0.0f)))
                        m_playing = false;
                }
            }
            return evaluateCurrentPose();
        }

        if (m_clip == nullptr)
            return false;

        if (m_playing)
        {
            const float previousTime = m_time;
            const float unwrappedTime = previousTime + deltaTime * m_speed;
            switch (m_clip->wrapMode)
            {
            case AnimationWrapMode::Once:
                m_time = std::clamp(unwrappedTime, 0.0f, m_clip->duration);
                if ((m_speed > 0.0f && unwrappedTime >= m_clip->duration)
                    || (m_speed < 0.0f && unwrappedTime <= 0.0f))
                    m_playing = false;
                break;
            case AnimationWrapMode::Loop:
                m_loopCount += static_cast<std::int64_t>(std::floor(unwrappedTime / m_clip->duration));
                m_time = wrapTime(unwrappedTime, m_clip->duration);
                break;
            case AnimationWrapMode::PingPong:
            {
                const float period = m_clip->duration * 2.0f;
                const float wrapped = wrapTime(unwrappedTime, period);
                m_loopCount += static_cast<std::int64_t>(std::floor(unwrappedTime / period));
                m_time = wrapped;
                break;
            }
            }
        }
        return evaluate(getNormalizedTime());
    }

    void AnimatorInstance::advanceState(RuntimeState& state, const float deltaTime) noexcept
    {
        const float unwrappedTime = state.time + deltaTime * m_speed * state.speed;
        switch (state.wrapMode)
        {
        case AnimationWrapMode::Once:
            state.time = std::clamp(unwrappedTime, 0.0f, state.clip->duration);
            break;
        case AnimationWrapMode::Loop:
            state.loopCount += static_cast<std::int64_t>(std::floor(unwrappedTime / state.clip->duration));
            state.time = wrapTime(unwrappedTime, state.clip->duration);
            break;
        case AnimationWrapMode::PingPong:
            const float period = state.clip->duration * 2.0f;
            state.loopCount += static_cast<std::int64_t>(std::floor(unwrappedTime / period));
            state.time = wrapTime(unwrappedTime, period);
            break;
        }
    }

    float AnimatorInstance::normalizedTime(const RuntimeState& state) const noexcept
    {
        if (state.wrapMode == AnimationWrapMode::PingPong)
        {
            const float period = state.clip->duration * 2.0f;
            const float sampleTime = state.time <= state.clip->duration ? state.time : period - state.time;
            return std::clamp(sampleTime / state.clip->duration, 0.0f, 1.0f);
        }
        return std::clamp(state.time / state.clip->duration, 0.0f, 1.0f);
    }

    std::size_t AnimatorInstance::findState(const AnimatorStateID stateId) const noexcept
    {
        for (std::size_t index = 0; index < m_states.size(); ++index)
            if (m_states[index].id == stateId) return index;
        return m_states.size();
    }

    std::size_t AnimatorInstance::findParameter(const AnimatorParameterID parameterId) const noexcept
    {
        for (std::size_t index = 0; index < m_parameters.size(); ++index)
            if (m_parameters[index].id == parameterId) return index;
        return m_parameters.size();
    }

    bool AnimatorInstance::conditionsPass(const RuntimeTransition& transition) const noexcept
    {
        for (const RuntimeCondition& condition : transition.conditions)
        {
            const RuntimeParameter& parameter = m_parameters[condition.parameterIndex];
            bool passed = false;
            switch (condition.mode)
            {
            case AnimatorConditionMode::Greater: passed = parameter.type == AnimatorParameterType::Float
                ? parameter.floatValue > condition.floatThreshold : parameter.intValue > condition.intThreshold; break;
            case AnimatorConditionMode::Less: passed = parameter.type == AnimatorParameterType::Float
                ? parameter.floatValue < condition.floatThreshold : parameter.intValue < condition.intThreshold; break;
            case AnimatorConditionMode::Equals: passed = parameter.type == AnimatorParameterType::Float
                ? parameter.floatValue == condition.floatThreshold : parameter.intValue == condition.intThreshold; break;
            case AnimatorConditionMode::NotEqual: passed = parameter.type == AnimatorParameterType::Float
                ? parameter.floatValue != condition.floatThreshold : parameter.intValue != condition.intThreshold; break;
            case AnimatorConditionMode::If: passed = parameter.boolValue; break;
            case AnimatorConditionMode::IfNot: passed = !parameter.boolValue; break;
            case AnimatorConditionMode::Triggered: passed = parameter.boolValue; break;
            }
            if (!passed) return false;
        }
        return true;
    }

    void AnimatorInstance::consumeTriggers(const RuntimeTransition& transition) noexcept
    {
        for (const RuntimeCondition& condition : transition.conditions)
            if (m_parameters[condition.parameterIndex].type == AnimatorParameterType::Trigger)
                m_parameters[condition.parameterIndex].boolValue = false;
    }

    bool AnimatorInstance::beginTransition(const std::size_t destinationStateIndex, const float duration) noexcept
    {
        if (destinationStateIndex >= m_states.size()) return false;
        if (duration <= 0.0f)
        {
            m_currentStateIndex = destinationStateIndex;
            m_destinationStateIndex = destinationStateIndex;
            m_transitioning = false;
            m_states[destinationStateIndex].time = 0.0f;
            m_states[destinationStateIndex].loopCount = 0;
            m_states[destinationStateIndex].context->Invalidate();
            return true;
        }
        m_destinationStateIndex = destinationStateIndex;
        m_transitionTime = 0.0f;
        m_transitionDuration = duration;
        m_transitioning = true;
        m_states[destinationStateIndex].time = 0.0f;
        m_states[destinationStateIndex].loopCount = 0;
        m_states[destinationStateIndex].context->Invalidate();
        return true;
    }

    bool AnimatorInstance::sampleState(RuntimeState& state)
    {
        const ozz::animation::SamplingJob sampling{
            .ratio = normalizedTime(state), .animation = &state.clip->animation,
            .context = state.context.get(), .output = ozz::make_span(state.localTransforms) };
        return sampling.Run();
    }

    bool AnimatorInstance::evaluateCurrentPose()
    {
        RuntimeState& current = m_states[m_currentStateIndex];
        if (!sampleState(current)) return false;
        const std::vector<ozz::math::SoaTransform>* pose = &current.localTransforms;
        if (m_transitioning)
        {
            RuntimeState& destination = m_states[m_destinationStateIndex];
            if (!sampleState(destination)) return false;
            const float progress = getTransitionProgress();
            const std::array<ozz::animation::BlendingJob::Layer, 2> layers{ {
                { 1.0f - progress, ozz::make_span(current.localTransforms), {} },
                { progress, ozz::make_span(destination.localTransforms), {} },
            } };
            ozz::animation::BlendingJob blending;
            blending.layers = ozz::make_span(layers);
            blending.rest_pose = m_skeleton->skeleton.joint_rest_poses();
            blending.output = ozz::make_span(m_blendedTransforms);
            if (!blending.Run()) return false;
            pose = &m_blendedTransforms;
        }

        ozz::animation::LocalToModelJob localToModel;
        localToModel.skeleton = &m_skeleton->skeleton;
        localToModel.input = ozz::make_span(*pose);
        localToModel.output = ozz::make_span(m_modelMatrices);
        if (!localToModel.Run()) return false;

        SkinningPaletteSnapshot* const snapshot = acquireSnapshot();
        if (snapshot == nullptr) return false;
        for (std::size_t index = 0; index < m_modelMatrices.size(); ++index)
        {
            const Matrix modelMatrix = toEngineMatrix(m_modelMatrices[index]);
            const Matrix skinningMatrix = m_skeleton->inverseBindPoses[index] * modelMatrix;
            snapshot->constants.boneMatrices[index] = skinningMatrix;
            snapshot->constants.boneNormalMatrices[index] = skinningMatrix.Invert().Transpose();
        }
        m_publishedSnapshot = m_snapshotRing[(m_nextSnapshot + m_snapshotRing.size() - 1) % m_snapshotRing.size()];
        return true;
    }

    bool AnimatorInstance::evaluate(const float ratio)
    {
        if (m_samplingContext == nullptr || m_skeleton == nullptr || m_clip == nullptr)
            return false;

        const ozz::animation::SamplingJob sampling{
            .ratio = ratio,
            .animation = &m_clip->animation,
            .context = m_samplingContext.get(),
            .output = ozz::make_span(m_localTransforms),
        };
        if (!sampling.Run())
            return false;

        ozz::animation::LocalToModelJob localToModel;
        localToModel.skeleton = &m_skeleton->skeleton;
        localToModel.input = ozz::make_span(m_localTransforms);
        localToModel.output = ozz::make_span(m_modelMatrices);
        if (!localToModel.Run())
            return false;

        SkinningPaletteSnapshot* const snapshot = acquireSnapshot();
        if (snapshot == nullptr)
            return false;
        for (std::size_t index = 0; index < m_modelMatrices.size(); ++index)
        {
            const Matrix modelMatrix = toEngineMatrix(m_modelMatrices[index]);
            const Matrix skinningMatrix = m_skeleton->inverseBindPoses[index] * modelMatrix;
            snapshot->constants.boneMatrices[index] = skinningMatrix;
            snapshot->constants.boneNormalMatrices[index] = skinningMatrix.Invert().Transpose();
        }
        m_publishedSnapshot = m_snapshotRing[(m_nextSnapshot + m_snapshotRing.size() - 1) % m_snapshotRing.size()];
        return true;
    }

    SkinningPaletteSnapshot* AnimatorInstance::acquireSnapshot() noexcept
    {
        for (std::size_t attempt = 0; attempt < m_snapshotRing.size(); ++attempt)
        {
            const std::size_t index = (m_nextSnapshot + attempt) % m_snapshotRing.size();
            if (m_snapshotRing[index].use_count() == 1)
            {
                m_nextSnapshot = (index + 1) % m_snapshotRing.size();
                return m_snapshotRing[index].get();
            }
        }
        return nullptr;
    }
}