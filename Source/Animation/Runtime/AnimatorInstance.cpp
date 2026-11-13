#include "Pch.h"
#include "Animation\Runtime\AnimatorInstance.h"
#include "Animation\Runtime\FootIK.h"
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/span.h>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    namespace
    {
        bool isFinite(const Matrix& matrix) noexcept
        {
            for (const auto& row : matrix.m)
                for (const float value : row)
                    if (!std::isfinite(value))
                        return false;
            return true;
        }

        bool rigidTransform(const Matrix& matrix, Matrix& rigid)
        {
            Transform transform;
            if (!Transform::tryFromMatrix(matrix, transform)
                || transform.getScale().x == 0.0f || transform.getScale().y == 0.0f
                || transform.getScale().z == 0.0f)
            {
                LOG_ERROR("[Animator] Root motion requires finite, nonsingular TRS joint transforms (no shear).");
                return false;
            }
            Quaternion rotation = transform.getRotation();
            rotation.Normalize();
            rigid = Matrix::CreateFromQuaternion(rotation) * Matrix::CreateTranslation(transform.getPosition());
            return true;
        }

        Matrix inverseRigidTransform(const Matrix& matrix) noexcept
        {
            Matrix inverse = matrix.Transpose();
            inverse._14 = inverse._24 = inverse._34 = 0.0f;
            inverse._44 = 1.0f;
            inverse.Translation(Vector3::TransformNormal(-matrix.Translation(), inverse));
            return inverse;
        }

        Matrix filterRootMotion(const Matrix& motion, const RootMotionAxes axes) noexcept
        {
            Matrix filtered = axes.rotation ? motion : Matrix::Identity;
            filtered.Translation(Vector3(axes.translationX ? motion._41 : 0.0f,
                axes.translationY ? motion._42 : 0.0f, axes.translationZ ? motion._43 : 0.0f));
            return filtered;
        }

        bool rigidPower(Matrix base, const double exponent, Matrix& result)
        {
            // Loop counts are computed in double; do not silently truncate an unrepresentable count.
            if (!std::isfinite(exponent) || std::abs(exponent) > 9007199254740991.0)
            {
                LOG_ERROR("[Animator] Root motion loop count exceeds the exact supported range.");
                return false;
            }
            if (exponent < 0.0)
                base = inverseRigidTransform(base);
            auto remaining = static_cast<std::uint64_t>(std::abs(exponent));
            result = Matrix::Identity;
            while (remaining != 0)
            {
                if ((remaining & 1u) != 0 && !rigidTransform(result * base, result))
                    return false;
                remaining >>= 1;
                if (remaining != 0 && !rigidTransform(base * base, base))
                    return false;
            }
            return true;
        }

        float sampleRatio(const float time, const float duration, const AnimationWrapMode wrapMode)
        {
            const double sample = wrapMode == AnimationWrapMode::PingPong && time > duration
                ? 2.0 * static_cast<double>(duration) - time : time;
            return static_cast<float>(std::clamp(sample / duration, 0.0, 1.0));
        }

        bool blendRootDeltas(const Matrix& source, const Matrix& destination, const float weight, Matrix& result)
        {
            Transform from;
            Transform to;
            if (!Transform::tryFromMatrix(source, from) || !Transform::tryFromMatrix(destination, to))
            {
                LOG_ERROR("[Animator] Cannot blend invalid root motion deltas.");
                return false;
            }
            return rigidTransform(Transform::lerp(from, to, weight).toMatrix(), result);
        }

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

        bool buildBindPoseCorrection(const Matrix& inverseBind, const Matrix& restPose,
            const std::size_t bone, Matrix& correction)
        {
            for (const Matrix* const matrix : { &inverseBind, &restPose })
            {
                for (const auto& row : matrix->m)
                    for (const float value : row)
                        if (!std::isfinite(value))
                        {
                            LOG_ERROR("[Animator] Bone {} has a nonfinite bind transform.", bone);
                            return false;
                        }
                if (matrix->_14 != 0.0f || matrix->_24 != 0.0f
                    || matrix->_34 != 0.0f || matrix->_44 != 1.0f)
                {
                    LOG_ERROR("[Animator] Bone {} has a non-affine bind transform (last column: {}, {}, {}, {}).",
                        bone, matrix->_14, matrix->_24, matrix->_34, matrix->_44);
                    return false;
                }
            }
            const Matrix bindTransform = inverseBind * restPose;
            DirectX::XMVECTOR determinant;
            Matrix inverse(DirectX::XMMatrixInverse(&determinant, bindTransform));
            const float determinantValue = DirectX::XMVectorGetX(determinant);
            if (!std::isfinite(determinantValue) || determinantValue == 0.0f)
            {
                LOG_ERROR("[Animator] Bone {} has a singular or overflowing bind transform.", bone);
                return false;
            }
            for (const auto& row : inverse.m)
                for (const float value : row)
                    if (!std::isfinite(value))
                    {
                        LOG_ERROR("[Animator] Bone {} has a singular or overflowing bind transform.", bone);
                        return false;
                    }
            // The inverse of an affine matrix has this exact column; a generic inverse rounds det/det.
            inverse._14 = 0.0f;
            inverse._24 = 0.0f;
            inverse._34 = 0.0f;
            inverse._44 = 1.0f;
            correction = inverse;
            return true;
        }

        float wrapTime(const double time, const double duration) noexcept
        {
            double wrapped = std::fmod(time, duration);
            if (wrapped < 0.0)
                wrapped += duration;
            return static_cast<float>(wrapped);
        }

        std::int64_t addLoopCount(const std::int64_t current, const double loops) noexcept
        {
            constexpr std::int64_t maxCount = (std::numeric_limits<std::int64_t>::max)();
            constexpr std::int64_t minCount = (std::numeric_limits<std::int64_t>::min)();
            if (loops >= static_cast<double>(maxCount))
                return maxCount;
            if (loops <= static_cast<double>(minCount))
                return minCount;

            const std::int64_t increment = static_cast<std::int64_t>(loops);
            if (increment > 0 && current > maxCount - increment)
                return maxCount;
            if (increment < 0 && current < minCount - increment)
                return minCount;
            return current + increment;
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

        bool isValidParameterType(const AnimatorParameterType type) noexcept
        {
            switch (type)
            {
            case AnimatorParameterType::Float:
            case AnimatorParameterType::Int:
            case AnimatorParameterType::Bool:
            case AnimatorParameterType::Trigger:
                return true;
            default:
                return false;
            }
        }

        bool isValidWrapOverride(const AnimatorWrapOverride wrapOverride) noexcept
        {
            switch (wrapOverride)
            {
            case AnimatorWrapOverride::UseClip:
            case AnimatorWrapOverride::Once:
            case AnimatorWrapOverride::Loop:
            case AnimatorWrapOverride::PingPong:
                return true;
            default:
                return false;
            }
        }
    }

    bool AnimatorInstance::setAssets(std::shared_ptr<const SkeletonAsset> skeleton,
        std::shared_ptr<const AnimationClipAsset> clip)
    {
        if (skeleton == nullptr || clip == nullptr
            || skeleton->skeleton.num_joints() <= 0
            || clip->skeletonGuid != skeleton->guid
            || clip->skeletonSignature != skeleton->signature
            || clip->animation.num_tracks() != skeleton->skeleton.num_joints()
            || !std::isfinite(clip->duration) || clip->duration <= 0.0f
            || clip->duration != clip->animation.duration()
            || clip->wrapMode > AnimationWrapMode::PingPong
            || skeleton->inverseBindPoses.size() != static_cast<std::size_t>(skeleton->skeleton.num_joints()))
        {
            clear();
            return false;
        }

        clear();
        const int jointCount = skeleton->skeleton.num_joints();
        m_skeleton = std::move(skeleton);
        m_clip = std::move(clip);
        m_wrapMode = m_clip->wrapMode;
        m_samplingContext = std::make_unique<ozz::animation::SamplingJob::Context>(jointCount);
        m_localTransforms.resize(static_cast<std::size_t>(m_skeleton->skeleton.num_soa_joints()));
        m_modelMatrices.resize(static_cast<std::size_t>(jointCount));
        m_ikLocalTransforms.resize(m_localTransforms.size());
        m_ikModelMatrices.resize(m_modelMatrices.size());
        m_bindPoseCorrections.resize(static_cast<std::size_t>(jointCount));
        ozz::animation::LocalToModelJob restPoseJob;
        restPoseJob.skeleton = &m_skeleton->skeleton;
        restPoseJob.input = m_skeleton->skeleton.joint_rest_poses();
        restPoseJob.output = ozz::make_span(m_modelMatrices);
        if (!restPoseJob.Run())
        {
            clear();
            return false;
        }
        for (std::size_t index = 0; index < m_bindPoseCorrections.size(); ++index)
        {
            if (!buildBindPoseCorrection(m_skeleton->inverseBindPoses[index],
                toEngineMatrix(m_modelMatrices[index]), index, m_bindPoseCorrections[index]))
            {
                clear();
                return false;
            }
        }
        for (auto& snapshot : m_snapshotRing)
        {
            snapshot = std::make_shared<SkinningPaletteSnapshot>();
            snapshot->skeletonGuid = m_skeleton->guid;
            snapshot->skeletonSignature = m_skeleton->signature;
            snapshot->jointCount = static_cast<std::uint32_t>(jointCount);
            snapshot->constants.boneMatrices.fill(Matrix::Identity);
            snapshot->constants.boneNormalMatrices.fill(Matrix::Identity);
            snapshot->constants.previousBoneMatrices.fill(Matrix::Identity);
        }
        m_time = 0.0f;
        m_loopCount = 0;
        m_nextSnapshot = 0;
        m_playing = true;
        return samplePose(0.0f) && publishPose();
    }

    bool AnimatorInstance::setController(std::shared_ptr<const SkeletonAsset> skeleton,
        std::shared_ptr<const AnimatorControllerAsset> controller,
        std::vector<std::shared_ptr<const AnimationClipAsset>> clips)
    {
        if (skeleton == nullptr || controller == nullptr || clips.size() != controller->states.size()
            || controller->skeletonGuid != skeleton->guid || controller->skeletonSignature != skeleton->signature
            || controller->layerId == 0 || controller->defaultState == 0 || controller->states.empty()
            || skeleton->skeleton.num_joints() <= 0
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
                || !isValidWrapOverride(state.wrapOverride)
                || clip == nullptr || clip->guid != state.clipGuid || clip->skeletonGuid != m_skeleton->guid
                || clip->skeletonSignature != m_skeleton->signature || clip->animation.num_tracks() != jointCount
                || !std::isfinite(clip->duration) || clip->duration <= 0.0f
                || clip->duration != clip->animation.duration()
                || clip->wrapMode > AnimationWrapMode::PingPong)
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
                || !isValidParameterType(parameter.type)
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
                || !std::isfinite(transition.exitTime) || transition.exitTime < 0.0f
                || (transition.hasExitTime && transition.exitTime > 1.0f))
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
                if (parameterIndex == m_parameters.size() || !std::isfinite(condition.floatThreshold))
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
        m_ikLocalTransforms.resize(m_localTransforms.size());
        m_ikModelMatrices.resize(m_modelMatrices.size());
        m_bindPoseCorrections.resize(static_cast<std::size_t>(jointCount));
        ozz::animation::LocalToModelJob restPoseJob;
        restPoseJob.skeleton = &m_skeleton->skeleton;
        restPoseJob.input = m_skeleton->skeleton.joint_rest_poses();
        restPoseJob.output = ozz::make_span(m_modelMatrices);
        if (!restPoseJob.Run())
        {
            clear();
            return false;
        }
        for (std::size_t index = 0; index < m_bindPoseCorrections.size(); ++index)
        {
            if (!buildBindPoseCorrection(m_skeleton->inverseBindPoses[index],
                toEngineMatrix(m_modelMatrices[index]), index, m_bindPoseCorrections[index]))
            {
                clear();
                return false;
            }
        }
        for (auto& snapshot : m_snapshotRing)
        {
            snapshot = std::make_shared<SkinningPaletteSnapshot>();
            snapshot->skeletonGuid = m_skeleton->guid;
            snapshot->skeletonSignature = m_skeleton->signature;
            snapshot->jointCount = static_cast<std::uint32_t>(jointCount);
            snapshot->constants.boneMatrices.fill(Matrix::Identity);
            snapshot->constants.boneNormalMatrices.fill(Matrix::Identity);
            snapshot->constants.previousBoneMatrices.fill(Matrix::Identity);
        }
        m_nextSnapshot = 0;
        m_playing = true;
        return sampleControllerPose() && publishPose();
    }

    void AnimatorInstance::clear() noexcept
    {
        m_rootMotionEnabled = false;
        m_rootMotionAxes = {};
        m_rootMotionJointName.clear();
        m_rootSamplingContext.reset();
        m_rootMotionDelta = Matrix::Identity;
        m_publishedSnapshot.reset();
        for (auto& snapshot : m_snapshotRing)
            snapshot.reset();
        m_modelMatrices.clear();
        m_ikLocalTransforms.clear();
        m_ikModelMatrices.clear();
        m_bindPoseCorrections.clear();
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
        m_wrapMode = AnimationWrapMode::Loop;
    }

    bool AnimatorInstance::play(const float normalizedTime) noexcept
    {
        m_rootMotionDelta = Matrix::Identity;
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
        m_rootMotionDelta = Matrix::Identity;
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

    bool AnimatorInstance::seek(const float normalizedTime)
    {
        if (m_clip == nullptr || m_controller != nullptr || !std::isfinite(normalizedTime))
        {
            LOG_ERROR("[Animator] Seeking requires a single Clip and a finite time.");
            return false;
        }
        if (std::none_of(m_snapshotRing.begin(), m_snapshotRing.end(),
            [](const auto& snapshot) { return snapshot != nullptr && snapshot.use_count() == 1; }))
        {
            LOG_ERROR("[Animator] No free pose snapshot; seek was not applied.");
            return false;
        }
        const float previousTime = m_time;
        const auto previousLoops = m_loopCount;
        m_time = std::clamp(normalizedTime, 0.0f, 1.0f) * m_clip->duration;
        m_loopCount = 0;
        m_samplingContext->Invalidate();
        if (!samplePose(getNormalizedTime()) || !publishPose())
        {
            m_time = previousTime;
            m_loopCount = previousLoops;
            m_samplingContext->Invalidate();
            LOG_ERROR("[Animator] Seek evaluation failed; keeping the previous time and published pose.");
            return false;
        }
        m_rootMotionDelta = Matrix::Identity;
        return true;
    }

    bool AnimatorInstance::setWrapMode(const AnimationWrapMode mode) noexcept
    {
        if (m_clip == nullptr || m_controller != nullptr || mode > AnimationWrapMode::PingPong)
        {
            LOG_ERROR("[Animator] Wrap Mode requires a single Clip and a valid mode.");
            return false;
        }
        if (mode != m_wrapMode)
        {
            const float ratio = getNormalizedTime();
            m_wrapMode = mode;
            m_time = ratio * m_clip->duration;
            m_loopCount = 0;
            m_rootMotionDelta = Matrix::Identity;
        }
        return true;
    }

    bool AnimatorInstance::crossFade(const AnimatorStateID stateId, const float duration) noexcept
    {
        if (m_controller == nullptr || !std::isfinite(duration) || duration < 0.0f)
            return false;
        const std::size_t destination = findState(stateId);
        if (destination == m_currentStateIndex)
            return destination != m_states.size();
        return destination != m_states.size() && beginTransition(destination, duration);
    }

    bool AnimatorInstance::configureRootMotion(const bool enabled, const std::string_view jointName,
        const RootMotionAxes axes)
    {
        m_rootMotionDelta = Matrix::Identity;
        if (!enabled)
        {
            m_rootMotionEnabled = false;
            return true;
        }
        if (m_rootMotionEnabled && m_rootMotionJointName == jointName && m_rootMotionAxes == axes)
            return true;
        const bool previouslyEnabled = m_rootMotionEnabled;
        const std::size_t previousJoint = m_rootMotionJoint;
        const Matrix previousReference = m_rootReference;
        m_rootMotionEnabled = false;
        if (m_skeleton == nullptr)
        {
            LOG_ERROR("[Animator] Cannot configure root motion without a skeleton.");
            return false;
        }
        m_rootMotionJoint = 0;
        if (!jointName.empty())
        {
            const auto found = m_skeleton->jointLookup.find(std::string(jointName));
            if (found == m_skeleton->jointLookup.end())
            {
                LOG_ERROR("[Animator] Root motion joint '{}' does not exist.", jointName);
                return false;
            }
            m_rootMotionJoint = found->second;
        }
        if (m_rootMotionJoint >= m_modelMatrices.size())
        {
            LOG_ERROR("[Animator] Root motion joint index is outside the skeleton.");
            return false;
        }
        m_rootSamplingContext = std::make_unique<ozz::animation::SamplingJob::Context>(
            m_skeleton->skeleton.num_joints());
        if (!(m_controller != nullptr ? sampleControllerPose() : samplePose(getNormalizedTime())))
        {
            LOG_ERROR("[Animator] Root motion reference-pose evaluation failed.");
            return false;
        }
        Matrix previousRemoval = Matrix::Identity;
        if (previouslyEnabled)
        {
            Matrix previousRoot;
            if (!rigidTransform(toEngineMatrix(m_modelMatrices[previousJoint]), previousRoot))
                return false;
            previousRemoval = inverseRigidTransform(filterRootMotion(
                inverseRigidTransform(previousReference) * previousRoot, m_rootMotionAxes));
        }
        if (!rigidTransform(toEngineMatrix(m_modelMatrices[m_rootMotionJoint]) * previousRemoval, m_rootReference))
            return false;
        m_inverseRootReference = inverseRigidTransform(m_rootReference);
        m_rootMotionAxes = axes;
        if (m_controller != nullptr)
        {
            for (RuntimeState& state : m_states)
                if (!buildRootTrack(*state.clip, state.rootMotion))
                    return false;
        }
        else if (m_clip == nullptr || !buildRootTrack(*m_clip, m_rootTrack))
            return false;
        m_rootMotionJointName = jointName;
        m_rootMotionEnabled = true;
        return true;
    }

    bool AnimatorInstance::sampleRoot(const AnimationClipAsset& clip, const float ratio, Matrix& root)
    {
        const ozz::animation::SamplingJob sampling{
            .ratio = ratio, .animation = &clip.animation, .context = m_rootSamplingContext.get(),
            .output = ozz::make_span(m_localTransforms) };
        ozz::animation::LocalToModelJob localToModel;
        localToModel.skeleton = &m_skeleton->skeleton;
        localToModel.input = ozz::make_span(m_localTransforms);
        localToModel.output = ozz::make_span(m_modelMatrices);
        Matrix rigid;
        if (!sampling.Run() || !localToModel.Run())
        {
            LOG_ERROR("[Animator] Root motion sampling/hierarchy evaluation failed.");
            return false;
        }
        if (!rigidTransform(toEngineMatrix(m_modelMatrices[m_rootMotionJoint]), rigid))
            return false;
        if (!rigidTransform(m_inverseRootReference * rigid, root))
            return false;
        root = filterRootMotion(root, m_rootMotionAxes);
        return true;
    }

    bool AnimatorInstance::buildRootTrack(const AnimationClipAsset& clip, RootMotionTrack& track)
    {
        Matrix start;
        Matrix end;
        if (!sampleRoot(clip, 0.0f, start) || !sampleRoot(clip, 1.0f, end))
            return false;
        track.inverseStart = inverseRigidTransform(start);
        return rigidTransform(end * track.inverseStart, track.cycle);
    }

    bool AnimatorInstance::calculateRootDelta(const AnimationClipAsset& clip, const RootMotionTrack& track,
        const AnimationWrapMode wrapMode, const float oldTime, const float newTime,
        const double step, Matrix& delta)
    {
        if (step == 0.0)
        {
            delta = Matrix::Identity;
            return true;
        }
        Matrix previous;
        Matrix current;
        Matrix cycles = Matrix::Identity;
        if (!sampleRoot(clip, sampleRatio(oldTime, clip.duration, wrapMode), previous)
            || !sampleRoot(clip, sampleRatio(newTime, clip.duration, wrapMode), current))
            return false;
        if (wrapMode == AnimationWrapMode::Loop
            && !rigidPower(track.cycle, std::floor((oldTime + step) / clip.duration), cycles))
            return false;
        // Extend the trajectory across wraps before differencing, including reverse/multiple loops.
        const Matrix before = previous * track.inverseStart;
        const Matrix after = current * track.inverseStart;
        return rigidTransform(after * cycles * inverseRigidTransform(before), delta);
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
        if (m_wrapMode == AnimationWrapMode::PingPong)
        {
            const float period = m_clip->duration * 2.0f;
            const float sampleTime = m_time <= m_clip->duration ? m_time : period - m_time;
            return std::clamp(sampleTime / m_clip->duration, 0.0f, 1.0f);
        }
        return std::clamp(m_time / m_clip->duration, 0.0f, 1.0f);
    }

    bool AnimatorInstance::update(const float deltaTime, const bool publishSnapshot)
    {
        m_rootMotionDelta = Matrix::Identity;
        if (m_skeleton == nullptr || !std::isfinite(deltaTime))
            return false;
        if (m_rootMotionEnabled && std::none_of(m_snapshotRing.begin(), m_snapshotRing.end(),
            [](const auto& snapshot) { return snapshot != nullptr && snapshot.use_count() == 1; }))
        {
            LOG_ERROR("[Animator] No free pose snapshot; animation time was not advanced.");
            return false;
        }
        Matrix rootDelta = Matrix::Identity;

        if (m_controller != nullptr)
        {
            if (m_states.empty()) return false;
            if (m_playing && deltaTime != 0.0f)
            {
                RuntimeState& source = m_states[m_currentStateIndex];
                const float sourceTime = source.time;
                advanceState(source, deltaTime);
                if (m_rootMotionEnabled && !calculateRootDelta(*source.clip, source.rootMotion,
                    source.wrapMode, sourceTime, source.time,
                    static_cast<double>(deltaTime) * m_speed * source.speed, rootDelta))
                    return false;
                if (m_transitioning)
                {
                    RuntimeState& destination = m_states[m_destinationStateIndex];
                    const float destinationTime = destination.time;
                    advanceState(destination, deltaTime);
                    if (m_rootMotionEnabled)
                    {
                        Matrix destinationDelta;
                        if (!calculateRootDelta(*destination.clip, destination.rootMotion,
                            destination.wrapMode, destinationTime, destination.time,
                            static_cast<double>(deltaTime) * m_speed * destination.speed, destinationDelta))
                            return false;
                        const float elapsed = std::abs(deltaTime);
                        const float remaining = std::max(0.0f, m_transitionDuration - m_transitionTime);
                        const float blendedTime = std::min(elapsed, remaining);
                        const float start = getTransitionProgress();
                        const float end = m_transitionDuration > 0.0f
                            ? std::min(1.0f, (m_transitionTime + blendedTime) / m_transitionDuration) : 1.0f;
                        const float weight = elapsed > 0.0f
                            ? 1.0f - (blendedTime / elapsed) * (1.0f - 0.5f * (start + end)) : start;
                        if (!blendRootDeltas(rootDelta, destinationDelta, weight, rootDelta))
                            return false;
                    }
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
                        && ((deltaTime * m_speed * current.speed > 0.0f && current.time >= current.clip->duration)
                            || (deltaTime * m_speed * current.speed < 0.0f && current.time <= 0.0f)))
                        m_playing = false;
                }
            }
            if (!sampleControllerPose() || !publishPose(m_modelMatrices, publishSnapshot))
                return false;
            m_rootMotionDelta = rootDelta;
            return true;
        }

        if (m_clip == nullptr)
            return false;

        if (m_playing && deltaTime != 0.0f)
        {
            const float previousTime = m_time;
            const double unwrappedTime = static_cast<double>(m_time)
                + static_cast<double>(deltaTime) * static_cast<double>(m_speed);
            switch (m_wrapMode)
            {
            case AnimationWrapMode::Once:
                m_time = static_cast<float>(std::clamp(unwrappedTime, 0.0, static_cast<double>(m_clip->duration)));
                if ((deltaTime * m_speed > 0.0f && unwrappedTime >= m_clip->duration)
                    || (deltaTime * m_speed < 0.0f && unwrappedTime <= 0.0f))
                    m_playing = false;
                break;
            case AnimationWrapMode::Loop:
                m_loopCount = addLoopCount(m_loopCount,
                    std::floor(unwrappedTime / static_cast<double>(m_clip->duration)));
                m_time = wrapTime(unwrappedTime, static_cast<double>(m_clip->duration));
                break;
            case AnimationWrapMode::PingPong:
            {
                const double period = static_cast<double>(m_clip->duration) * 2.0;
                const float wrapped = wrapTime(unwrappedTime, period);
                m_loopCount = addLoopCount(m_loopCount, std::floor(unwrappedTime / period));
                m_time = wrapped;
                break;
            }
            }
            if (m_rootMotionEnabled && !calculateRootDelta(*m_clip, m_rootTrack, m_wrapMode,
                previousTime, m_time, static_cast<double>(deltaTime) * m_speed, rootDelta))
                return false;
        }
        if (!samplePose(getNormalizedTime()) || !publishPose(m_modelMatrices, publishSnapshot))
            return false;
        m_rootMotionDelta = rootDelta;
        return true;
    }

    void AnimatorInstance::advanceState(RuntimeState& state, const float deltaTime) noexcept
    {
        const double unwrappedTime = static_cast<double>(state.time)
            + static_cast<double>(deltaTime) * static_cast<double>(m_speed) * static_cast<double>(state.speed);
        switch (state.wrapMode)
        {
        case AnimationWrapMode::Once:
            state.time = static_cast<float>(
                std::clamp(unwrappedTime, 0.0, static_cast<double>(state.clip->duration)));
            break;
        case AnimationWrapMode::Loop:
            state.loopCount = addLoopCount(state.loopCount,
                std::floor(unwrappedTime / static_cast<double>(state.clip->duration)));
            state.time = wrapTime(unwrappedTime, static_cast<double>(state.clip->duration));
            break;
        case AnimationWrapMode::PingPong:
        {
            const double period = static_cast<double>(state.clip->duration) * 2.0;
            state.loopCount = addLoopCount(state.loopCount, std::floor(unwrappedTime / period));
            state.time = wrapTime(unwrappedTime, period);
            break;
        }
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

    bool AnimatorInstance::sampleControllerPose()
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

        std::copy(pose->begin(), pose->end(), m_localTransforms.begin());
        ozz::animation::LocalToModelJob localToModel;
        localToModel.skeleton = &m_skeleton->skeleton;
        localToModel.input = ozz::make_span(*pose);
        localToModel.output = ozz::make_span(m_modelMatrices);
        if (!localToModel.Run()) return false;

        return true;
    }

    bool AnimatorInstance::samplePose(const float ratio)
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

        return true;
    }

    bool AnimatorInstance::publishPose()
    {
        return publishPose(m_modelMatrices);
    }

    bool AnimatorInstance::getPoseRemoval(Matrix& removeMotion) const
    {
        removeMotion = Matrix::Identity;
        if (m_rootMotionEnabled)
        {
            Matrix root;
            if (!rigidTransform(toEngineMatrix(m_modelMatrices[m_rootMotionJoint]), root))
                return false;
            // Filter the trajectory, not its frame delta, so pose and extracted motion stay complementary.
            removeMotion = inverseRigidTransform(filterRootMotion(m_inverseRootReference * root, m_rootMotionAxes));
        }
        return true;
    }

    bool AnimatorInstance::getJointWorldPosition(const int joint, const Matrix& world, Vector3& position) const
    {
        if (joint < 0 || static_cast<std::size_t>(joint) >= m_modelMatrices.size() || !isFinite(world))
        {
            LOG_ERROR("[FootIK] Cannot read a missing joint or invalid world transform.");
            return false;
        }
        Matrix removal;
        if (!getPoseRemoval(removal))
            return false;
        position = Vector3::Transform(toEngineMatrix(m_modelMatrices[static_cast<std::size_t>(joint)]).Translation(),
            removal * world);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            LOG_ERROR("[FootIK] Joint world position overflowed finite coordinates.");
            return false;
        }
        return true;
    }

    bool AnimatorInstance::applyFootIK(const std::span<const FootIKTarget> targets,
        const Vector3& pelvisOffset, const Matrix& world)
    {
        Matrix removal;
        Transform transform;
        if (m_skeleton == nullptr || targets.size() > 2
            || !validateFootIKTargets(m_skeleton->skeleton, targets) || !getPoseRemoval(removal)
            || !isFinite(world) || !Transform::tryFromMatrix(removal * world, transform))
        {
            LOG_ERROR("[FootIK] Missing animation or invalid model-to-world transform.");
            return false;
        }
        const Vector3 scale = transform.getScale();
        if (scale.x <= 0.0f || std::abs(scale.y - scale.x) > scale.x * 1.0e-4f
            || std::abs(scale.z - scale.x) > scale.x * 1.0e-4f)
        {
            LOG_ERROR("[FootIK] Model-to-world transform requires positive uniform scale without shear.");
            return false;
        }
        const Matrix inverse = (removal * world).Invert();
        std::array<FootIKTarget, 2> modelTargets;
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            modelTargets[index] = targets[index];
            modelTargets[index].position = Vector3::Transform(targets[index].position, inverse);
            modelTargets[index].normal = Vector3::TransformNormal(targets[index].normal, inverse);
            modelTargets[index].normal.Normalize();
        }
        std::copy(m_localTransforms.begin(), m_localTransforms.end(), m_ikLocalTransforms.begin());
        std::copy(m_modelMatrices.begin(), m_modelMatrices.end(), m_ikModelMatrices.begin());
        if (!solveFootIK(m_skeleton->skeleton, std::span(modelTargets).first(targets.size()),
            Vector3::TransformNormal(pelvisOffset, inverse), m_ikLocalTransforms, m_ikModelMatrices))
            return false;
        return publishPose(m_ikModelMatrices);
    }

    bool AnimatorInstance::publishPose(const std::span<const ozz::math::Float4x4> models, const bool publishSnapshot)
    {
        Matrix removeMotion;
        if (!getPoseRemoval(removeMotion))
            return false;
        SkinningPaletteSnapshot* const snapshot = acquireSnapshot();
        if (snapshot == nullptr)
            return false;
        for (std::size_t index = 0; index < models.size(); ++index)
        {
            const Matrix modelMatrix = toEngineMatrix(models[index]) * removeMotion;
            const Matrix skinningMatrix = m_bindPoseCorrections[index]
                * m_skeleton->inverseBindPoses[index] * modelMatrix;
            const Matrix normalMatrix = skinningMatrix.Invert().Transpose();
            if (!isFinite(skinningMatrix) || !isFinite(normalMatrix))
            {
                LOG_ERROR("[Animator] Joint {} '{}' produced a nonfinite or singular skinning transform; "
                    "keeping the last valid palette.", index, m_skeleton->jointNames[index]);
                return false;
            }
            snapshot->constants.boneMatrices[index] = skinningMatrix;
            snapshot->constants.boneNormalMatrices[index] = normalMatrix;
        }
        if (publishSnapshot)
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