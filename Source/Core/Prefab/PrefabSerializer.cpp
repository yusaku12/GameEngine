#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Prefab_generated.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        flatbuffers::Offset<ObjectGUID> createGuid(flatbuffers::FlatBufferBuilder& builder, const Engine::ObjectGUID& guid)
        {
            return CreateObjectGUID(builder, guid.high, guid.low);
        }

        flatbuffers::Offset<Quaternion> createQuaternion(flatbuffers::FlatBufferBuilder& builder, const Engine::Quaternion& value)
        {
            return CreateQuaternion(builder, value.x, value.y, value.z, value.w);
        }

        flatbuffers::Offset<TransformData> createTransform(flatbuffers::FlatBufferBuilder& builder, const Engine::Transform& transform)
        {
            const Vec3 position(transform.getPosition().x, transform.getPosition().y, transform.getPosition().z);
            const Vec3 scale(transform.getScale().x, transform.getScale().y, transform.getScale().z);
            return CreateTransformData(builder, &position, createQuaternion(builder, transform.getRotation()), &scale);
        }

        flatbuffers::Offset<PrefabNodeData> createNode(flatbuffers::FlatBufferBuilder& builder, const PrefabNode& node)
        {
            std::vector<flatbuffers::Offset<flatbuffers::String>> componentTypes;
            componentTypes.reserve(node.componentTypes.size());
            for (const std::string& componentType : node.componentTypes)
                componentTypes.push_back(builder.CreateString(componentType));

            std::vector<flatbuffers::Offset<PrefabNodeData>> children;
            children.reserve(node.children.size());
            for (const PrefabNode& child : node.children)
                children.push_back(createNode(builder, child));

            return CreatePrefabNodeData(builder, createGuid(builder, node.sourceGUID), builder.CreateString(node.name),
                node.active, node.tag, node.layer, createTransform(builder, node.localTransform),
                builder.CreateVector(componentTypes), builder.CreateVector(children));
        }

        bool readNode(const PrefabNodeData& source, PrefabNode& node)
        {
            if (source.source_guid() == nullptr || source.name() == nullptr || source.transform() == nullptr)
                return false;

            node.sourceGUID = Engine::ObjectGUID{ source.source_guid()->high(), source.source_guid()->low() };
            if (!node.sourceGUID.isValid())
                return false;
            node.name = source.name()->str();
            node.active = source.active();
            node.tag = source.tag();
            node.layer = source.layer();

            const TransformData& transform = *source.transform();
            const Vec3* position = transform.position();
            const Quaternion* rotation = transform.rotation();
            const Vec3* scale = transform.scale();
            if (position == nullptr || rotation == nullptr || scale == nullptr)
                return false;
            node.localTransform = Engine::Transform(
                Engine::Vector3(position->x(), position->y(), position->z()),
                Engine::Quaternion(rotation->x(), rotation->y(), rotation->z(), rotation->w()),
                Engine::Vector3(scale->x(), scale->y(), scale->z()));

            node.componentTypes.clear();
            if (const auto* componentTypes = source.component_types())
            {
                node.componentTypes.reserve(componentTypes->size());
                for (const flatbuffers::String* componentType : *componentTypes)
                {
                    if (componentType == nullptr)
                        return false;
                    node.componentTypes.push_back(componentType->str());
                }
            }

            node.children.clear();
            if (const auto* children = source.children())
            {
                node.children.reserve(children->size());
                for (const PrefabNodeData* child : *children)
                {
                    if (child == nullptr)
                        return false;
                    node.children.emplace_back();
                    if (!readNode(*child, node.children.back()))
                        return false;
                }
            }
            return true;
        }
    }

    bool PrefabSerializer::save(const std::filesystem::path& path, const Prefab& prefab) const
    {
        if (path.empty() || !prefab.isValid())
            return false;

        flatbuffers::FlatBufferBuilder builder(1024);
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_PREFAB_VERSION, 0);
        const auto root = CreatePrefabFile(builder, header, createNode(builder, prefab.getRoot()));
        FinishPrefabFileBuffer(builder, root);
        return FlatBufferWriter{}.saveAtomic(path, std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
    }

    bool PrefabSerializer::load(const std::filesystem::path& path, Prefab& prefab) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("PREF"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyPrefabFileBuffer(verifier))
            return false;

        const PrefabFile* source = GetPrefabFile(reader.data());
        if (source == nullptr || source->header() == nullptr || source->root() == nullptr
            || source->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || source->header()->asset_version() != CURRENT_PREFAB_VERSION)
            return false;

        PrefabNode root;
        if (!readNode(*source->root(), root))
            return false;
        prefab.m_root = std::move(root);
        prefab.m_valid = true;
        return true;
    }
} // namespace Engine::Serialization