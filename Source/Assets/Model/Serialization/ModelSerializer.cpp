#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Model_generated.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        Vec2 toFlat(const Vector2& value) { return Vec2(value.x, value.y); }
        Vec3 toFlat(const Vector3& value) { return Vec3(value.x, value.y, value.z); }
        Vec4 toFlat(const Vector4& value) { return Vec4(value.x, value.y, value.z, value.w); }

        Vector2 toEngine(const Vec2* value) { return value ? Vector2(value->x(), value->y()) : Vector2::Zero; }
        Vector3 toEngine(const Vec3* value) { return value ? Vector3(value->x(), value->y(), value->z()) : Vector3::Zero; }
        Vector4 toEngine(const Vec4* value) { return value ? Vector4(value->x(), value->y(), value->z(), value->w()) : Vector4::Zero; }

        flatbuffers::Offset<Bounds> createBounds(flatbuffers::FlatBufferBuilder& builder, const AABB& box, const BoundingSphere& sphere)
        {
            const Vec3 boxCenter = toFlat(Vector3(box.Center));
            const Vec3 boxExtents = toFlat(Vector3(box.Extents));
            const Vec3 sphereCenter = toFlat(Vector3(sphere.Center));
            return CreateBounds(builder, &boxCenter, &boxExtents, &sphereCenter, sphere.Radius);
        }

        flatbuffers::Offset<flatbuffers::Vector<float>> createMatrix(flatbuffers::FlatBufferBuilder& builder, const Matrix& matrix)
        {
            return builder.CreateVector(&matrix._11, 16);
        }

        bool readMatrix(const flatbuffers::Vector<float>* values, Matrix& matrix)
        {
            if (values == nullptr || values->size() != 16)
                return false;
            matrix = Matrix(
                values->Get(0), values->Get(1), values->Get(2), values->Get(3),
                values->Get(4), values->Get(5), values->Get(6), values->Get(7),
                values->Get(8), values->Get(9), values->Get(10), values->Get(11),
                values->Get(12), values->Get(13), values->Get(14), values->Get(15));
            return true;
        }

        flatbuffers::Offset<MaterialTexturePaths> createTexturePaths(flatbuffers::FlatBufferBuilder& builder, const Engine::MaterialTexturePaths& paths)
        {
            return CreateMaterialTexturePaths(builder,
                builder.CreateString(paths.baseColor), builder.CreateString(paths.normal),
                builder.CreateString(paths.metallic), builder.CreateString(paths.roughness),
                builder.CreateString(paths.metallicRoughness), builder.CreateString(paths.ambientOcclusion),
                builder.CreateString(paths.emissive), builder.CreateString(paths.opacity));
        }

        flatbuffers::Offset<Material> createMaterial(flatbuffers::FlatBufferBuilder& builder, const MaterialResource& material)
        {
            const Vec4 baseColor = toFlat(material.baseColor);
            const Vec3 emissive = toFlat(material.emissive);
            return CreateMaterial(builder, builder.CreateString(material.name), &baseColor,
                material.metallic, material.roughness, &emissive, material.opacity,
                createTexturePaths(builder, material.textures));
        }

        flatbuffers::Offset<ModelVertex> createVertex(flatbuffers::FlatBufferBuilder& builder, const Engine::ModelVertex& vertex)
        {
            const Vec3 position = toFlat(vertex.position);
            const Vec3 normal = toFlat(vertex.normal);
            const Vec3 tangent = toFlat(vertex.tangent);
            const Vec3 bitangent = toFlat(vertex.bitangent);
            const Vec2 texCoord = toFlat(vertex.texCoord);
            const Vec4 color = toFlat(vertex.color);
            const Vec4 weights = toFlat(vertex.boneWeights);
            return CreateModelVertex(builder, &position, &normal, &tangent, &bitangent, &texCoord, &color,
                builder.CreateVector(vertex.boneIndices.data(), vertex.boneIndices.size()), &weights);
        }

        flatbuffers::Offset<Mesh> createMesh(flatbuffers::FlatBufferBuilder& builder, const Engine::MeshResource& mesh)
        {
            std::vector<flatbuffers::Offset<ModelVertex>> vertices;
            vertices.reserve(mesh.vertices.size());
            for (const Engine::ModelVertex& vertex : mesh.vertices)
                vertices.push_back(createVertex(builder, vertex));

            std::vector<flatbuffers::Offset<SubMesh>> subMeshes;
            subMeshes.reserve(mesh.subMeshes.size());
            for (const SubMeshResource& subMesh : mesh.subMeshes)
                subMeshes.push_back(CreateSubMesh(builder, subMesh.indexStart, subMesh.indexCount, subMesh.materialIndex));

            return CreateMesh(builder, builder.CreateString(mesh.name), builder.CreateVector(vertices),
                builder.CreateVector(mesh.indices), builder.CreateVector(subMeshes),
                createBounds(builder, mesh.boundingBox, mesh.boundingSphere));
        }

        flatbuffers::Offset<Bone> createBone(flatbuffers::FlatBufferBuilder& builder, const Engine::Bone& bone)
        {
            return CreateBone(builder, bone.index, builder.CreateString(bone.name), bone.parentIndex,
                createMatrix(builder, bone.inverseBindPose), createMatrix(builder, bone.localBindTransform));
        }

        flatbuffers::Offset<Skeleton> createSkeleton(flatbuffers::FlatBufferBuilder& builder, const Engine::SkeletonResource& skeleton)
        {
            std::vector<flatbuffers::Offset<Bone>> bones;
            bones.reserve(skeleton.bones.size());
            for (const Engine::Bone& bone : skeleton.bones)
                bones.push_back(createBone(builder, bone));
            return CreateSkeleton(builder, builder.CreateVector(bones));
        }

        flatbuffers::Offset<ModelNode> createNode(flatbuffers::FlatBufferBuilder& builder, const Engine::ModelNode& node)
        {
            return CreateModelNode(builder, builder.CreateString(node.name), node.parentIndex,
                createMatrix(builder, node.localTransform), builder.CreateVector(node.children), builder.CreateVector(node.meshIndices));
        }

        flatbuffers::Offset<Animation> createAnimation(flatbuffers::FlatBufferBuilder& builder, const Engine::AnimationResource& animation)
        {
            std::vector<flatbuffers::Offset<AnimationChannel>> channels;
            channels.reserve(animation.channels.size());
            for (const Engine::AnimationChannel& channel : animation.channels) {
                std::vector<flatbuffers::Offset<PositionKey>> positions;
                for (const AnimationKeyPosition& key : channel.positions) {
                    const Vec3 value = toFlat(key.value);
                    positions.push_back(CreatePositionKey(builder, &value, key.time));
                }
                std::vector<flatbuffers::Offset<RotationKey>> rotations;
                for (const AnimationKeyRotation& key : channel.rotations) {
                    const Vec4 value = toFlat(Vector4(key.value.x, key.value.y, key.value.z, key.value.w));
                    rotations.push_back(CreateRotationKey(builder, &value, key.time));
                }
                std::vector<flatbuffers::Offset<ScaleKey>> scales;
                for (const AnimationKeyScale& key : channel.scales) {
                    const Vec3 value = toFlat(key.value);
                    scales.push_back(CreateScaleKey(builder, &value, key.time));
                }
                channels.push_back(CreateAnimationChannel(builder, builder.CreateString(channel.nodeName),
                    builder.CreateVector(positions), builder.CreateVector(rotations), builder.CreateVector(scales)));
            }
            return CreateAnimation(builder, builder.CreateString(animation.name), animation.duration,
                animation.ticksPerSecond, builder.CreateVector(channels));
        }

        bool readBounds(const Bounds* bounds, AABB& box, BoundingSphere& sphere)
        {
            if (bounds == nullptr)
                return false;
            box.Center = toEngine(bounds->box_center());
            box.Extents = toEngine(bounds->box_extents());
            sphere.Center = toEngine(bounds->sphere_center());
            sphere.Radius = bounds->sphere_radius();
            return true;
        }

        bool readMesh(const Mesh* source, MeshResource& mesh)
        {
            if (source == nullptr || source->name() == nullptr || !readBounds(source->bounds(), mesh.boundingBox, mesh.boundingSphere))
                return false;
            mesh.name = source->name()->str();
            if (const auto* indices = source->indices())
                mesh.indices.assign(indices->begin(), indices->end());
            if (const auto* subMeshes = source->sub_meshes()) {
                mesh.subMeshes.reserve(subMeshes->size());
                for (const SubMesh* subMesh : *subMeshes) {
                    if (subMesh == nullptr)
                        return false;
                    mesh.subMeshes.push_back({ subMesh->index_start(), subMesh->index_count(), subMesh->material_index() });
                }
            }
            if (const auto* vertices = source->vertices()) {
                mesh.vertices.reserve(vertices->size());
                for (const ModelVertex* sourceVertex : *vertices) {
                    if (sourceVertex == nullptr || sourceVertex->bone_indices() == nullptr)
                        return false;
                    Engine::ModelVertex vertex;
                    vertex.position = toEngine(sourceVertex->position());
                    vertex.normal = toEngine(sourceVertex->normal());
                    vertex.tangent = toEngine(sourceVertex->tangent());
                    vertex.bitangent = toEngine(sourceVertex->bitangent());
                    vertex.texCoord = toEngine(sourceVertex->tex_coord());
                    vertex.color = toEngine(sourceVertex->color());
                    vertex.boneWeights = toEngine(sourceVertex->bone_weights());
                    if (sourceVertex->bone_indices()->size() != vertex.boneIndices.size())
                        return false;
                    std::copy(sourceVertex->bone_indices()->begin(), sourceVertex->bone_indices()->end(), vertex.boneIndices.begin());
                    mesh.vertices.push_back(vertex);
                }
            }
            return true;
        }

        bool readSkeleton(const Skeleton* source, SkeletonResource& skeleton)
        {
            if (source == nullptr || source->bones() == nullptr)
                return false;
            skeleton.bones.reserve(source->bones()->size());
            for (const Bone* sourceBone : *source->bones()) {
                if (sourceBone == nullptr || sourceBone->name() == nullptr)
                    return false;
                Engine::Bone bone;
                bone.index = sourceBone->index();
                bone.name = sourceBone->name()->str();
                bone.parentIndex = sourceBone->parent_index();
                if (!readMatrix(sourceBone->inverse_bind_pose(), bone.inverseBindPose) ||
                    !readMatrix(sourceBone->local_bind_transform(), bone.localBindTransform))
                    return false;
                skeleton.boneMap.emplace(bone.name, bone.index);
                skeleton.bones.push_back(std::move(bone));
            }
            return true;
        }

        bool readNode(const ::Engine::Serialization::ModelNode* source, ::Engine::ModelNode& node)
        {
            if (source == nullptr || source->name() == nullptr || !readMatrix(source->local_transform(), node.localTransform))
                return false;
            node.name = source->name()->str();
            node.parentIndex = source->parent_index();
            if (const auto* children = source->children())
                node.children.assign(children->begin(), children->end());
            if (const auto* meshIndices = source->mesh_indices())
                node.meshIndices.assign(meshIndices->begin(), meshIndices->end());
            return true;
        }

        bool readAnimation(const Animation* source, AnimationResource& animation)
        {
            if (source == nullptr || source->name() == nullptr)
                return false;
            animation.name = source->name()->str();
            animation.duration = source->duration();
            animation.ticksPerSecond = source->ticks_per_second();
            if (const auto* channels = source->channels()) {
                animation.channels.reserve(channels->size());
                for (const AnimationChannel* sourceChannel : *channels) {
                    if (sourceChannel == nullptr || sourceChannel->node_name() == nullptr)
                        return false;
                    Engine::AnimationChannel channel;
                    channel.nodeName = sourceChannel->node_name()->str();
                    if (const auto* positions = sourceChannel->positions()) {
                        for (const PositionKey* sourceKey : *positions) {
                            if (sourceKey == nullptr)
                                return false;
                            channel.positions.push_back({ toEngine(sourceKey->value()), sourceKey->time() });
                        }
                    }
                    if (const auto* rotations = sourceChannel->rotations()) {
                        for (const RotationKey* sourceKey : *rotations) {
                            if (sourceKey == nullptr)
                                return false;
                            const Vector4 value = toEngine(sourceKey->value());
                            channel.rotations.push_back({ Quaternion(value.x, value.y, value.z, value.w), sourceKey->time() });
                        }
                    }
                    if (const auto* scales = sourceChannel->scales()) {
                        for (const ScaleKey* sourceKey : *scales) {
                            if (sourceKey == nullptr)
                                return false;
                            channel.scales.push_back({ toEngine(sourceKey->value()), sourceKey->time() });
                        }
                    }
                    animation.channels.push_back(std::move(channel));
                }
            }
            return true;
        }
    }

    bool ModelSerializer::save(const std::filesystem::path& path, const ModelResource& model) const
    {
        flatbuffers::FlatBufferBuilder builder(1024);
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_MODEL_VERSION, 0);
        std::vector<flatbuffers::Offset<Mesh>> meshes;
        for (const MeshResource& mesh : model.meshes)
            meshes.push_back(createMesh(builder, mesh));
        std::vector<flatbuffers::Offset<Material>> materials;
        for (const MaterialResource& material : model.materials)
            materials.push_back(createMaterial(builder, material));
        std::vector<flatbuffers::Offset<Animation>> animations;
        for (const AnimationResource& animation : model.animations)
            animations.push_back(createAnimation(builder, animation));
        std::vector<flatbuffers::Offset<ModelNode>> nodes;
        for (const ::Engine::ModelNode& node : model.nodes)
            nodes.push_back(createNode(builder, node));

        const auto root = CreateModelFile(builder, header, builder.CreateVector(meshes), builder.CreateVector(materials),
            model.skeleton ? createSkeleton(builder, *model.skeleton) : 0, builder.CreateVector(animations),
            builder.CreateVector(nodes), createBounds(builder, model.boundingBox, model.boundingSphere),
            builder.CreateString(model.sourcePath.generic_string()));
        FinishModelFileBuffer(builder, root);
        return FlatBufferWriter{}.saveAtomic(path, std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
    }

    bool ModelSerializer::load(const std::filesystem::path& path, ModelResource& model) const
    {
        FlatBufferReader reader;
        if (!reader.open(path))
            return false;
        if (!reader.hasIdentifier("MODL")) {
            LOG_ERROR("FlatBuffers model has an invalid file identifier: {}", path.string());
            return false;
        }
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyModelFileBuffer(verifier)) {
            LOG_ERROR("FlatBuffers model verification failed: {}", path.string());
            return false;
        }
        const ModelFile* source = GetModelFile(reader.data());
        if (source == nullptr || source->header() == nullptr || source->header()->schema_version() != CURRENT_SCHEMA_VERSION ||
            source->header()->asset_version() < MINIMUM_SUPPORTED_MODEL_VERSION || source->header()->asset_version() > CURRENT_MODEL_VERSION ||
            source->bounds() == nullptr) {
            LOG_ERROR("Unsupported or invalid model version: {}", path.string());
            return false;
        }
        model = ModelResource{};
        if (!readBounds(source->bounds(), model.boundingBox, model.boundingSphere))
            return false;
        model.sourcePath = source->source_path() ? std::filesystem::path(source->source_path()->str()) : std::filesystem::path{};
        if (const auto* meshes = source->meshes()) {
            model.meshes.reserve(meshes->size());
            for (const Mesh* mesh : *meshes) {
                model.meshes.emplace_back();
                if (!readMesh(mesh, model.meshes.back()))
                    return false;
            }
        }
        if (const auto* materials = source->materials()) {
            for (const Material* sourceMaterial : *materials) {
                if (sourceMaterial == nullptr || sourceMaterial->name() == nullptr)
                    return false;
                MaterialResource material;
                material.name = sourceMaterial->name()->str();
                material.baseColor = toEngine(sourceMaterial->base_color());
                material.metallic = sourceMaterial->metallic();
                material.roughness = sourceMaterial->roughness();
                material.emissive = toEngine(sourceMaterial->emissive());
                material.opacity = sourceMaterial->opacity();
                const MaterialTexturePaths* textures = sourceMaterial->textures();
                if (textures != nullptr) {
                    material.textures.baseColor = textures->base_color() ? textures->base_color()->str() : "";
                    material.textures.normal = textures->normal() ? textures->normal()->str() : "";
                    material.textures.metallic = textures->metallic() ? textures->metallic()->str() : "";
                    material.textures.roughness = textures->roughness() ? textures->roughness()->str() : "";
                    material.textures.metallicRoughness = textures->metallic_roughness() ? textures->metallic_roughness()->str() : "";
                    material.textures.ambientOcclusion = textures->ambient_occlusion() ? textures->ambient_occlusion()->str() : "";
                    material.textures.emissive = textures->emissive() ? textures->emissive()->str() : "";
                    material.textures.opacity = textures->opacity() ? textures->opacity()->str() : "";
                }
                model.materials.push_back(std::move(material));
            }
        }
        if (source->skeleton() != nullptr) {
            model.skeleton.emplace();
            if (!readSkeleton(source->skeleton(), *model.skeleton))
                return false;
        }
        if (const auto* animations = source->animations()) {
            model.animations.reserve(animations->size());
            for (const Animation* animation : *animations) {
                model.animations.emplace_back();
                if (!readAnimation(animation, model.animations.back()))
                    return false;
            }
        }
        if (const auto* nodes = source->nodes()) {
            model.nodes.reserve(nodes->size());
            for (const ::Engine::Serialization::ModelNode* node : *nodes) {
                model.nodes.emplace_back();
                if (!readNode(node, model.nodes.back()))
                    return false;
            }
        }
        for (const MeshResource& mesh : model.meshes) {
            for (const SubMeshResource& subMesh : mesh.subMeshes) {
                if (subMesh.materialIndex >= model.materials.size() ||
                    static_cast<std::size_t>(subMesh.indexStart) + subMesh.indexCount > mesh.indices.size())
                    return false;
            }
        }
        for (const Engine::ModelNode& node : model.nodes) {
            if (node.parentIndex >= static_cast<std::int32_t>(model.nodes.size()))
                return false;
            for (const std::uint32_t child : node.children)
                if (child >= model.nodes.size())
                    return false;
            for (const std::uint32_t meshIndex : node.meshIndices)
                if (meshIndex >= model.meshes.size())
                    return false;
        }
        return true;
    }
}