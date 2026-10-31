#include "Pch.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Scene\SceneSerializer.h"
#include "Editor\ImGui\EditorUi.h"
#include "Graphics\Camera\CameraManager.h"
#include "Physics\Collider\BoxColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Rigidbody\RigidbodyComponent.h"
#include <functional>
#include <imgui.h>

namespace
{
    using namespace Engine;

    void require(const bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Counts
    {
        int awake = 0;
        int enable = 0;
        int disable = 0;
        int destroy = 0;
        int destruct = 0;
        int draws = 0;
        bool detached = false;
        ImVec2 headerMin;
        ImVec2 headerMax;
    };

    class Probe final : public Component
    {
    public:
        explicit Probe(Counts* counts = nullptr) : m_counts(counts) {}
        ~Probe() override
        {
            if (m_counts != nullptr)
            {
                ++m_counts->destruct;
                m_counts->detached = getGameObject() == nullptr;
            }
        }

        std::function<void(Probe&)> update;
        std::function<void(Probe&)> destroy;

    protected:
        void onAwake() override { if (m_counts != nullptr) ++m_counts->awake; }
        void onEnable() override { if (m_counts != nullptr) ++m_counts->enable; }
        void onDisable() override { if (m_counts != nullptr) ++m_counts->disable; }
        void onDestroy() override
        {
            if (m_counts != nullptr)
                ++m_counts->destroy;
            if (destroy)
                destroy(*this);
        }
        void onUpdate(float) override { if (update) update(*this); }
        void onImGui() override
        {
            if (m_counts != nullptr)
            {
                ++m_counts->draws;
                m_counts->headerMin = ImGui::GetItemRectMin();
                m_counts->headerMax = ImGui::GetItemRectMax();
            }
        }

    private:
        Counts* m_counts;
    };

    void registerProbe(const bool multiple = true)
    {
        require(ComponentRegistry::instance().registerType<Probe>(
            "Removal Test Probe", multiple, false, true) != 0, "Probe registration failed");
    }

    void testImmediateRemoval()
    {
        registerProbe();
        Counts first, second;
        Scene scene(ObjectGUID::generate(), "Immediate");
        GameObject* const object = scene.createGameObject();
        require(object != nullptr, "Object creation failed");
        Probe* const left = object->addComponent<Probe>(&first);
        Probe* const right = object->addComponent<Probe>(&second);
        require(left != nullptr && right != nullptr, "Probe creation failed");
        scene.update(0.016f);
        require(object->removeComponent(*right), "Instance removal failed");
        require(object->getComponent<Probe>() == left, "Removal affected a sibling instance");
        require(second.disable == 1 && second.destroy == 1 && second.destruct == 1
            && second.detached, "Active removal did not complete its lifecycle exactly once");
        require(first.destruct == 0, "Sibling was destroyed");
        require(object->removeComponent<Probe>(), "Type-wide removal failed");
        require(!object->hasComponent<Probe>(), "Type-wide removal left an instance");
        require(first.disable == 1 && first.destroy == 1 && first.destruct == 1,
            "Type-wide removal changed lifecycle behavior");
    }

    void testProtectedAndForeignRemoval()
    {
        registerProbe();
        Counts counts;
        Scene scene(ObjectGUID::generate(), "Protected");
        GameObject* const object = scene.createGameObject();
        GameObject* const other = scene.createGameObject();
        require(object != nullptr && other != nullptr, "Object creation failed");
        TransformComponent* const transform = object->getComponent<TransformComponent>();
        require(transform != nullptr, "Required Transform is missing");
        require(!object->removeComponent(*transform) && !object->removeComponent<TransformComponent>(),
            "Required Transform was removable");
        Probe* const foreign = other->addComponent<Probe>(&counts);
        require(foreign != nullptr && !object->removeComponent(*foreign), "Foreign removal was accepted");
        require(other->getComponent<Probe>() == foreign, "Foreign component was changed");
        require(object->getTransform() != nullptr, "Transform was invalidated");
        require(other->removeComponent(*foreign), "Uninitialized removal failed");
        require(counts.disable == 0 && counts.destroy == 0 && counts.destruct == 1 && counts.detached,
            "Uninitialized component received lifecycle callbacks");
    }

    void testDeferredRemoval()
    {
        registerProbe();
        Counts first, second, pending;
        Scene scene(ObjectGUID::generate(), "Deferred");
        GameObject* const object = scene.createGameObject();
        require(object != nullptr, "Object creation failed");
        Probe* const left = object->addComponent<Probe>(&first);
        Probe* const right = object->addComponent<Probe>(&second);
        require(left != nullptr && right != nullptr, "Probe creation failed");
        scene.update(0.016f);
        int visited = 0;
        object->forEachComponent([&](Component& component, const std::type_index& type)
            {
                if (type != std::type_index(typeid(Probe)))
                    return;
                ++visited;
                if (&component == right)
                {
                    require(object->removeComponent(component), "Deferred removal failed");
                    require(!object->removeComponent(component), "Duplicate removal was accepted");
                    require(second.destruct == 0, "Component destroyed during enumeration");
                    Probe* const added = object->addComponent<Probe>(&pending);
                    require(added != nullptr && object->removeComponent(*added),
                        "Pending addition could not be removed");
                }
            });
        require(visited == 2, "Enumeration was invalidated");
        require(second.destruct == 1 && pending.destruct == 1 && first.destruct == 0,
            "Deferred operations affected the wrong instances");
        require(object->getComponent<Probe>() == left, "Surviving instance was lost");
        left->update = [](Probe& self)
            {
                require(self.getGameObject()->removeComponent(self), "Update self-removal failed");
            };
        scene.update(0.016f);
        require(first.destruct == 1 && first.destroy == 1, "Update self-removal did not finish");
    }

    void testReentrantRemovalAndAddition()
    {
        ComponentRegistry::instance().clear();
        registerProbe(false);
        Counts original, replacement;
        Scene scene(ObjectGUID::generate(), "Reentrant");
        GameObject* const object = scene.createGameObject();
        require(object != nullptr, "Object creation failed");
        Probe* const probe = object->addComponent<Probe>(&original);
        require(probe != nullptr, "Probe creation failed");
        scene.update(0.016f);
        Probe* added = nullptr;
        probe->destroy = [&](Probe& self)
            {
                added = self.getGameObject()->addComponent<Probe>(&replacement);
                require(added != nullptr && added != &self, "Destroy callback reused the dying instance");
                require(self.getGameObject()->addComponent<Probe>() == added,
                    "Pending singleton addition was duplicated");
            };
        require(object->removeComponent(*probe), "Reentrant removal failed");
        require(object->getComponent<Probe>() == added && replacement.awake == 1
            && replacement.enable == 1 && original.destruct == 1, "Replacement was not activated");
        added->setEnabled(false);
        require(object->removeComponent(*added), "Disabled removal failed");
        require(replacement.disable == 1 && replacement.destroy == 1 && replacement.destruct == 1,
            "Disabled removal repeated lifecycle callbacks");
    }

    void testTypeWideDeferredRemoval()
    {
        registerProbe();
        Counts first, second, replacement;
        Scene scene(ObjectGUID::generate(), "Type-wide deferred");
        GameObject* const object = scene.createGameObject();
        require(object != nullptr, "Object creation failed");
        require(object->addComponent<Probe>(&first) != nullptr
            && object->addComponent<Probe>(&second) != nullptr, "Probe creation failed");
        bool requested = false;
        object->forEachComponent([&](Component&, const std::type_index& type)
            {
                if (requested || type != std::type_index(typeid(Probe)))
                    return;
                requested = true;
                require(object->removeComponent<Probe>(), "Deferred type-wide removal failed");
                require(object->addComponent<Probe>(&replacement) != nullptr, "Replacement addition failed");
                require(first.destruct == 0 && second.destruct == 0, "Type-wide removal invalidated enumeration");
            });
        require(first.destruct == 1 && second.destruct == 1 && replacement.destruct == 0,
            "Type-wide removal affected a later addition");
        require(object->removeComponent<Probe>() && replacement.destruct == 1, "Replacement was not removable");
    }

    void testPersistence()
    {
        registerProbe();
        const ObjectGUID guid = ObjectGUID::generate();
        const auto path = std::filesystem::temp_directory_path()
            / ("ComponentRemoval-" + std::to_string(guid.high) + "-" + std::to_string(guid.low) + ".scene");
        struct Cleanup
        {
            const std::filesystem::path& path;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                if (error)
                    LOG_ERROR("Failed to remove temporary scene: {}.", error.message());
            }
        };
        const Cleanup cleanup{ path };
        Scene source(ObjectGUID::generate(), "Persistence");
        GameObject* const object = source.createGameObject();
        require(object != nullptr, "Object creation failed");
        Probe* const removed = object->addComponent<Probe>();
        require(removed != nullptr && object->removeComponent(*removed), "Removal before save failed");
        Serialization::SceneSerializer serializer;
        require(serializer.save(path, source), "Scene save failed");
        Scene restored(ObjectGUID::generate(), "Restored");
        require(serializer.load(path, restored), "Scene load failed");
        const GameObject* const loaded = restored.find(object->getGUID());
        require(loaded != nullptr && loaded->getTransform() != nullptr && !loaded->hasComponent<Probe>(),
            "Saving and loading resurrected the removed component");
    }

    void testPhysicsRemoval()
    {
        PhysicsSystem& physics = PhysicsSystem::instance();
        require(physics.initialize(), "Physics initialization failed");
        {
            Scene scene(ObjectGUID::generate(), "Physics removal");
            GameObject* const object = scene.createGameObject();
            require(object != nullptr, "Object creation failed");
            auto* const body = object->addComponent<RigidbodyComponent>();
            auto* const collider = object->addComponent<BoxColliderComponent>();
            require(body != nullptr && collider != nullptr, "Physics components were not added");
            scene.update(0.016f);
            physics.update(1.0f / 60.0f);
            PhysicsWorld* const world = physics.getWorld();
            require(world != nullptr && world->getStatistics().activeShapes == 1
                && world->getStatistics().dynamicBodies == 1, "Physics resources were not created");
            require(object->removeComponent(*body), "Rigidbody removal failed");
            require(world->getStatistics().dynamicBodies == 0 && world->getStatistics().staticBodies == 1
                && world->getStatistics().activeShapes == 1,
                "Rigidbody removal did not preserve the surviving collider");
            require(object->removeComponent(*collider), "Collider removal failed");
            require(world->getStatistics().activeShapes == 0
                && world->getStatistics().activeActors == 0, "Removed collider retained native resources");
            auto* const newBody = object->addComponent<RigidbodyComponent>();
            auto* const newCollider = object->addComponent<BoxColliderComponent>();
            require(newBody != nullptr && newCollider != nullptr, "Physics components could not be re-added");
            require(world->getStatistics().dynamicBodies == 1 && world->getStatistics().activeShapes == 1,
                "Re-added physics resources were not created");
            require(object->removeComponent(*newCollider), "Re-added Collider removal failed");
            require(world->getStatistics().dynamicBodies == 1 && world->getStatistics().activeShapes == 0,
                "Collider removal destroyed the surviving Rigidbody");
            require(object->removeComponent(*newBody) && world->getStatistics().activeActors == 0,
                "Re-added Rigidbody retained native resources");
            physics.update(1.0f / 60.0f);
        }
        physics.shutdown();
    }

    void testInspectorRemoval()
    {
        ComponentRegistry::instance().clear();
        registerProbe();
        ComponentRegistry::instance().registerType<CameraComponent>("Camera", false, false, true);
        const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
            ImGui::CreateContext(), &ImGui::DestroyContext);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 1000.0f);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        Counts first, second;
        {
            EditorUi editor;
            Scene* const scene = SceneManager::instance().getActiveScene();
            require(scene != nullptr, "Editor scene was not created");
            scene->update(0.016f);
            CameraComponent* const camera = CameraManager::instance().getActiveCamera();
            require(camera != nullptr, "Editor camera was not activated");
            GameObject* const selected = camera->getGameObject();
            require(selected->addComponent<Probe>(&first) != nullptr
                && selected->addComponent<Probe>(&second) != nullptr, "Inspector probes were not added");

            const auto frame = [&]()
                {
                    ImGui::NewFrame();
                    ImGui::SetWindowPos("Inspector", ImVec2(700.0f, 30.0f));
                    ImGui::SetWindowSize("Inspector", ImVec2(550.0f, 950.0f));
                    ImGui::SetWindowFocus("Inspector");
                    std::uint32_t gameWidth = 640, gameHeight = 480;
                    editor.draw(nullptr, 0, gameWidth, gameHeight);
                    ImGui::Render();
                };
            frame();
            frame();
            frame();
            require(second.headerMax.x > second.headerMin.x, "Inspector header was not rendered");
            const float halfButton = ImGui::GetFrameHeight() * 0.5f;
            io.AddMousePosEvent(second.headerMax.x - halfButton, second.headerMin.y + halfButton);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            if (second.destruct != 1 || first.destruct != 0)
                std::fprintf(stderr, "Inspector counts: first=%d second=%d; header=(%.1f,%.1f)-(%.1f,%.1f)\n",
                    first.destruct, second.destruct, second.headerMin.x, second.headerMin.y,
                    second.headerMax.x, second.headerMax.y);
            require(second.destruct == 1 && first.destruct == 0,
                "Inspector X button did not remove only the clicked instance");
            require(selected->getTransform() != nullptr && CameraManager::instance().getActiveCamera() == camera,
                "Inspector removal affected required Transform or active Camera");
            io.AddMousePosEvent(first.headerMin.x + 5.0f, first.headerMin.y + halfButton);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            const int collapsedDraws = first.draws;
            frame();
            require(first.draws == collapsedDraws, "Inspector header did not collapse");
            io.AddMousePosEvent(first.headerMax.x - halfButton, first.headerMin.y + halfButton);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            require(first.destruct == 1, "Collapsed Inspector X button did not remove the component");
            require(selected->removeComponent(*camera), "Camera removal failed");
            require(CameraManager::instance().getActiveCamera() == nullptr,
                "CameraManager retained the removed Camera");
            frame();
            SceneManager::instance().shutdown();
        }
        require(first.destruct == 1 && first.destroy == 1 && second.destroy == 1,
            "Editor shutdown repeated or skipped component destruction");
    }
}

int main()
{
    Engine::LoggerConfig logging;
    logging.useFile = false;
    logging.useDebugOutput = false;
    if (!Engine::Logger::instance().initialize(logging))
        return 1;
    try
    {
        testImmediateRemoval();
        testProtectedAndForeignRemoval();
        testDeferredRemoval();
        testTypeWideDeferredRemoval();
        testPersistence();
        testReentrantRemovalAndAddition();
        testInspectorRemoval();
        testPhysicsRemoval();
        std::puts("PASS: 8 component removal tests (including Inspector input, save/load and PhysX resources).");
    }
    catch (const std::runtime_error& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        Engine::SceneManager::instance().shutdown();
        Engine::PhysicsSystem::instance().shutdown();
        return 1;
    }
    Engine::Logger::instance().finalize();
    return 0;
}
