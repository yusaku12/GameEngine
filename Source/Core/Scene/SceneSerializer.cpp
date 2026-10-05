#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Scene_generated.h"
#include "Core\Logging\Logging.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\ComponentSerialization.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Core\GameObject\ComponentRegistry.h"

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

        bool isFinite(const Engine::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isValidRotation(const Engine::Quaternion& value) noexcept
        {
            const float lengthSquared = value.x * value.x + value.y * value.y
                + value.z * value.z + value.w * value.w;
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w)
                && std::isfinite(lengthSquared) && lengthSquared > std::numeric_limits<float>::epsilon();
        }

        std::vector<flatbuffers::Offset<ComponentData>> createComponents(flatbuffers::FlatBufferBuilder& builder,
            const std::vector<Engine::ComponentSnapshot>& snapshots)
        {
            std::vector<flatbuffers::Offset<ComponentData>> result;
            result.reserve(snapshots.size());
            for (const Engine::ComponentSnapshot& snapshot : snapshots)
                result.push_back(CreateComponentData(builder, builder.CreateString(snapshot.type), snapshot.enabled,
                    snapshot.payloadVersion, builder.CreateVector(snapshot.payload)));
            return result;
        }

        bool readComponents(const flatbuffers::Vector<flatbuffers::Offset<ComponentData>>* source,
            std::vector<Engine::ComponentSnapshot>& snapshots)
        {
            snapshots.clear();
            if (source == nullptr) return true;
            snapshots.reserve(source->size());
            for (const ComponentData* component : *source)
            {
                if (component == nullptr || component->type() == nullptr)
                    return false;
                Engine::ComponentSnapshot snapshot;
                snapshot.type = component->type()->str();
                snapshot.enabled = component->enabled();
                snapshot.payloadVersion = component->payload_version();
                if (const auto* payload = component->payload())
                    snapshot.payload.assign(payload->begin(), payload->end());
                snapshots.push_back(std::move(snapshot));
            }
            return true;
        }

        flatbuffers::Offset<flatbuffers::String> createTagName(
            flatbuffers::FlatBufferBuilder& builder,
            const TagID tag)
        {
            const std::string_view name = TagManager::instance().getName(tag);
            return name.empty() ? flatbuffers::Offset<flatbuffers::String>{}
            : builder.CreateString(name.data(), name.size());
        }
    }

    bool SceneSerializer::save(const std::filesystem::path& path, const Scene& scene) const
    {
        if (!scene.getGUID().isValid())
        {
            LOG_ERROR("Cannot save a scene with an invalid GUID.");
            return false;
        }

        try
        {
            flatbuffers::FlatBufferBuilder builder(1024);
            const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_SCENE_VERSION, 0);
            std::vector<flatbuffers::Offset<SceneObject>> objects;
            objects.reserve(scene.getGameObjectCount());

            for (const auto& object : scene.getGameObjects())
            {
                std::vector<Engine::ComponentSnapshot> snapshots;
                if (!captureComponents(*object, snapshots))
                    return false;
                std::vector<flatbuffers::Offset<flatbuffers::String>> componentTypes;
                const std::vector<std::string_view> typeNames = object->getComponentTypeNames();
                componentTypes.reserve(typeNames.size());
                for (const std::string_view typeName : typeNames)
                    componentTypes.push_back(builder.CreateString(typeName));
                objects.push_back(CreateSceneObject(builder, createGuid(builder, object->getGUID()),
                    object->getParent() == nullptr ? 0 : createGuid(builder, object->getParent()->getGUID()),
                    builder.CreateString(object->getName()), object->isActiveSelf(), object->getTag(), object->getLayer(),
                    createTransform(builder, *object->getTransform()), builder.CreateVector(componentTypes),
                    builder.CreateVector(createComponents(builder, snapshots)),
                    createTagName(builder, object->getTag())));
            }

            const auto root = CreateSceneFile(builder, header, createGuid(builder, scene.getGUID()),
                builder.CreateString(scene.getName()), builder.CreateVector(objects));
            FinishSceneFileBuffer(builder, root);
            return FlatBufferWriter{}.saveAtomic(path,
                std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while saving a scene.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Scene data exceeded a collection capacity while saving.");
            return false;
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            LOG_ERROR("Filesystem error while saving a scene: {}", error.what());
            return false;
        }
    }

    static bool loadSceneData(const std::filesystem::path& path, Scene& scene)
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("SCNE"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifySceneFileBuffer(verifier))
            return false;
        const SceneFile* source = GetSceneFile(reader.data());
        if (source == nullptr || source->header() == nullptr || source->guid() == nullptr || source->name() == nullptr
            || !readGuid(source->guid()).isValid()
            || source->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || source->header()->asset_version() < MINIMUM_SUPPORTED_SCENE_VERSION
            || source->header()->asset_version() > CURRENT_SCENE_VERSION)
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
                if (serialized->layer() >= 32)
                {
                    LOG_ERROR("Scene contains an invalid Layer ID for GameObject.");
                    return false;
                }
                const Engine::ObjectGUID guid = readGuid(serialized->guid());
                GameObject* object = scene.createGameObject(serialized->name()->str(), guid);
                if (object == nullptr || objects.contains(guid))
                    return false;
                object->setActive(serialized->active());
                TagID tag = serialized->tag();
                if (const flatbuffers::String* const tagName = serialized->tag_name();
                    tagName != nullptr && tagName->size() != 0)
                {
                    const std::string name = tagName->str();
                    tag = TagManager::instance().registerTag(name);
                    if (TagManager::instance().getName(tag) != name)
                    {
                        LOG_ERROR("Failed to register a Scene Tag name: {}", name);
                        return false;
                    }
                }
                object->setTag(tag);
                object->setLayer(serialized->layer());
                const TransformData* transform = serialized->transform();
                if (transform != nullptr)
                {
                    const Engine::Serialization::Quaternion* rotation = transform->rotation();
                    const Vector3 position = readVector(transform->position(), Vector3::Zero);
                    const Vector3 scale = readVector(transform->scale(), Vector3::One);
                    const Engine::Quaternion rotationValue = rotation == nullptr
                        ? Engine::Quaternion::Identity : Engine::Quaternion(rotation->x(), rotation->y(), rotation->z(), rotation->w());
                    if (!isFinite(position) || !isFinite(scale) || !isValidRotation(rotationValue))
                    {
                        LOG_ERROR("Scene contains an invalid transform for GameObject {}.", guid.low);
                        return false;
                    }
                    object->getTransform()->setPosition(position);
                    object->getTransform()->setRotation(rotationValue);
                    object->getTransform()->setScale(scale);
                }
                std::vector<Engine::ComponentSnapshot> snapshots;
                if (!readComponents(serialized->components(), snapshots))
                    return false;
                if (!snapshots.empty())
                {
                    if (!restoreComponents(*object, snapshots))
                        return false;
                }
                else if (const auto* componentTypes = serialized->component_types())
                {
                    for (const flatbuffers::String* componentType : *componentTypes)
                    {
                        if (componentType == nullptr)
                            continue;
                        const std::string_view typeName = componentType->string_view();
                        if (object->addComponent(typeName) == nullptr
                            && ComponentRegistry::instance().findByName(typeName) != nullptr)
                        {
                            LOG_ERROR("Failed to create registered component while loading scene: {}", typeName);
                            return false;
                        }
                    }
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

    bool SceneSerializer::load(const std::filesystem::path& path, Scene& scene) const
    {
        try
        {
            Scene loadedScene(Engine::ObjectGUID::generate(), {});
            if (!loadSceneData(path, loadedScene))
                return false;

            std::string loadedName = loadedScene.getName();
            if (!scene.replaceGameObjectsFrom(loadedScene))
            {
                LOG_ERROR("Failed to replace scene contents after loading.");
                return false;
            }
            scene.setGUID(loadedScene.getGUID());
            scene.setName(std::move(loadedName));
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while loading a scene.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Scene data exceeded a collection capacity while loading.");
            return false;
        }
    }
} // namespace Engine::Serialization