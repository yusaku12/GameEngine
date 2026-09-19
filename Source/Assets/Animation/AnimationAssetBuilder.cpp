#include "Pch.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>

namespace Engine
{
    namespace
    {
        void hashBytes(std::uint64_t& hash, const void* data, const std::size_t size) noexcept
        {
            constexpr std::uint64_t prime = 1099511628211ull;
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= prime;
            }
        }

        AssetGUID makeAssetGuid(const std::filesystem::path& sourcePath, const std::string_view discriminator) noexcept
        {
            const std::string path = sourcePath.lexically_normal().generic_string();
            AssetGUID guid{ 14695981039346656037ull, 1099511628211ull };
            hashBytes(guid.high, path.data(), path.size());
            hashBytes(guid.high, discriminator.data(), discriminator.size());
            hashBytes(guid.low, discriminator.data(), discriminator.size());
            hashBytes(guid.low, path.data(), path.size());
            if (!guid.isValid()) guid.low = 1;
            return guid;
        }

        bool decompose(const Matrix& matrix, ozz::math::Transform& transform)
        {
            Matrix decomposedMatrix = matrix;
            Vector3 scale;
            Quaternion rotation;
            Vector3 translation;
            if (!decomposedMatrix.Decompose(scale, rotation, translation))
                return false;
            rotation.Normalize();
            transform.translation = { translation.x, translation.y, translation.z };
            transform.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
            transform.scale = { scale.x, scale.y, scale.z };
            return true;
        }

        bool buildJoint(const SkeletonResource& source, const std::size_t index, ozz::animation::offline::RawSkeleton::Joint& destination)
        {
            const Bone& bone = source.bones[index];
            destination.name = bone.name;
            if (!decompose(bone.localBindTransform, destination.transform))
                return false;

            std::size_t childCount = 0;
            for (const Bone& candidate : source.bones)
                childCount += candidate.parentIndex == static_cast<std::int32_t>(index) ? 1u : 0u;
            destination.children.resize(childCount);

            std::size_t childIndex = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < source.bones.size(); ++candidateIndex)
            {
                if (source.bones[candidateIndex].parentIndex == static_cast<std::int32_t>(index)
                    && !buildJoint(source, candidateIndex, destination.children[childIndex++]))
                    return false;
            }
            return true;
        }
    }

    SkeletonSignature AnimationAssetBuilder::calculateSignature(const std::span<const std::string> hierarchyPaths, const std::span<const std::int32_t> parentIndices) noexcept
    {
        if (hierarchyPaths.size() != parentIndices.size() || hierarchyPaths.empty())
            return {};

        SkeletonSignature result{ 14695981039346656037ull, 1099511628211ull };
        for (std::size_t index = 0; index < hierarchyPaths.size(); ++index)
        {
            hashBytes(result.high, hierarchyPaths[index].data(), hierarchyPaths[index].size());
            hashBytes(result.high, &parentIndices[index], sizeof(parentIndices[index]));
            hashBytes(result.low, &parentIndices[index], sizeof(parentIndices[index]));
            hashBytes(result.low, hierarchyPaths[index].data(), hierarchyPaths[index].size());
        }
        return result;
    }

    bool AnimationAssetBuilder::buildSkeleton(const SkeletonResource& source, const std::filesystem::path& sourcePath, SkeletonAsset& destination) const
    {
        if (source.bones.empty() || source.bones.size() > MAX_SKINNING_BONES)
            return false;

        SkeletonAsset result;
        result.guid = makeAssetGuid(sourcePath, "skeleton");
        result.name = sourcePath.stem().string();
        result.sourcePath = sourcePath;
        result.jointNames.reserve(source.bones.size());
        result.parentIndices.reserve(source.bones.size());
        result.hierarchyPaths.reserve(source.bones.size());
        result.inverseBindPoses.reserve(source.bones.size());
        result.localBindTransforms.reserve(source.bones.size());

        std::size_t rootCount = 0;
        for (std::size_t index = 0; index < source.bones.size(); ++index)
        {
            const Bone& bone = source.bones[index];
            if (bone.index != index || bone.name.empty() || !result.jointLookup.emplace(bone.name, static_cast<std::uint32_t>(index)).second)
                return false;
            if (bone.parentIndex >= static_cast<std::int32_t>(index) || bone.parentIndex < -1)
                return false;

            result.jointNames.push_back(bone.name);
            result.parentIndices.push_back(bone.parentIndex);
            result.inverseBindPoses.push_back(bone.inverseBindPose);
            result.localBindTransforms.push_back(bone.localBindTransform);
            if (bone.parentIndex < 0)
            {
                result.hierarchyPaths.push_back(bone.name);
                ++rootCount;
            }
            else
            {
                result.hierarchyPaths.push_back(result.hierarchyPaths[static_cast<std::size_t>(bone.parentIndex)] + "/" + bone.name);
            }
        }

        ozz::animation::offline::RawSkeleton rawSkeleton;
        rawSkeleton.roots.resize(rootCount);
        std::size_t rootIndex = 0;
        for (std::size_t index = 0; index < source.bones.size(); ++index)
        {
            if (source.bones[index].parentIndex < 0 && !buildJoint(source, index, rawSkeleton.roots[rootIndex++]))
                return false;
        }
        if (!rawSkeleton.Validate())
            return false;

        ozz::animation::offline::SkeletonBuilder builder;
        auto runtimeSkeleton = builder(rawSkeleton);
        if (runtimeSkeleton == nullptr || runtimeSkeleton->num_joints() != static_cast<int>(source.bones.size()))
            return false;

        result.signature = calculateSignature(result.hierarchyPaths, result.parentIndices);
        result.skeleton = std::move(*runtimeSkeleton);
        destination = std::move(result);
        return true;
    }

    bool AnimationAssetBuilder::buildClip(const AnimationResource& source, const SkeletonAsset& skeleton, const std::filesystem::path& sourcePath, AnimationClipAsset& destination) const
    {
        if (source.duration <= 0.0f || skeleton.skeleton.num_joints() == 0)
            return false;

        std::unordered_map<std::string_view, const AnimationChannel*> channels;
        channels.reserve(source.channels.size());
        for (const AnimationChannel& channel : source.channels)
        {
            if (!channels.emplace(channel.nodeName, &channel).second)
                return false;
        }

        ozz::animation::offline::RawAnimation rawAnimation;
        rawAnimation.name = source.name;
        rawAnimation.duration = source.duration;
        rawAnimation.tracks.resize(skeleton.jointNames.size());
        for (std::size_t index = 0; index < skeleton.jointNames.size(); ++index)
        {
            Vector3 bindScale;
            Quaternion bindRotation;
            Vector3 bindTranslation;
            Matrix localBindTransform = skeleton.localBindTransforms[index];
            if (!localBindTransform.Decompose(bindScale, bindRotation, bindTranslation))
            {
                bindScale = Vector3::One;
                bindRotation = Quaternion::Identity;
                bindTranslation = Vector3::Zero;
            }

            auto& track = rawAnimation.tracks[index];
            const auto found = channels.find(skeleton.jointNames[index]);
            const AnimationChannel* channel = found == channels.end() ? nullptr : found->second;
            if (channel == nullptr || channel->positions.empty())
                track.translations.push_back({ 0.0f, { bindTranslation.x, bindTranslation.y, bindTranslation.z } });
            else
                for (const AnimationKeyPosition& key : channel->positions)
                    track.translations.push_back({ key.time, { key.value.x, key.value.y, key.value.z } });
            if (channel == nullptr || channel->rotations.empty())
                track.rotations.push_back({ 0.0f, { bindRotation.x, bindRotation.y, bindRotation.z, bindRotation.w } });
            else
                for (const AnimationKeyRotation& key : channel->rotations)
                {
                    Quaternion rotation = key.value;
                    rotation.Normalize();
                    track.rotations.push_back({ key.time, { rotation.x, rotation.y, rotation.z, rotation.w } });
                }
            if (channel == nullptr || channel->scales.empty())
                track.scales.push_back({ 0.0f, { bindScale.x, bindScale.y, bindScale.z } });
            else
                for (const AnimationKeyScale& key : channel->scales)
                    track.scales.push_back({ key.time, { key.value.x, key.value.y, key.value.z } });
        }
        if (!rawAnimation.Validate())
            return false;

        ozz::animation::offline::AnimationBuilder builder;
        auto runtimeAnimation = builder(rawAnimation);
        if (runtimeAnimation == nullptr)
            return false;

        AnimationClipAsset result;
        result.guid = makeAssetGuid(sourcePath, std::string("clip/") + source.name);
        result.name = source.name.empty() ? sourcePath.stem().string() : source.name;
        result.skeletonGuid = skeleton.guid;
        result.skeletonSignature = skeleton.signature;
        result.animation = std::move(*runtimeAnimation);
        result.duration = result.animation.duration();
        result.sourcePath = sourcePath;
        result.sourceClipName = source.name;
        destination = std::move(result);
        return true;
    }
}