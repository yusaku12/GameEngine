#include "Pch.h"
#include "Assets\Model\Import\AssimpModelImporter.h"
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Engine
{
    namespace
    {
        Matrix convertMatrix(const aiMatrix4x4& value)
        {
            const DirectX::XMFLOAT4X4 converted{
                value.a1, value.a2, value.a3, value.a4,
                value.b1, value.b2, value.b3, value.b4,
                value.c1, value.c2, value.c3, value.c4,
                value.d1, value.d2, value.d3, value.d4 };
            return Matrix(converted);
        }

        Vector3 convertVector(const aiVector3D& value)
        {
            return Vector3(value.x, value.y, value.z);
        }

        std::string texturePath(const aiMaterial* material, aiTextureType type, const std::filesystem::path& modelPath)
        {
            aiString path;
            if (material == nullptr || material->GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0 || path.C_Str()[0] == '*')
                return {};

            return (modelPath.parent_path() / std::filesystem::path(path.C_Str())).lexically_normal().generic_string();
        }

        void addBoneInfluence(ModelVertex& vertex, std::uint32_t boneIndex, float weight)
        {
            std::array<std::pair<float, std::uint32_t>, MAX_BONE_INFLUENCES + 1> influences{};
            influences[0] = { vertex.boneWeights.x, vertex.boneIndices[0] };
            influences[1] = { vertex.boneWeights.y, vertex.boneIndices[1] };
            influences[2] = { vertex.boneWeights.z, vertex.boneIndices[2] };
            influences[3] = { vertex.boneWeights.w, vertex.boneIndices[3] };
            influences[MAX_BONE_INFLUENCES] = { weight, boneIndex };
            std::sort(influences.begin(), influences.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

            vertex.boneWeights = Vector4(influences[0].first, influences[1].first, influences[2].first, influences[3].first);
            for (std::size_t index = 0; index < MAX_BONE_INFLUENCES; ++index)
                vertex.boneIndices[index] = static_cast<std::uint16_t>(influences[index].second);
        }

        void normalizeBoneWeights(ModelVertex& vertex)
        {
            const float total = vertex.boneWeights.x + vertex.boneWeights.y + vertex.boneWeights.z + vertex.boneWeights.w;
            if (total > EPSILON)
                vertex.boneWeights /= total;
        }
    }

    std::shared_ptr<ModelResource> AssimpModelImporter::importModel(const std::filesystem::path& path) const
    {
        if (!std::filesystem::exists(path))
        {
            LOG_ERROR_CAT("ModelImporter", "Import failed. File not found: {}", path.string());
            return nullptr;
        }

        constexpr unsigned int importFlags = aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_ImproveCacheLocality |
            aiProcess_LimitBoneWeights |
            aiProcess_ValidateDataStructure |
            aiProcess_SortByPType;

        LOG_INFO_CAT("ModelImporter", "Import started: {}", path.string());
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path.string(), importFlags);
        if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
        {
            LOG_ERROR_CAT("ModelImporter", "Import failed. Path: {}. Assimp Error: {}", path.string(), importer.GetErrorString());
            return nullptr;
        }

        auto model = std::make_shared<ModelResource>();
        model->sourcePath = path;
        processScene(scene, *model);

        LOG_INFO_CAT("ModelImporter", "Import succeeded. Meshes: {}, Vertices: {}, Indices: {}, Materials: {}, Bones: {}, Animations: {}",
            model->meshes.size(),
            [&model] { std::size_t count = 0; for (const auto& mesh : model->meshes) count += mesh.vertices.size(); return count; }(),
            [&model] { std::size_t count = 0; for (const auto& mesh : model->meshes) count += mesh.indices.size(); return count; }(),
            model->materials.size(),
            model->skeleton ? model->skeleton->bones.size() : 0,
            model->animations.size());
        return model;
    }

    void AssimpModelImporter::processScene(const aiScene* scene, ModelResource& model) const
    {
        model.materials.reserve(scene->mNumMaterials);
        for (unsigned int index = 0; index < scene->mNumMaterials; ++index)
            processMaterial(scene->mMaterials[index], model);

        processSkeleton(scene, model);
        processNode(scene->mRootNode, scene, model, Matrix::Identity);
        processAnimations(scene, model);

        bool hasBounds = false;
        for (const MeshResource& mesh : model.meshes)
        {
            if (mesh.vertices.empty())
                continue;
            if (!hasBounds)
            {
                model.boundingBox = mesh.boundingBox;
                hasBounds = true;
                continue;
            }
            model.boundingBox = makeAABB(
                Vector3::Min(minimumPointOf(model.boundingBox), minimumPointOf(mesh.boundingBox)),
                Vector3::Max(maximumPointOf(model.boundingBox), maximumPointOf(mesh.boundingBox)));
        }
        if (hasBounds)
            BoundingSphere::CreateFromBoundingBox(model.boundingSphere, model.boundingBox);
    }

    void AssimpModelImporter::processNode(const aiNode* node, const aiScene* scene, ModelResource& model, const Matrix& parentTransform) const
    {
        processNodeInternal(node, scene, model, -1, parentTransform);
    }

    void AssimpModelImporter::processNodeInternal(const aiNode* node, const aiScene* scene, ModelResource& model, std::int32_t parentIndex, const Matrix& parentTransform) const
    {
        if (node == nullptr)
            return;

        const std::uint32_t nodeIndex = static_cast<std::uint32_t>(model.nodes.size());
        model.nodes.push_back({});
        ModelNode& modelNode = model.nodes.back();
        modelNode.name = node->mName.C_Str();
        modelNode.parentIndex = parentIndex;
        modelNode.localTransform = convertMatrix(node->mTransformation);
        const Matrix worldTransform = modelNode.localTransform * parentTransform;

        for (unsigned int index = 0; index < node->mNumMeshes; ++index)
            modelNode.meshIndices.push_back(processMesh(scene->mMeshes[node->mMeshes[index]], scene, model));

        if (parentIndex >= 0)
            model.nodes[static_cast<std::size_t>(parentIndex)].children.push_back(nodeIndex);

        for (unsigned int index = 0; index < node->mNumChildren; ++index)
            processNodeInternal(node->mChildren[index], scene, model, static_cast<std::int32_t>(nodeIndex), worldTransform);
    }

    std::uint32_t AssimpModelImporter::processMesh(const aiMesh* mesh, const aiScene* scene, ModelResource& model) const
    {
        static_cast<void>(scene);
        const std::uint32_t meshIndex = static_cast<std::uint32_t>(model.meshes.size());
        model.meshes.push_back({});
        MeshResource& resource = model.meshes.back();
        resource.name = mesh->mName.C_Str();
        resource.vertices.resize(mesh->mNumVertices);

        for (unsigned int index = 0; index < mesh->mNumVertices; ++index)
        {
            ModelVertex& vertex = resource.vertices[index];
            vertex.position = convertVector(mesh->mVertices[index]);
            if (mesh->HasNormals()) vertex.normal = convertVector(mesh->mNormals[index]);
            if (mesh->HasTangentsAndBitangents())
            {
                vertex.tangent = convertVector(mesh->mTangents[index]);
                vertex.bitangent = convertVector(mesh->mBitangents[index]);
            }
            if (mesh->HasTextureCoords(0)) vertex.texCoord = Vector2(mesh->mTextureCoords[0][index].x, mesh->mTextureCoords[0][index].y);
            if (mesh->HasVertexColors(0))
            {
                const aiColor4D& color = mesh->mColors[0][index];
                vertex.color = Vector4(color.r, color.g, color.b, color.a);
            }
        }

        for (unsigned int index = 0; index < mesh->mNumBones; ++index)
        {
            const aiBone* bone = mesh->mBones[index];
            if (!model.skeleton)
                break;
            const auto boneIt = model.skeleton->boneMap.find(bone->mName.C_Str());
            if (boneIt == model.skeleton->boneMap.end() || boneIt->second > std::numeric_limits<std::uint16_t>::max())
                continue;
            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
            {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                if (weight.mVertexId < resource.vertices.size())
                    addBoneInfluence(resource.vertices[weight.mVertexId], boneIt->second, weight.mWeight);
            }
        }
        for (ModelVertex& vertex : resource.vertices)
            normalizeBoneWeights(vertex);

        resource.indices.reserve(mesh->mNumFaces * 3);
        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3) continue;
            resource.indices.insert(resource.indices.end(), face.mIndices, face.mIndices + 3);
        }
        resource.subMeshes.push_back({ 0, static_cast<std::uint32_t>(resource.indices.size()), mesh->mMaterialIndex });
        if (!resource.vertices.empty())
        {
            resource.boundingBox = makeAABB(resource.vertices[0].position, resource.vertices[0].position);
            for (const ModelVertex& vertex : resource.vertices)
            {
                const Vector3 minimum = minimumPointOf(resource.boundingBox);
                const Vector3 maximum = maximumPointOf(resource.boundingBox);
                resource.boundingBox = makeAABB(Vector3::Min(minimum, vertex.position), Vector3::Max(maximum, vertex.position));
            }
            BoundingSphere::CreateFromBoundingBox(resource.boundingSphere, resource.boundingBox);
        }
        return meshIndex;
    }

    std::uint32_t AssimpModelImporter::processMaterial(const aiMaterial* material, ModelResource& model) const
    {
        MaterialResource resource;
        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            resource.name = name.C_Str();

        aiColor4D color;
        if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
            resource.baseColor = Vector4(color.r, color.g, color.b, color.a);

        material->Get(AI_MATKEY_OPACITY, resource.opacity);
        resource.textures.baseColor = texturePath(material, aiTextureType_DIFFUSE, model.sourcePath);
        resource.textures.normal = texturePath(material, aiTextureType_NORMALS, model.sourcePath);
        resource.textures.ambientOcclusion = texturePath(material, aiTextureType_LIGHTMAP, model.sourcePath);
        resource.textures.emissive = texturePath(material, aiTextureType_EMISSIVE, model.sourcePath);
        resource.textures.opacity = texturePath(material, aiTextureType_OPACITY, model.sourcePath);

        model.materials.push_back(std::move(resource));
        return static_cast<std::uint32_t>(model.materials.size() - 1);
    }

    void AssimpModelImporter::processSkeleton(const aiScene* scene, ModelResource& model) const
    {
        SkeletonResource skeleton;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
            {
                const aiBone* sourceBone = mesh->mBones[boneIndex];
                const std::string name = sourceBone->mName.C_Str();
                if (skeleton.boneMap.contains(name))
                    continue;

                const std::uint32_t index = static_cast<std::uint32_t>(skeleton.bones.size());
                skeleton.boneMap.emplace(name, index);
                skeleton.bones.push_back({ index, name, -1, convertMatrix(sourceBone->mOffsetMatrix), Matrix::Identity });
            }
        }
        if (skeleton.bones.empty())
            return;

        model.skeleton = std::move(skeleton);
    }

    void AssimpModelImporter::processAnimations(const aiScene* scene, ModelResource& model) const
    {
        constexpr float defaultTicksPerSecond = 25.0f;
        model.animations.reserve(scene->mNumAnimations);
        for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
        {
            const aiAnimation* source = scene->mAnimations[animationIndex];
            AnimationResource animation;
            animation.name = source->mName.C_Str();
            animation.ticksPerSecond = source->mTicksPerSecond > 0.0 ? static_cast<float>(source->mTicksPerSecond) : defaultTicksPerSecond;
            animation.duration = static_cast<float>(source->mDuration / animation.ticksPerSecond);
            animation.channels.reserve(source->mNumChannels);
            for (unsigned int channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex)
            {
                const aiNodeAnim* sourceChannel = source->mChannels[channelIndex];
                AnimationChannel channel;
                channel.nodeName = sourceChannel->mNodeName.C_Str();
                channel.positions.reserve(sourceChannel->mNumPositionKeys);
                for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumPositionKeys; ++keyIndex)
                    channel.positions.push_back({ convertVector(sourceChannel->mPositionKeys[keyIndex].mValue), static_cast<float>(sourceChannel->mPositionKeys[keyIndex].mTime / animation.ticksPerSecond) });
                channel.rotations.reserve(sourceChannel->mNumRotationKeys);
                for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumRotationKeys; ++keyIndex)
                {
                    const aiQuaternion& value = sourceChannel->mRotationKeys[keyIndex].mValue;
                    channel.rotations.push_back({ Quaternion(value.x, value.y, value.z, value.w), static_cast<float>(sourceChannel->mRotationKeys[keyIndex].mTime / animation.ticksPerSecond) });
                }
                channel.scales.reserve(sourceChannel->mNumScalingKeys);
                for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumScalingKeys; ++keyIndex)
                    channel.scales.push_back({ convertVector(sourceChannel->mScalingKeys[keyIndex].mValue), static_cast<float>(sourceChannel->mScalingKeys[keyIndex].mTime / animation.ticksPerSecond) });
                animation.channels.push_back(std::move(channel));
            }
            model.animations.push_back(std::move(animation));
        }
    }
} // namespace Engine