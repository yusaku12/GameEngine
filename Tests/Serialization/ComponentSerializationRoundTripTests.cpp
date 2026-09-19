#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <flatbuffers/flatbuffers.h>

#include "Generated\FlatBuffers\AnimatorComponent_generated.h"
#include "Generated\FlatBuffers\Prefab_generated.h"
#include "Core\GameObject\Component\TransformComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\Prefab\Prefab.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Serialization\ComponentSerialization.h"

namespace Engine
{
    void TransformComponent::onImGui() {}
}

namespace
{
    class TestPayloadComponent final : public Engine::Component
    {
    public:
        std::uint32_t getPayloadVersion() const noexcept override { return 7; }

        bool serializePayload(std::vector<std::uint8_t>& payload) const override
        {
            payload.resize(sizeof(value));
            std::memcpy(payload.data(), &value, sizeof(value));
            return true;
        }

        bool deserializePayload(const std::uint32_t version,
            const std::span<const std::uint8_t> payload) override
        {
            if (version != 7 || payload.size() != sizeof(value))
                return false;
            std::memcpy(&value, payload.data(), sizeof(value));
            return true;
        }

        std::int32_t value = 0;
    };

    bool check(const bool condition, const char* message)
    {
        if (!condition) std::cerr << message << '\n';
        return condition;
    }
}

int main()
{
    Engine::ComponentRegistry::instance().registerType<TestPayloadComponent>("TestPayload");
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "GameEngineSerializationTests";
    const std::filesystem::path scenePath = directory / "component.scene";
    const std::filesystem::path prefabPath = directory / "component.prefab";

    Engine::Scene source({ 1, 2 }, "Source");
    Engine::GameObject* object = source.createGameObject("Animated", { 3, 4 });
    TestPayloadComponent* component = object == nullptr ? nullptr : object->addComponent<TestPayloadComponent>();
    if (!check(component != nullptr, "Test component creation failed.")) return 1;
    component->value = 42;
    component->setEnabled(false);

    Engine::Serialization::SceneSerializer sceneSerializer;
    if (!check(sceneSerializer.save(scenePath, source), "Scene save failed.")) return 1;
    Engine::Scene loadedScene({ 5, 6 }, "Loaded");
    if (!check(sceneSerializer.load(scenePath, loadedScene), "Scene load failed.")) return 1;
    const Engine::GameObject* loadedObject = loadedScene.find("Animated");
    const TestPayloadComponent* loadedComponent = loadedObject == nullptr
        ? nullptr : loadedObject->getComponent<TestPayloadComponent>();
    if (!check(loadedComponent != nullptr && loadedComponent->value == 42 && !loadedComponent->isEnabled(),
        "Scene component payload changed.")) return 1;

    Engine::Prefab prefab;
    if (!check(prefab.capture(*object), "Prefab capture failed.")) return 1;
    Engine::Serialization::PrefabSerializer prefabSerializer;
    if (!check(prefabSerializer.save(prefabPath, prefab), "Prefab save failed.")) return 1;
    Engine::Prefab loadedPrefab;
    if (!check(prefabSerializer.load(prefabPath, loadedPrefab), "Prefab load failed.")) return 1;
    Engine::Scene prefabScene({ 7, 8 }, "PrefabScene");
    Engine::GameObject* instance = loadedPrefab.instantiate(prefabScene);
    const TestPayloadComponent* prefabComponent = instance == nullptr
        ? nullptr : instance->getComponent<TestPayloadComponent>();
    if (!check(prefabComponent != nullptr && prefabComponent->value == 42 && !prefabComponent->isEnabled(),
        "Prefab component payload changed.")) return 1;

    const std::vector<Engine::ComponentSnapshot> unknown{ { "UnknownComponent", false, 99, { 1, 2, 3 } } };
    if (!check(Engine::Serialization::restoreComponents(*instance, unknown), "Unknown component was not ignored."))
        return 1;

    const std::filesystem::path legacyPath = directory / "legacy.prefab";
    flatbuffers::FlatBufferBuilder legacyBuilder(256);
    const auto legacyGuid = Engine::Serialization::CreateObjectGUID(legacyBuilder, 9, 10);
    const Engine::Serialization::Vec3 zero(0.0f, 0.0f, 0.0f);
    const Engine::Serialization::Vec3 one(1.0f, 1.0f, 1.0f);
    const auto rotation = Engine::Serialization::CreateQuaternion(legacyBuilder, 0.0f, 0.0f, 0.0f, 1.0f);
    const auto transform = Engine::Serialization::CreateTransformData(legacyBuilder, &zero, rotation, &one);
    const std::vector<flatbuffers::Offset<flatbuffers::String>> legacyTypes{
        legacyBuilder.CreateString("TestPayload") };
    const auto legacyNode = Engine::Serialization::CreatePrefabNodeData(legacyBuilder, legacyGuid,
        legacyBuilder.CreateString("Legacy"), true, 0, 0, transform,
        legacyBuilder.CreateVector(legacyTypes), 0, 0);
    const auto legacyHeader = Engine::Serialization::CreateFileHeader(legacyBuilder, 1, 1, 0);
    Engine::Serialization::FinishPrefabFileBuffer(legacyBuilder,
        Engine::Serialization::CreatePrefabFile(legacyBuilder, legacyHeader, legacyNode));
    std::ofstream legacyOutput(legacyPath, std::ios::binary | std::ios::trunc);
    legacyOutput.write(reinterpret_cast<const char*>(legacyBuilder.GetBufferPointer()),
        static_cast<std::streamsize>(legacyBuilder.GetSize()));
    legacyOutput.close();
    Engine::Prefab legacyPrefab;
    Engine::Scene legacyScene({ 20, 21 }, "LegacyScene");
    if (!check(prefabSerializer.load(legacyPath, legacyPrefab), "Version 1 Prefab load failed.")) return 1;
    Engine::GameObject* legacyInstance = legacyPrefab.instantiate(legacyScene);
    if (!check(legacyInstance != nullptr && legacyInstance->getComponent<TestPayloadComponent>() != nullptr,
        "Version 1 component type was not restored.")) return 1;

    flatbuffers::FlatBufferBuilder animatorBuilder(256);
    const Engine::Serialization::AssetGuid skeletonGuid(11, 12);
    const Engine::Serialization::AssetGuid clipGuid(13, 14);
    const Engine::Serialization::AssetGuid controllerGuid(15, 16);
    const std::vector<flatbuffers::Offset<Engine::Serialization::AnimatorParameterValueData>> parameters{
        Engine::Serialization::CreateAnimatorParameterValueData(animatorBuilder, 17,
            Engine::Serialization::AnimatorParameterValueType_Float, 2.5f, 0, false),
        Engine::Serialization::CreateAnimatorParameterValueData(animatorBuilder, 18,
            Engine::Serialization::AnimatorParameterValueType_Trigger, 0.0f, 0, true),
    };
    Engine::Serialization::FinishAnimatorComponentPayloadBuffer(animatorBuilder,
        Engine::Serialization::CreateAnimatorComponentPayload(animatorBuilder, &skeletonGuid, &clipGuid,
            &controllerGuid, 1.5f, true, false, 19, 0.25f, animatorBuilder.CreateVector(parameters)));
    flatbuffers::Verifier animatorVerifier(animatorBuilder.GetBufferPointer(), animatorBuilder.GetSize());
    const auto* animatorPayload = Engine::Serialization::GetAnimatorComponentPayload(
        animatorBuilder.GetBufferPointer());
    if (!check(Engine::Serialization::VerifyAnimatorComponentPayloadBuffer(animatorVerifier)
        && animatorPayload->skeleton_guid()->high() == 11
        && animatorPayload->controller_guid()->low() == 16
        && animatorPayload->speed() == 1.5f && animatorPayload->apply_root_motion()
        && !animatorPayload->playing() && animatorPayload->current_state() == 19
        && animatorPayload->normalized_time() == 0.25f
        && animatorPayload->parameters()->size() == 2
        && animatorPayload->parameters()->Get(1)->bool_value(),
        "Animator component payload changed.")) return 1;

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return 0;
}
