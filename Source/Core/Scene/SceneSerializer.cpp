#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Scene_generated.h"
#include "Core\Logging\Logging.h"
#include "Core\Scene\SceneSerializer.h"
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

        Engine::ObjectGUID readGuid(const Engine::Serialization::ObjectGUID* source) noexcept
        {
            return source == nullptr ? Engine::ObjectGUID{} : Engine::ObjectGUID{ source->high(), source->low() };
        }

        Engine::Vector3 readVector(const Vec3* source, const Engine::Vector3& fallback) noexcept
        {
            return source == nullptr ? fallback : Engine::Vector3(source->x(), source->y(), source->z());
        }
    }

    bool SceneSerializer::save(const std::filesystem::path& path, const Scene& scene) const
    {
        flatbuffers::FlatBufferBuilder builder(1024);
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_SCENE_VERSION, 0);
        std::vector<flatbuffers::Offset<SceneObject>> objects;
        objects.reserve(scene.getGameObjectCount());

        for (const auto& object : scene.getGameObjects())
        {
            std::vector<flatbuffers::Offset<flatbuffers::String>> componentTypes;
            const std::vector<std::string_view> typeNames = object->getComponentTypeNames();
            componentTypes.reserve(typeNames.size());
            for (const std::string_view typeName : typeNames)
                componentTypes.push_back(builder.CreateString(typeName));
            objects.push_back(CreateSceneObject(builder, createGuid(builder, object->getGUID()),
                object->getParent() == nullptr ? 0 : createGuid(builder, object->getParent()->getGUID()),
                builder.CreateString(object->getName()), object->isActiveSelf(), object->getTag(), object->getLayer(),
                createTransform(builder, *object->getTransform()), builder.CreateVector(componentTypes)));
        }

        const auto root = CreateSceneFile(builder, header, createGuid(builder, scene.getGUID()),
            builder.CreateString(scene.getName()), builder.CreateVector(objects));
        FinishSceneFileBuffer(builder, root);
        return FlatBufferWriter{}.saveAtomic(path, std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
    }

    bool SceneSerializer::load(const std::filesystem::path& path, Scene& scene) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("SCNE"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifySceneFileBuffer(verifier))
            return false;
        const SceneFile* source = GetSceneFile(reader.data());
        if (source == nullptr || source->header() == nullptr || source->guid() == nullptr || source->name() == nullptr
            || source->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || source->header()->asset_version() != CURRENT_SCENE_VERSION)
            return false;

        scene.clear();
        scene.setGUID(readGuid(source->guid()));
        scene.setName(source->name()->str());
        std::unordered_map<Engine::ObjectGUID, GameObject*, Engine::ObjectGUIDHash> objects;
        if (const auto* serializedObjects = source->objects())
        {
            for (const SceneObject* serialized : *serializedObjects)
            {
                if (serialized == nullptr || serialized->guid() == nullptr || serialized->name() == nullptr)
                    return false;
                const Engine::ObjectGUID guid = readGuid(serialized->guid());
                GameObject* object = scene.createGameObject(serialized->name()->str(), guid);
                if (object == nullptr || objects.contains(guid))
                    return false;
                object->setActive(serialized->active());
                object->setTag(serialized->tag());
                object->setLayer(serialized->layer());
                const TransformData* transform = serialized->transform();
                if (transform != nullptr)
                {
                    object->getTransform()->setPosition(readVector(transform->position(), Vector3::Zero));
                    const Engine::Serialization::Quaternion* rotation = transform->rotation();
                    object->getTransform()->setRotation(rotation == nullptr
                        ? Engine::Quaternion::Identity : Engine::Quaternion(rotation->x(), rotation->y(), rotation->z(), rotation->w()));
                    object->getTransform()->setScale(readVector(transform->scale(), Vector3::One));
                }
                objects.emplace(guid, object);
            }
            for (const SceneObject* serialized : *serializedObjects)
            {
                const Engine::ObjectGUID guid = readGuid(serialized->guid());
                GameObject* object = objects.at(guid);
                const Engine::ObjectGUID parentGuid = readGuid(serialized->parent_guid());
                if (parentGuid.isValid())
                {
                    const auto parent = objects.find(parentGuid);
                    if (parent == objects.end() || !object->setParent(parent->second, false))
                        return false;
                }
            }
        }
        return true;
    }
} // namespace Engine::Serialization