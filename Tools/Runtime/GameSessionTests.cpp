#include "Pch.h"
#include "Core\GameObject\Behaviour.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\System\GameSession.h"
#include "Core\System\StartupOptions.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Editor\ImGui\EditorUi.h"
#include "Graphics\Camera\CameraManager.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Physics\Character\CharacterControllerComponent.h"
#include "Physics\Collider\BoxColliderComponent.h"
#include "Physics\Collider\ConvexColliderComponent.h"
#include "Physics\Joint\JointComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Rigidbody\RigidbodyComponent.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Material\MaterialManager.h"
#include "Generated\FlatBuffers\CameraComponent_generated.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace
{
    using namespace Engine;

    int checks = 0;

    void require(const bool condition, const char* message)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Probe final : Behaviour
    {
        static inline int awake = 0;
        static inline int starts = 0;
        static inline int updates = 0;
        static inline int fixed = 0;
        static inline int late = 0;
        static inline int disabled = 0;
        static inline int destroyed = 0;
        static inline bool tryReentrantStop = false;
        static inline bool stopWasRejected = false;
        static inline bool destroySawEditor = false;
        static inline Scene* originalScene = nullptr;

        void onAwake() override { ++awake; }
        void onStart() override { ++starts; }
        void onUpdate(float) override
        {
            ++updates;
            if (tryReentrantStop)
                stopWasRejected = !GameSession::instance().stop();
        }
        void onFixedUpdate(float) override { ++fixed; }
        void onLateUpdate(float) override { ++late; }
        void onDisable() override { ++disabled; }
        void onDestroy() override
        {
            ++destroyed;
            if (SceneManager::instance().getActiveScene() == originalScene)
                destroySawEditor = true;
            static_cast<void>(SceneManager::instance().createScene("DestroyCallbackLeak"));
        }
    };

    struct Uncloneable final : Behaviour
    {
        bool copyConfigurationTo(Component&) const override { return false; }
    };

    void testArguments()
    {
        StartupOptions options;
        std::string error;
        require(options.parse({}, error) && options.mode == ExecutionMode::Editor, "default editor mode");
        const std::array valid{ std::wstring_view(L"--runtime"), std::wstring_view(L"--scene"),
            std::wstring_view(L"Assets\\Scenes\\Scene with spaces.scene") };
        require(options.parse(valid, error) && options.mode == ExecutionMode::Runtime
            && options.scenePath.filename() == L"Scene with spaces.scene", "Runtime argument path");
        const std::array missing{ std::wstring_view(L"--runtime") };
        require(!options.parse(missing, error) && !error.empty(), "Runtime requires a scene");
        const std::array duplicate{ std::wstring_view(L"--runtime"), std::wstring_view(L"--runtime") };
        require(!options.parse(duplicate, error), "duplicate argument rejected");
        const std::array pathOnly{ std::wstring_view(L"--scene"), std::wstring_view(L"sample.scene") };
        require(!options.parse(pathOnly, error), "scene without Runtime rejected");
        const std::array malformed{ std::wstring_view(L"--runtime"), std::wstring_view(L"--scene"),
            std::wstring_view(L"--help") };
        require(!options.parse(malformed, error), "missing scene path rejected");
        const std::array unknown{ std::wstring_view(L"--unknown") };
        require(!options.parse(unknown, error), "unknown argument rejected");
        const std::array help{ std::wstring_view(L"--help") };
        require(options.parse(help, error) && options.showHelp, "help");
    }

    void tick()
    {
        GameSession::instance().beginFrame(true);
        GameSession::instance().update();
    }

    void testSessions(const std::filesystem::path& fixture)
    {
        ComponentRegistry::instance().registerType<Probe>("Session Probe", false, false, true);
        ComponentRegistry::instance().registerType<Uncloneable>("Uncloneable", false, false, true);
        SceneManager& manager = SceneManager::instance();
        GameSession& session = GameSession::instance();
        session.initialize(ExecutionMode::Editor);
        TimeManager::instance().initialize();
        Scene* editing = manager.createScene("SessionTests");
        Probe::originalScene = editing;
        require(editing != nullptr, "create editing scene");
        GameObject* root = editing->createGameObject("Root");
        require(root != nullptr && root->addComponent<Probe>() != nullptr, "add lifecycle probe");
        const ObjectGUID rootGuid = root->getGUID();
        root->getTransform()->setPosition(Vector3(3.0f, 5.0f, 7.0f));
        GameObject* child = editing->createGameObject("Child");
        require(child != nullptr && child->setParent(root, false), "create hierarchy");
        child->setActive(false);
        child->setLayer(5);
        child->setTag(7);
        child->getComponent<TransformComponent>()->setEnabled(false);
        auto* camera = root->addComponent<CameraComponent>();
        require(camera != nullptr, "add camera");
        auto* alternateCamera = editing->createGameObject("Alternate Camera")->addComponent<CameraComponent>();
        camera->setFieldOfView(83.0f);
        camera->setBackgroundColor(Color(0.7f, 0.1f, 0.2f, 1.0f));
        tick();
        require(Probe::awake == 0 && Probe::starts == 0 && Probe::updates == 0
            && Probe::fixed == 0 && Probe::late == 0, "editing executes no gameplay lifecycle");
        require(TimeManager::instance().deltaTime() == 0.0f, "editing simulation time is frozen");
        require(CameraManager::instance().getActiveCamera() == camera, "editing camera preview");
        require(CameraManager::instance().setMainCamera(alternateCamera), "select non-first main camera");
        require(Serialization::SceneSerializer{}.save(fixture, *editing), "save Runtime fixture");
        {
            Scene loaded(ObjectGUID::generate(), "Loaded");
            require(Serialization::SceneSerializer{}.load(fixture, loaded), "load Runtime fixture");
            auto* loadedCamera = loaded.find(rootGuid)->getComponent<CameraComponent>();
            require(loadedCamera != nullptr && loadedCamera->getFieldOfView() == 83.0f
                && loadedCamera->getBackgroundColor().x == 0.7f, "persist camera settings");
            std::vector<std::uint8_t> payload;
            require(camera->serializePayload(payload), "camera payload");
            require(!loadedCamera->deserializePayload(999, payload), "reject unsupported camera version");
            require(loadedCamera->deserializePayload(0, {}), "legacy empty camera payload");
            require(!loadedCamera->deserializePayload(1, std::span<const std::uint8_t>(payload.data(), 3)),
                "reject truncated camera payload");
            flatbuffers::FlatBufferBuilder invalidBuilder;
            const Serialization::Vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
            const Serialization::Vec4 background(0.0f, 0.0f, 0.0f, 1.0f);
            const auto invalid = Serialization::CreateCameraComponentPayload(invalidBuilder, 0, 999.0f,
                0.1f, 1000.0f, 5.0f, &viewport, 1, &background);
            Serialization::FinishCameraComponentPayloadBuffer(invalidBuilder, invalid);
            require(!loadedCamera->deserializePayload(1,
                { invalidBuilder.GetBufferPointer(), invalidBuilder.GetSize() })
                && loadedCamera->getFieldOfView() == 83.0f, "invalid settings leave camera unchanged");
            CameraManager::instance().shutdown();
            loaded.submitRenderData();
            require(CameraManager::instance().getMainCamera() == loaded.find("Alternate Camera")->getComponent<CameraComponent>(),
                "saved main-camera preference survives registration order");
        }
        tick();
        require(CameraManager::instance().setMainCamera(camera), "restore primary camera");
        {
            Scene runtimeFixture(ObjectGUID::generate(), "RuntimeFixture");
            GameObject* runtimeCamera = runtimeFixture.createGameObject("Main Camera");
            auto* component = runtimeCamera->addComponent<CameraComponent>();
            require(camera->copyConfigurationTo(*component), "copy standalone Runtime camera");
            require(Serialization::SceneSerializer{}.save(fixture, runtimeFixture), "write standalone Runtime fixture");
        }
        Scene* secondary = manager.createScene("Secondary");
        auto* secondaryCamera = secondary->createGameObject("Secondary Camera")->addComponent<CameraComponent>();
        require(manager.setActiveScene(secondary), "switch editing scene");
        tick();
        require(CameraManager::instance().getActiveCamera() == secondaryCamera, "editing preview cannot retain inactive scene camera");
        require(manager.setActiveScene(editing), "switch back to editing scene");
        tick();
        require(CameraManager::instance().getActiveCamera() == camera, "restore current editing preview");
        session.setGameViewFocused(true);
        tick();
        InputManager::instance().addMouseWheel(120);
        tick();
        require(InputManager::instance().getMouseWheel() == 120, "focused Game view accepts editor input");
        session.setGameViewFocused(false);
        tick();
        require(InputManager::instance().getMouseWheel() == 0, "panel focus loss clears transient input");
        InputManager::instance().addMouseWheel(120);
        tick();
        require(InputManager::instance().getMouseWheel() == 0, "unfocused Game view rejects input");

        GameObject* body = editing->createGameObject("Body");
        body->getTransform()->setPosition(Vector3(0.0f, 10.0f, 0.0f));
        require(body->addComponent<RigidbodyComponent>() != nullptr
            && body->addComponent<BoxColliderComponent>() != nullptr, "create physics body");
        GameObject* persistent = manager.getPersistentScene()->createGameObject("PersistentBody");
        auto* persistentBody = persistent->addComponent<RigidbodyComponent>();
        require(persistentBody != nullptr, "persistent body");
        auto* joint = body->addComponent<JointComponent>();
        joint->setConnectedBody(persistentBody);
        const ObjectGUID persistentGuid = persistent->getGUID();
        const ObjectGUID bodyGuid = body->getGUID();
        const ObjectGUID childGuid = child->getGUID();
        GameObject* controllerObject = editing->createGameObject("Controller");
        auto* controller = controllerObject->addComponent<CharacterControllerComponent>();
        controller->setHeight(3.0f);
        controller->setRadius(0.75f);
        const std::array points{ Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, -1.0f, -1.0f),
            Vector3(0.0f, 1.0f, -1.0f), Vector3(0.0f, 0.0f, 1.0f) };
        const auto convexAsset = PhysicsMeshAsset::cookConvex(points);
        require(convexAsset != nullptr && convexAsset->getPath().empty(), "unsaved physics asset");
        auto* convex = editing->createGameObject("Convex")->addComponent<ConvexColliderComponent>();
        convex->setPhysicsAsset(convexAsset);
        auto* modelRenderer = root->addComponent<ModelRendererComponent>();
        ModelResource modelResource;
        modelResource.materialSlots.push_back({ "Test" });
        const ModelHandle model = ModelManager::instance().create(std::move(modelResource));
        require(model.isValid(), "unsaved model");
        modelRenderer->setModel(model);
        const MaterialHandle material = MaterialManager::instance().create(MaterialAsset{});
        require(material.isValid() && modelRenderer->setMaterialOverride(0, material), "unsaved material override");
        auto* animator = root->addComponent<AnimatorComponent>();
        require(animator != nullptr, "Animator configuration");
        SkeletonResource skeletonSource;
        skeletonSource.bones.push_back(Bone{ 0, "Root", -1 });
        SkeletonAsset skeletonAsset;
        AnimationAssetBuilder builder;
        require(builder.buildSkeleton(skeletonSource, "MemorySkeleton.fbx", skeletonAsset), "build unsaved skeleton");
        AnimationResource clipSource;
        clipSource.name = "Move";
        clipSource.duration = 1.0f;
        AnimationChannel channel;
        channel.nodeName = "Root";
        channel.positions = { { Vector3::Zero, 0.0f }, { Vector3::UnitX, 1.0f } };
        clipSource.channels.push_back(channel);
        AnimationClipAsset clipAsset;
        require(builder.buildClip(clipSource, skeletonAsset, "MemoryClip.fbx", clipAsset), "build unsaved clip");
        auto& animationAssets = AnimationAssetManager::instance();
        const auto skeleton = animationAssets.createSkeleton(std::move(skeletonAsset));
        const auto clip = animationAssets.createClip(std::move(clipAsset));
        require(animator->setAnimation(skeleton, clip) && animator->play(0.25f), "configure unsaved animation");
        animator->setSpeed(0.5f);
        const float editAnimationTime = animator->getNormalizedTime();
        tick();
        require(animator->getNormalizedTime() == editAnimationTime, "editing does not evaluate animation");
        require(PhysicsSystem::instance().getWorld()->getStatistics().dynamicBodies == 0,
            "editing never enables physics actors");

        TimeManager::instance().setTimeScale(0.5f);
        TimeManager::instance().setMaxDeltaTime(0.08f);
        require(session.play(), "Play clones unsaved scene and resources");
        Scene* playing = manager.getActiveScene();
        require(playing != editing && playing->getGUID() != editing->getGUID(), "isolated scene identity");
        require(playing->find(rootGuid) != root, "isolated GameObject ownership");
        require(playing->find(childGuid)->getParent() == playing->find(rootGuid)
            && !playing->find(childGuid)->isActiveSelf()
            && !playing->find(childGuid)->getComponent<TransformComponent>()->isEnabled()
            && playing->find(childGuid)->getLayer() == 5 && playing->find(childGuid)->getTag() == 7,
            "hierarchy, active, tag and layer copied");
        require(playing->find(rootGuid)->getComponent<ModelRendererComponent>()->getModel() == model,
            "in-memory model handle preserved");
        require(playing->find(rootGuid)->getComponent<ModelRendererComponent>()->getMaterialOverrides()[0] == material,
            "unsaved material override preserved");
        require(playing->find(bodyGuid)->getComponent<JointComponent>()->getConnectedBody()
            == manager.getPersistentScene()->find(persistentGuid)->getComponent<RigidbodyComponent>(),
            "cross-scene joint references point to clones");
        require(playing->find(controllerObject->getGUID())->getComponent<CharacterControllerComponent>()->getHeight() == 3.0f,
            "controller configuration copied");
        require(playing->find("Convex")->getComponent<ConvexColliderComponent>()->getPhysicsAsset() == convexAsset,
            "unsaved cooked physics asset shared immutably");
        tick();
        auto* playAnimator = playing->find(rootGuid)->getComponent<AnimatorComponent>();
        require(playAnimator->getSkeleton() == skeleton && playAnimator->getClip() == clip
            && playAnimator->getSpeed() == 0.5f && playAnimator->isPlaying(), "clone unsaved animation configuration");
        require(playAnimator->getSkinningSnapshot() != animator->getSkinningSnapshot(),
            "clone owns independent animation runtime state");
        require(Probe::awake == 1 && Probe::starts == 1 && Probe::updates == 1 && Probe::late == 1,
            "Play lifecycle executes exactly once");
        require(CameraManager::instance().getActiveCamera() != camera
            && CameraManager::instance().getActiveCamera()->getFieldOfView() == 83.0f, "Play camera settings");
        require(PhysicsSystem::instance().getWorld()->getStatistics().dynamicBodies == 2,
            "only cloned physics bodies enabled");
        require(session.setPaused(true), "Pause");
        const int beforeUpdates = Probe::updates;
        const int beforeFixed = Probe::fixed;
        const int beforeLate = Probe::late;
        const auto beforeSteps = PhysicsSystem::instance().getProfilerSnapshot().totalSteps;
        const double beforeTime = TimeManager::instance().timeSinceStartupPrecise();
        const float beforeAnimation = playAnimator->getNormalizedTime();
        for (int index = 0; index < 10; ++index)
            tick();
        require(Probe::updates == beforeUpdates && Probe::fixed == beforeFixed && Probe::late == beforeLate,
            "Pause suppresses all gameplay callbacks");
        require(PhysicsSystem::instance().getProfilerSnapshot().totalSteps == beforeSteps
            && TimeManager::instance().timeSinceStartupPrecise() == beforeTime, "Pause freezes physics and game clock");
        require(playAnimator->getNormalizedTime() == beforeAnimation, "Pause freezes animation");
        require(session.step(), "request Step");
        require(!session.step(), "reject duplicate Step");
        tick();
        require(Probe::updates == beforeUpdates + 1 && Probe::fixed == beforeFixed + 1 && Probe::late == beforeLate + 1,
            "Step executes one Update/FixedUpdate/LateUpdate");
        require(PhysicsSystem::instance().getProfilerSnapshot().totalSteps == beforeSteps + 1,
            "Step advances exactly one physics step");
        require(std::abs(TimeManager::instance().timeSinceStartupPrecise() - beforeTime
            - TimeManager::instance().fixedDeltaTimePrecise()) < 1.0e-7, "Step clock uses fixed duration");
        require(std::abs(playAnimator->getNormalizedTime() - beforeAnimation
            - 0.5f * TimeManager::instance().fixedDeltaTime()) < 1.0e-6f, "Step advances animation by one fixed duration");
        tick();
        require(Probe::updates == beforeUpdates + 1, "Step remains paused");
        require(session.setPaused(false), "Resume");
        Probe::tryReentrantStop = true;
        tick();
        Probe::tryReentrantStop = false;
        require(Probe::stopWasRejected && session.isPlaying(), "reject Stop inside gameplay callback");
        playing->find(rootGuid)->getTransform()->setPosition(Vector3::Zero);
        TimeManager::instance().setMaxDeltaTime(0.5f);
        require(manager.dontDestroyOnLoad(playing->find(rootGuid)), "persistent transfer during Play");
        require(manager.createScene("CreatedDuringPlay") != nullptr, "new scene during Play");
        require(manager.unloadScene("SessionTests"), "unload runtime scene during Play");
        require(session.stop(), "Stop after scene switch/unload");
        require(manager.getActiveScene() == editing && editing->find(rootGuid) == root
            && root->getWorldPosition() == Vector3(3.0f, 5.0f, 7.0f), "restore original editor object identity and transform");
        require(manager.getPersistentScene()->find(persistentGuid) == persistent
            && manager.getPersistentScene()->find(rootGuid) == nullptr, "restore original persistent ownership");
        require(manager.findScene("CreatedDuringPlay") == nullptr, "discard runtime-created scenes");
        require(Probe::disabled == 1 && Probe::destroyed == 1, "cloned lifecycle disables and destroys once");
        require(!Probe::destroySawEditor && manager.findScene("DestroyCallbackLeak") == nullptr,
            "runtime destruction callbacks cannot mutate restored editor scenes");
        require(PhysicsSystem::instance().getWorld()->getStatistics().dynamicBodies == 0,
            "Stop releases all runtime physics actors");
        require(TimeManager::instance().timeScale() == 0.5f, "restore editor time settings");
        require(TimeManager::instance().maxDeltaTime() == 0.08f, "restore frame delta cap");
        require(animator->getNormalizedTime() == editAnimationTime, "Stop preserves original animation state");
        require(!session.step() && !session.setPaused(true) && !session.stop(), "invalid edit transitions rejected");

        GameObject* broken = editing->createGameObject("Broken");
        broken->addComponent<Uncloneable>();
        require(!session.play() && manager.getActiveScene() == editing
            && manager.getPersistentScene()->find(persistentGuid) == persistent,
            "failed clone leaves editor scenes untouched");
        editing->destroyGameObject(broken);
        manager.processDestroyQueue();
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            require(session.play(), "repeat Play");
            tick();
            require(session.stop(), "repeat Stop");
        }
        require(Probe::awake == 4 && Probe::destroyed == 4, "fresh lifecycles for repeated Play");
        require(CameraManager::instance().getActiveCamera() == camera, "restore editing camera");
        require(session.play(), "Play before shutdown");
        session.initialize(ExecutionMode::Runtime);
        require(session.mode() == ExecutionMode::Editor && session.isPlaying(),
            "mode initialization cannot orphan an active editor session");
        tick();
        require(session.setPaused(true), "Pause before shutdown");
        session.shutdown();
        require(session.state() == PlayState::Editing && manager.getActiveScene() == editing
            && editing->find(rootGuid) == root, "shutdown during paused Play restores and releases session");
    }

    void testEditorControls()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        {
            EditorUi ui;
            SceneManager& manager = SceneManager::instance();
            Scene* original = manager.getActiveScene();
            Scene* scene = manager.findScene("EditorScene");
            require(manager.setActiveScene(scene), "activate editor UI scene");
            const auto frame = [&ui](const char* button)
            {
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                if (button != nullptr)
                {
                    ImGuiWindow* menu = ImGui::FindWindowByName("##MainMenuBar");
                    require(menu != nullptr, "editor menu exists");
                    ImGuiContext& context = *ImGui::GetCurrentContext();
                    const ImGuiID id = ImHashStr(button, 0, menu->GetID("##MenuBar"));
                    context.NavWindow = menu;
                    context.NavId = id;
                    context.NavActivateId = id;
                    context.NavActivateDownId = id;
                    context.NavActivatePressedId = id;
                }
                std::uint32_t gameWidth = 320;
                std::uint32_t gameHeight = 240;
                ui.draw(nullptr, 0, gameWidth, gameHeight);
                ImGui::Render();
            };
            frame(nullptr);
            frame("Play");
            require(GameSession::instance().state() == PlayState::Editing, "UI transitions wait for frame boundary");
            MainThreadDispatcher::instance().dispatchPending();
            require(GameSession::instance().state() == PlayState::Playing
                && manager.getActiveScene() != scene, "UI Play starts isolated session");
            tick();
            frame("Pause");
            MainThreadDispatcher::instance().dispatchPending();
            require(GameSession::instance().state() == PlayState::Paused, "UI Pause");
            frame("Step");
            MainThreadDispatcher::instance().dispatchPending();
            const auto steps = PhysicsSystem::instance().getProfilerSnapshot().totalSteps;
            tick();
            require(GameSession::instance().state() == PlayState::Paused
                && PhysicsSystem::instance().getProfilerSnapshot().totalSteps == steps + 1, "UI Step");
            frame("Resume");
            MainThreadDispatcher::instance().dispatchPending();
            require(GameSession::instance().state() == PlayState::Playing, "UI Resume");
            frame("Stop");
            MainThreadDispatcher::instance().dispatchPending();
            require(GameSession::instance().state() == PlayState::Editing
                && manager.getActiveScene() == scene, "UI Stop restores editor ownership");
            frame(nullptr);
            require(manager.setActiveScene(original), "restore test scene after UI controls");
        }
        ImGui::DestroyContext();
    }

    void testRuntimeRenderer()
    {
        GameSession::instance().initialize(ExecutionMode::Runtime);
        GameSession::instance().beginFrame(true);
        InputManager::instance().addMouseWheel(120);
        GameSession::instance().beginFrame(false);
        require(InputManager::instance().getMouseWheel() == 0, "Runtime window focus loss clears input");
        HWND hwnd = CreateWindowExW(0, L"STATIC", L"RuntimeTests", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(hwnd != nullptr, "create test window");
        DX12Renderer renderer;
        try
        {
            require(renderer.initialize(hwnd, 320, 240, ExecutionMode::Runtime), "Runtime renderer initialization");
            require(ImGui::GetCurrentContext() == nullptr, "Runtime creates no ImGui context");
            Scene* scene = SceneManager::instance().getActiveScene();
            auto* camera = scene->find("Root")->getComponent<CameraComponent>();
            ModelResource model;
            MeshResource mesh;
            mesh.vertices.resize(3);
            mesh.vertices[0].position = Vector3(-1.0f, -1.0f, 0.0f);
            mesh.vertices[1].position = Vector3(0.0f, 1.0f, 0.0f);
            mesh.vertices[2].position = Vector3(1.0f, -1.0f, 0.0f);
            mesh.indices = { 0, 1, 2 };
            mesh.subMeshes.push_back({ 0, 3, 0 });
            mesh.boundingBox = AABB(Vector3::Zero, Vector3(1.0f, 1.0f, 0.001f));
            model.boundingBox = mesh.boundingBox;
            model.meshes.push_back(std::move(mesh));
            ModelNode node;
            node.meshIndices.push_back(0);
            model.nodes.push_back(std::move(node));
            model.materialSlots.push_back({ "Default" });
            auto* triangle = scene->createGameObject("Runtime Triangle");
            triangle->getTransform()->setPosition(camera->getGameObject()->getWorldPosition() + Vector3(0.0f, 0.0f, 5.0f));
            triangle->addComponent<ModelRendererComponent>()->setModel(ModelManager::instance().create(std::move(model)));
            CameraManager::instance().registerCamera(camera);
            CameraManager::instance().setRenderTargetSize(320, 240);
            scene->submitRenderData();
            const auto render = [&renderer]
            {
                const auto frame = FrameProfiler::instance().beginFrame();
                const bool result = renderer.render();
                require(FrameProfiler::instance().endFrame(frame, 1.0), "record render frame");
                return result;
            };
            require(render(), "Runtime presents camera/model frame");
            require(renderer.getStatistics().drawCallCount > 0, "Runtime submits real geometry");
            require(renderer.resize(640, 360), "Runtime resize");
            CameraManager::instance().setRenderTargetSize(640, 360);
            scene->submitRenderData();
            require(render() && renderer.getGameViewSize() == std::array<std::uint32_t, 2>{ 640, 360 },
                "Runtime target and backbuffer resize together");
            CameraRenderSubmissionQueue::instance().clear();
            require(render(), "Runtime frame without camera");
            require(renderer.finalize(), "Runtime renderer shutdown");
        }
        catch (...)
        {
            renderer.finalize();
            DestroyWindow(hwnd);
            throw;
        }
        DestroyWindow(hwnd);
    }
}

int main()
{
    using namespace Engine;
    setCurrentThreadRole(ThreadRole::Main);
    if (!Logger::instance().initialize() || !PhysicsSystem::instance().initialize())
        return 1;
    JobSystem::instance().initialize();
    int result = 0;
    try
    {
        std::array<wchar_t, 32768> executable{};
        require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) != 0,
            "resolve test executable");
        const auto fixture = std::filesystem::path(executable.data()).parent_path() / "RuntimeFixture.scene";
        testArguments();
        testSessions(fixture);
        testEditorControls();
        testRuntimeRenderer();
        std::printf("PASS: %d checks; editor isolation, Play/Pause/Step/Stop, physics, cloning, camera persistence, Runtime presentation.\n", checks);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL after %d checks: %s\n", checks, error.what());
        result = 1;
    }
    GameSession::instance().shutdown();
    SceneManager::instance().shutdown();
    CameraManager::instance().shutdown();
    ModelManager::instance().clear();
    PhysicsSystem::instance().shutdown();
    JobSystem::instance().finalize();
    Logger::instance().finalize();
    return result;
}
