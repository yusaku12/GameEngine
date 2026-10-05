#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Skeleton_generated.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\Serialization\OzzArchiveUtils.h"
#include "Assets\Animation\Serialization\SkeletonSerializer.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        AssetGuid toFlat(const AssetGUID& value) { return { value.high, value.low }; }
        flatbuffers::Offset<flatbuffers::Vector<float>> createMatrix(flatbuffers::FlatBufferBuilder& builder, const Matrix& matrix)
        {
            return builder.CreateVector(&matrix._11, 16);
        }
        bool readMatrix(const flatbuffers::Vector<float>* source, Matrix& matrix)
        {
            if (source == nullptr || source->size() != 16)
                return false;
            for (const float value : *source)
                if (!std::isfinite(value))
                    return false;
            matrix = Matrix(source->Get(0), source->Get(1), source->Get(2), source->Get(3),
                source->Get(4), source->Get(5), source->Get(6), source->Get(7),
                source->Get(8), source->Get(9), source->Get(10), source->Get(11),
                source->Get(12), source->Get(13), source->Get(14), source->Get(15));
            return true;
        }

        bool isFinite(const Matrix& matrix) noexcept
        {
            return std::isfinite(matrix._11) && std::isfinite(matrix._12)
                && std::isfinite(matrix._13) && std::isfinite(matrix._14)
                && std::isfinite(matrix._21) && std::isfinite(matrix._22)
                && std::isfinite(matrix._23) && std::isfinite(matrix._24)
                && std::isfinite(matrix._31) && std::isfinite(matrix._32)
                && std::isfinite(matrix._33) && std::isfinite(matrix._34)
                && std::isfinite(matrix._41) && std::isfinite(matrix._42)
                && std::isfinite(matrix._43) && std::isfinite(matrix._44);
        }

        bool hasConsistentHierarchy(const std::vector<std::string>& jointNames,
            const std::vector<std::int32_t>& parentIndices, const std::vector<std::string>& hierarchyPaths)
        {
            for (std::size_t index = 0; index < jointNames.size(); ++index)
            {
                const std::int32_t parentIndex = parentIndices[index];
                if (jointNames[index].empty() || hierarchyPaths[index].empty()
                    || parentIndex < -1 || parentIndex >= static_cast<std::int32_t>(index))
                    return false;
                const std::string expectedPath = parentIndex < 0
                    ? jointNames[index]
                    : hierarchyPaths[static_cast<std::size_t>(parentIndex)] + "/" + jointNames[index];
                if (hierarchyPaths[index] != expectedPath)
                    return false;
            }
            return true;
        }
    }

    bool SkeletonSerializer::save(const std::filesystem::path& path, const SkeletonAsset& skeleton) const
    {
        if (!skeleton.guid.isValid() || !skeleton.signature.isValid()
            || skeleton.jointNames.empty() || skeleton.jointNames.size() > MAX_SKINNING_BONES
            || skeleton.jointNames.size() != skeleton.parentIndices.size()
            || skeleton.jointNames.size() != skeleton.hierarchyPaths.size()
            || skeleton.jointNames.size() != skeleton.inverseBindPoses.size()
            || skeleton.jointNames.size() != skeleton.localBindTransforms.size()
            || skeleton.skeleton.num_joints() != static_cast<int>(skeleton.jointNames.size())
            || AnimationAssetBuilder::calculateSignature(skeleton.hierarchyPaths, skeleton.parentIndices) != skeleton.signature
            || !hasConsistentHierarchy(skeleton.jointNames, skeleton.parentIndices, skeleton.hierarchyPaths))
            return false;
        for (std::size_t index = 0; index < skeleton.jointNames.size(); ++index)
        {
            if (!isFinite(skeleton.inverseBindPoses[index]) || !isFinite(skeleton.localBindTransforms[index]))
                return false;
        }

        std::vector<std::uint8_t> archive;
        if (!saveOzzArchive(skeleton.skeleton, archive))
            return false;

        flatbuffers::FlatBufferBuilder builder(archive.size() + 1024);
        std::vector<flatbuffers::Offset<SkeletonJoint>> joints;
        joints.reserve(skeleton.jointNames.size());
        for (std::size_t index = 0; index < skeleton.jointNames.size(); ++index)
            joints.push_back(CreateSkeletonJoint(builder, builder.CreateString(skeleton.jointNames[index]),
                skeleton.parentIndices[index], builder.CreateString(skeleton.hierarchyPaths[index]),
                createMatrix(builder, skeleton.inverseBindPoses[index]), createMatrix(builder, skeleton.localBindTransforms[index])));

        const AssetGuid guid = toFlat(skeleton.guid);
        const auto data = CreateSkeletonAssetData(builder, &guid, builder.CreateString(skeleton.name),
            skeleton.signature.high, skeleton.signature.low, builder.CreateVector(joints), builder.CreateVector(archive),
            calculateArchiveChecksum(archive), builder.CreateString(skeleton.sourcePath.generic_string()));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_SKELETON_VERSION, 0);
        FinishSkeletonFileBuffer(builder, CreateSkeletonFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool SkeletonSerializer::load(const std::filesystem::path& path, SkeletonAsset& skeleton) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("SKEL"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifySkeletonFileBuffer(verifier))
            return false;
        const SkeletonFile* file = GetSkeletonFile(reader.data());
        const SkeletonAssetData* source = file ? file->skeleton() : nullptr;
        if (file == nullptr || file->header() == nullptr || source == nullptr || source->guid() == nullptr
            || source->name() == nullptr || source->joints() == nullptr || source->ozz_archive() == nullptr
            || source->joints()->size() == 0 || source->joints()->size() > MAX_SKINNING_BONES
            || file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() != CURRENT_SKELETON_VERSION)
            return false;

        const auto* blob = source->ozz_archive();
        const std::span archive(blob->data(), blob->size());
        if (calculateArchiveChecksum(archive) != source->archive_checksum())
            return false;

        SkeletonAsset loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        loaded.signature = { source->signature_high(), source->signature_low() };
        loaded.sourcePath = source->source_path() ? source->source_path()->str() : "";
        if (!loaded.guid.isValid() || !loaded.signature.isValid() || !loadOzzArchive(archive, loaded.skeleton)
            || loaded.skeleton.num_joints() != static_cast<int>(source->joints()->size()))
            return false;

        loaded.jointNames.reserve(source->joints()->size());
        loaded.parentIndices.reserve(source->joints()->size());
        loaded.hierarchyPaths.reserve(source->joints()->size());
        loaded.inverseBindPoses.reserve(source->joints()->size());
        loaded.localBindTransforms.reserve(source->joints()->size());
        for (std::uint32_t index = 0; index < source->joints()->size(); ++index)
        {
            const SkeletonJoint* joint = source->joints()->Get(index);
            Matrix inverseBind;
            Matrix localBind;
            if (joint == nullptr || joint->name() == nullptr || joint->hierarchy_path() == nullptr
                || joint->name()->size() == 0 || joint->hierarchy_path()->size() == 0
                || !readMatrix(joint->inverse_bind_pose(), inverseBind)
                || !readMatrix(joint->local_bind_transform(), localBind)
                || joint->parent_index() >= static_cast<std::int32_t>(index) || joint->parent_index() < -1)
                return false;
            loaded.jointNames.push_back(joint->name()->str());
            loaded.parentIndices.push_back(joint->parent_index());
            loaded.hierarchyPaths.push_back(joint->hierarchy_path()->str());
            loaded.inverseBindPoses.push_back(inverseBind);
            loaded.localBindTransforms.push_back(localBind);
            if (!loaded.jointLookup.emplace(loaded.jointNames.back(), index).second)
                return false;
        }
        if (!hasConsistentHierarchy(loaded.jointNames, loaded.parentIndices, loaded.hierarchyPaths)
            || AnimationAssetBuilder::calculateSignature(loaded.hierarchyPaths, loaded.parentIndices) != loaded.signature)
            return false;
        skeleton = std::move(loaded);
        return true;
    }
}