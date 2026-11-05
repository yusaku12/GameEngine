#include "Pch.h"
#include <cmath>
#include <functional>
#include <limits>
#include "Animation\Runtime\AnimatorInstance.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Components\Animation\AnimatorComponent.h"
#include "Components\Rendering\ModelRendererComponent.h"
#include "Components\Physics\Character\CharacterControllerComponent.h"
#include "Components\Physics\Collider\BoxColliderComponent.h"
#include "Components\Physics\Rigidbody\RigidbodyComponent.h"
#include "Core\GameObject\GameObjectManager.h"
#include "Core\Logging\Logger.h"
#include "Core\Threading\JobSystem.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\AnimatorComponent_generated.h"
#include "Physics\PhysicsSystem.h"

namespace
{
    using namespace Engine;
    constexpr float tolerance = 0.003f;
    int passed = 0;

    void require(const bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void expectNear(const Vector3& actual, const Vector3& expected, const char* message)
    {
        if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z)
            || (actual - expected).Length() > tolerance + expected.Length() * 0.0004f)
        {
            std::printf("Actual: %.6f %.6f %.6f; expected: %.6f %.6f %.6f\n",
                actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
            throw std::runtime_error(message);
        }
    }

    void expectNear(const Matrix& actual, const Matrix& expected, const char* message)
    {
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                if (!std::isfinite(actual.m[row][column])
                    || std::abs(actual.m[row][column] - expected.m[row][column]) > tolerance)
                {
                    std::printf("Matrix [%zu][%zu]: actual %.6f; expected %.6f\n",
                        row, column, actual.m[row][column], expected.m[row][column]);
                    throw std::runtime_error(message);
                }
    }

    void test(const char* name, const std::function<void()>& body)
    {
        body();
        ++passed;
        std::printf("PASS %s\n", name);
    }

    struct Assets
    {
        std::shared_ptr<SkeletonAsset> skeleton;
        std::shared_ptr<AnimationClipAsset> clip;
        AnimationResource animation;

        Assets(const Vector3& travel = Vector3(1, 0, 0),
            const AnimationWrapMode wrap = AnimationWrapMode::Loop,
            const Quaternion& rotation = Quaternion::Identity, const bool childMotion = false,
            const Vector3& offset = Vector3::Zero, const float endScale = 1.0f)
        {
            static unsigned sequence = 0;
            const std::filesystem::path source = "RootMotionTest" + std::to_string(++sequence) + ".fbx";
            SkeletonResource rawSkeleton;
            rawSkeleton.bones.push_back({ 0, "SceneRoot", -1, Matrix::Identity, Matrix::Identity });
            const Matrix childBind = Matrix::CreateTranslation(0, 1, 0);
            rawSkeleton.bones.push_back({ 1, "Motion", 0, childBind.Invert(), childBind });
            skeleton = std::make_shared<SkeletonAsset>();
            AnimationAssetBuilder builder;
            require(builder.buildSkeleton(rawSkeleton, source, *skeleton), "build skeleton");
            animation.name = "Move";
            animation.duration = 1.0f;
            animation.ticksPerSecond = 1.0f;
            AnimationChannel channel;
            channel.nodeName = childMotion ? "Motion" : "SceneRoot";
            const Vector3 start = offset + (childMotion ? Vector3(0, 1, 0) : Vector3::Zero);
            channel.positions = { { start, 0.0f }, { start + travel, 1.0f } };
            channel.rotations = { { Vector4(0, 0, 0, 1), 0.0f },
                { Vector4(rotation.x, rotation.y, rotation.z, rotation.w), 1.0f } };
            channel.scales = { { Vector3::One, 0.0f }, { Vector3(endScale, endScale, endScale), 1.0f } };
            animation.channels.push_back(std::move(channel));
            clip = std::make_shared<AnimationClipAsset>();
            require(builder.buildClip(animation, *skeleton, source, *clip), "build clip");
            clip->wrapMode = wrap;
        }

        void bind(AnimatorInstance& instance, const bool rootMotion = true,
            const std::string_view joint = {}) const
        {
            require(instance.setAssets(skeleton, clip), "bind assets");
            require(instance.configureRootMotion(rootMotion, joint), "configure root motion");
        }
    };

    struct Objects
    {
        GameObjectManager manager;
        ~Objects() { manager.clear(); }
    };

    class EnableRootMotionComponent final : public Component
    {
        void onUpdate(float) override
        {
            getGameObject()->getComponent<AnimatorComponent>()->setApplyRootMotion(true);
        }
    };

    AnimatorComponent* addAnimator(GameObject& object, const Assets& assets)
    {
        require(object.addComponent<ModelRendererComponent>() != nullptr, "add renderer");
        auto* animator = object.addComponent<AnimatorComponent>();
        require(animator != nullptr, "add animator");
        auto& cache = AnimationAssetManager::instance();
        SkeletonAsset skeleton;
        AnimationAssetBuilder builder;
        SkeletonResource raw;
        raw.bones.push_back({ 0, "SceneRoot", -1, Matrix::Identity, Matrix::Identity });
        raw.bones.push_back({ 1, "Motion", 0, Matrix::CreateTranslation(0, -1, 0),
            Matrix::CreateTranslation(0, 1, 0) });
        require(builder.buildSkeleton(raw, assets.skeleton->sourcePath, skeleton), "component skeleton");
        const auto skeletonHandle = cache.createSkeleton(std::move(skeleton));
        AnimationClipAsset clip;
        require(builder.buildClip(assets.animation, *cache.getSkeleton(skeletonHandle),
            assets.skeleton->sourcePath, clip), "component clip");
        clip.wrapMode = assets.clip->wrapMode;
        const auto clipHandle = cache.createClip(std::move(clip));
        require(animator->setAnimation(skeletonHandle, clipHandle), "set component animation");
        animator->setApplyRootMotion(true);
        return animator;
    }

    std::shared_ptr<AnimatorControllerAsset> controller(const Assets& assets,
        const std::shared_ptr<AnimationClipAsset>& destination)
    {
        auto result = std::make_shared<AnimatorControllerAsset>();
        result->guid = AssetGUID::generate();
        result->skeletonGuid = assets.skeleton->guid;
        result->skeletonSignature = assets.skeleton->signature;
        result->layerId = 1;
        result->layerName = "Base";
        result->defaultState = 1;
        result->states = { { 1, "Source", assets.clip->guid }, { 2, "Destination", destination->guid } };
        return result;
    }

    void runtimeTests()
    {
        test("disabled root motion preserves existing pose", []
            {
                Assets a(Vector3(1, 2, 3));
                AnimatorInstance i;
                a.bind(i, false);
                require(i.update(0.25f), "update");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "disabled delta");
                expectNear(i.getSnapshot()->constants.boneMatrices[0].Translation(), Vector3(0.25f, 0.5f, 0.75f), "original pose");
            });
        test("XYZ extraction removes motion from all bones", []
            {
                Assets a(Vector3(1, 2, 3));
                AnimatorInstance i;
                a.bind(i);
                require(i.update(0.25f), "update");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.25f, 0.5f, 0.75f), "XYZ delta");
                for (std::size_t bone = 0; bone < 2; ++bone)
                    expectNear(i.getSnapshot()->constants.boneMatrices[bone], Matrix::Identity, "no double motion");
            });
        test("loop forward reverse multiple wraps and negative deltaTime", []
            {
                Assets a;
                AnimatorInstance i;
                a.bind(i);
                for (const float step : { 0.75f, 0.5f, 3.25f, -4.5f, -0.25f, 100.0f })
                {
                    require(i.update(step), "loop update");
                    expectNear(i.getRootMotionDelta().Translation(), Vector3(step, 0, 0), "loop displacement");
                }
                i.setSpeed(-2.0f);
                require(i.update(1.25f), "negative speed");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(-2.5f, 0, 0), "speed displacement");
            });
        test("once clamps, stops, and reverses", []
            {
                Assets a(Vector3(1, 0, 0), AnimationWrapMode::Once);
                AnimatorInstance i;
                a.bind(i);
                require(i.update(2.0f), "once update");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(1, 0, 0), "once clamped");
                require(!i.isPlaying(), "once stops");
                require(i.update(1.0f), "stopped update");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "no stopped movement");
                require(i.play(1.0f), "seek end");
                require(i.update(-2.0f), "reverse once");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(-1, 0, 0), "reverse once clamped");
                require(!i.isPlaying(), "negative deltaTime stops once");
            });
        test("ping pong endpoints and full periods", []
            {
                Assets a(Vector3(1, 0, 0), AnimationWrapMode::PingPong);
                AnimatorInstance i;
                a.bind(i);
                for (const auto& [step, expected] : std::array<std::pair<float, float>, 5>{
                    { { 0.75f, 0.75f }, { 0.5f, 0.0f }, { 0.5f, -0.5f }, { 4.0f, 0.0f }, { -0.5f, 0.5f } } })
                {
                    require(i.update(step), "ping pong update");
                    expectNear(i.getRootMotionDelta().Translation(), Vector3(expected, 0, 0), "ping pong delta");
                }
            });
        test("pause seek enable and zero speed do not teleport", []
            {
                Assets a(Vector3(1, 0, 0), AnimationWrapMode::Loop, Quaternion::Identity, false, Vector3(10, 0, 0));
                AnimatorInstance i;
                a.bind(i);
                require(i.play(0.75f), "seek");
                require(i.update(0.0f), "seek evaluation");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "seek identity");
                require(i.update(0.5f), "after seek");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.5f, 0, 0), "offset not displacement");
                i.pause();
                require(i.update(1.0f), "pause update");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "pause identity");
                require(i.play(i.getNormalizedTime()), "resume");
                i.setSpeed(0.0f);
                require(i.update(1.0f), "zero speed");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "zero speed identity");
                require(i.configureRootMotion(false, {}), "disable");
                require(i.configureRootMotion(true, {}), "enable");
                require(i.update(0.0f), "enable evaluate");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "enable identity");
            });
        test("explicit child bone and missing joint validation", []
            {
                Assets a(Vector3(1, 0, 0), AnimationWrapMode::Loop, Quaternion::Identity, true);
                AnimatorInstance i;
                a.bind(i);
                require(i.update(0.5f), "group root update");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "static scene root");
                require(i.configureRootMotion(true, "Motion"), "select moving child");
                require(i.update(0.25f), "child update");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.25f, 0, 0), "child delta");
                expectNear(i.getSnapshot()->constants.boneMatrices[1], Matrix::Identity, "child motion removed");
                require(!i.configureRootMotion(true, "Missing"), "reject missing joint");
            });
        test("rotation and turning cycle accumulate as rigid transforms", []
            {
                const Quaternion turn = Quaternion::CreateFromYawPitchRoll(DirectX::XM_PIDIV2, 0, 0);
                Assets a(Vector3(1, 0, 0), AnimationWrapMode::Loop, turn);
                AnimatorInstance i;
                a.bind(i);
                const Matrix cycle = Matrix::CreateFromQuaternion(turn) * Matrix::CreateTranslation(1, 0, 0);
                require(i.update(2.0f), "two turning loops");
                expectNear(i.getRootMotionDelta(), cycle * cycle, "turning cycle composition");
                expectNear(i.getSnapshot()->constants.boneMatrices[0], Matrix::Identity, "rotation removed");
                require(i.update(-2.0f), "reverse turning loops");
                expectNear(i.getRootMotionDelta(), (cycle * cycle).Invert(), "reverse turning composition");
            });
        test("frame subdivision produces the same curved root trajectory", []
            {
                Assets a(Vector3(1, 0, 1), AnimationWrapMode::Loop,
                    Quaternion::CreateFromYawPitchRoll(1.0f, 0.3f, -0.2f));
                AnimatorInstance large;
                AnimatorInstance subdivided;
                a.bind(large);
                a.bind(subdivided);
                require(large.update(2.5f), "large update");
                Matrix accumulated = Matrix::Identity;
                for (int step = 0; step < 20; ++step)
                {
                    require(subdivided.update(0.125f), "small update");
                    accumulated = subdivided.getRootMotionDelta() * accumulated;
                }
                expectNear(accumulated, large.getRootMotionDelta(), "subdivision trajectory");
            });
        test("cross fade blends velocity without clip offset teleport", []
            {
                Assets a;
                Assets b(Vector3(3, 0, 0), AnimationWrapMode::Loop, Quaternion::Identity, false, Vector3(100, 0, 0));
                b.clip->skeletonGuid = a.skeleton->guid;
                b.clip->skeletonSignature = a.skeleton->signature;
                auto c = controller(a, b.clip);
                AnimatorInstance i;
                require(i.setController(a.skeleton, c, { a.clip, b.clip }), "bind controller");
                require(i.configureRootMotion(true, {}), "controller root motion");
                require(i.crossFade(2, 1.0f), "start crossfade");
                require(i.update(0.5f), "half transition");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.75f, 0, 0), "average blend weight");
                expectNear(i.getSnapshot()->constants.boneMatrices[0], Matrix::Identity, "blended motion removed");
                require(i.update(1.0f), "transition and remainder");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(2.75f, 0, 0), "transition overshoot weight");
                require(i.getCurrentState() == 2, "destination current");
                require(i.crossFade(2, 1.0f), "same state no-op");
                require(i.update(0.25f), "same state update");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.75f, 0, 0), "same state advances once");
            });
        test("automatic and immediate transitions do not extract pose jumps", []
            {
                Assets a;
                Assets b(Vector3(3, 0, 0), AnimationWrapMode::Loop, Quaternion::Identity, false, Vector3(50, 0, 0));
                b.clip->skeletonGuid = a.skeleton->guid;
                b.clip->skeletonSignature = a.skeleton->signature;
                auto c = controller(a, b.clip);
                c->transitions.push_back({ 1, 1, 2, 0.0f });
                AnimatorInstance i;
                require(i.setController(a.skeleton, c, { a.clip, b.clip }), "automatic controller");
                require(i.configureRootMotion(true, {}), "automatic root");
                require(i.update(0.25f), "automatic transition");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.25f, 0, 0), "source frame motion");
                require(i.update(0.25f), "destination motion");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.75f, 0, 0), "destination frame motion");
                require(i.play(1, 0.8f), "state seek");
                require(i.update(0), "state seek evaluation");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "state seek has no jump");
            });
        test("snapshot backpressure never loses movement time", []
            {
                Assets a;
                AnimatorInstance i;
                a.bind(i);
                auto first = i.getSnapshot();
                require(i.update(0.25f), "second snapshot");
                auto second = i.getSnapshot();
                require(i.update(0.25f), "third snapshot");
                auto third = i.getSnapshot();
                require(!i.update(0.25f), "backpressure fails");
                require(i.getNormalizedTime() == 0.5f, "time unchanged by backpressure");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "no stale delta on failure");
                first.reset();
                require(i.update(0.25f), "released snapshot");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.25f, 0, 0), "movement resumes");
            });
        test("invalid deltaTime rejects stale movement", []
            {
                Assets a;
                AnimatorInstance i;
                a.bind(i);
                require(i.update(0.25f), "valid update");
                require(!i.update(std::numeric_limits<float>::quiet_NaN()), "reject NaN");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "invalid update clears delta");
            });
        test("animated scale stays in the pose, not in root motion", []
            {
                Assets a(Vector3(1, 2, 3), AnimationWrapMode::Once, Quaternion::Identity,
                    false, Vector3::Zero, 2.0f);
                AnimatorInstance i;
                a.bind(i);
                require(i.update(1.0f), "scaled root update");
                Transform delta;
                require(Transform::tryFromMatrix(i.getRootMotionDelta(), delta), "rigid delta");
                expectNear(delta.getScale(), Vector3::One, "delta has unit scale");
                Transform pose;
                require(Transform::tryFromMatrix(i.getSnapshot()->constants.boneMatrices[0], pose), "scaled pose");
                expectNear(pose.getScale(), Vector3(2, 2, 2), "animated scale retained");
                expectNear(pose.getPosition(), Vector3::Zero, "scaled pose translation removed");
            });
        test("end seek zero time and rebind never replay prior root motion", []
            {
                Assets a;
                AnimatorInstance i;
                a.bind(i);
                require(i.play(1.0f), "seek normalized end");
                require(i.update(0), "end seek evaluate");
                require(i.getNormalizedTime() == 1, "end pose retained until time advances");
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "end seek no movement");
                require(i.update(0.25f), "end seek loop");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.25f, 0, 0), "end seek no extra cycle");
                a.bind(i);
                expectNear(i.getRootMotionDelta(), Matrix::Identity, "rebind clears delta");
            });
        test("long playback and reverse retrace without wrap drift", []
            {
                Assets a;
                AnimatorInstance i;
                a.bind(i);
                Matrix accumulated = Matrix::Identity;
                for (int step = 0; step < 2000; ++step)
                {
                    require(i.update(step < 1000 ? 0.125f : -0.125f), "long playback");
                    accumulated = i.getRootMotionDelta() * accumulated;
                }
                expectNear(accumulated, Matrix::Identity, "forward and reverse cancel");
            });
        test("controller state speed and wrap overrides affect root motion", []
            {
                Assets a;
                Assets b;
                b.clip->skeletonGuid = a.skeleton->guid;
                b.clip->skeletonSignature = a.skeleton->signature;
                auto c = controller(a, b.clip);
                c->states[0].speed = -2.0f;
                c->states[0].wrapOverride = AnimatorWrapOverride::PingPong;
                AnimatorInstance i;
                require(i.setController(a.skeleton, c, { a.clip, b.clip }), "speed controller");
                require(i.configureRootMotion(true, {}), "speed root");
                require(i.update(0.25f), "negative ping pong speed");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(0.5f, 0, 0), "wrap override and speed");
                i.setSpeed(-1);
                require(i.update(0.25f), "global speed");
                expectNear(i.getRootMotionDelta().Translation(), Vector3(-0.5f, 0, 0), "combined speeds");
            });
    }

    void integrationTests()
    {
        test("later gameplay callbacks configure root motion in the same frame", []
            {
                Assets a;
                Objects objects;
                auto* object = objects.manager.create("GameplayConfigured");
                auto* animator = addAnimator(*object, a);
                animator->setApplyRootMotion(false);
                require(ComponentRegistry::instance().registerType<EnableRootMotionComponent>(
                    "RootMotionTestEnable", false, false, true) != 0, "register gameplay test");
                require(object->addComponent<EnableRootMotionComponent>() != nullptr, "add gameplay callback");
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3(0.25f, 0, 0), "configuration applies immediately");
                expectNear(animator->getSkinningSnapshot()->constants.boneMatrices[0], Matrix::Identity, "pose stripped immediately");
            });
        test("GameObject applies XYZ after worker barrier and preserves scale", []
            {
                Assets a;
                Objects objects;
                auto* parent = objects.manager.create("Parent");
                parent->getTransform()->setPosition(Vector3(10, 2, 0));
                parent->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(DirectX::XM_PIDIV2, 0, 0));
                parent->getTransform()->setScale(Vector3(2, 3, 4));
                auto* child = objects.manager.create("Child");
                require(child->setParent(parent, false), "parent child");
                child->getTransform()->setScale(Vector3(2, 2, 2));
                auto* animator = addAnimator(*child, a);
                auto* other = objects.manager.create("Other");
                addAnimator(*other, a);
                const Matrix before = child->getWorldMatrix();
                const Vector3 expected = child->getWorldPosition()
                    + Vector3::TransformNormal(Vector3(0.25f, 0, 0), before);
                objects.manager.update(0.25f);
                expectNear(child->getWorldPosition(), expected, "world displacement respects parent and scale");
                expectNear(child->getTransform()->getScale(), Vector3(2, 2, 2), "scale unchanged");
                expectNear(other->getWorldPosition(), Vector3(0.25f, 0, 0), "parallel other displacement");
                expectNear(animator->getSkinningSnapshot()->constants.boneMatrices[0], Matrix::Identity, "in-place pose");
            });
        test("GameObject applies XYZ and full rotation in its local frame", []
            {
                const Quaternion turn = Quaternion::CreateFromYawPitchRoll(0.7f, 0.4f, -0.3f);
                Assets a(Vector3(1, 2, 3), AnimationWrapMode::Once, turn);
                Objects objects;
                auto* parent = objects.manager.create("RotatedParent");
                parent->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(-0.3f, 0.2f, 0));
                parent->getTransform()->setScale(Vector3(2, 3, 4));
                auto* object = objects.manager.create("FullMotion");
                require(object->setParent(parent, false), "rotation parent");
                const Quaternion initial = Quaternion::CreateFromYawPitchRoll(0.3f, 0, 0.1f);
                object->getTransform()->setRotation(initial);
                object->getTransform()->setScale(Vector3(2, 1, -1));
                addAnimator(*object, a);
                AnimatorInstance reference;
                a.bind(reference);
                require(reference.update(1), "reference full motion");
                const Matrix before = object->getWorldMatrix();
                const Matrix beforeRotation = Matrix::CreateFromQuaternion(object->getWorldRotation());
                const Vector3 expectedPosition = Vector3::TransformNormal(
                    reference.getRootMotionDelta().Translation(), before);
                Transform delta;
                require(Transform::tryFromMatrix(reference.getRootMotionDelta(), delta), "full motion delta");
                objects.manager.update(1.0f);
                expectNear(object->getWorldPosition(), expectedPosition, "full XYZ application");
                const Matrix expectedRotation = Matrix::CreateFromQuaternion(delta.getRotation())
                    * beforeRotation;
                expectNear(Matrix::CreateFromQuaternion(object->getWorldRotation()),
                    expectedRotation, "XYZ rotation applied without parent contamination");
                expectNear(object->getTransform()->getScale(), Vector3(2, 1, -1), "negative scale preserved");
            });
        test("editor preview and disabled animator never move objects", []
            {
                Assets a;
                Objects objects;
                auto* object = objects.manager.create("Preview");
                auto* animator = addAnimator(*object, a);
                objects.manager.submitRenderData(0.25f);
                expectNear(object->getWorldPosition(), Vector3::Zero, "preview no movement");
                expectNear(animator->getSkinningSnapshot()->constants.boneMatrices[0], Matrix::Identity, "preview stripped");
                animator->setEnabled(false);
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3::Zero, "disabled no movement");
                animator->setEnabled(true);
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3(0.25f, 0, 0), "no preview delta replay");
            });
        test("Dynamic Rigidbody rejects root motion, kinematic accepts it", []
            {
                Assets a;
                Objects objects;
                auto* object = objects.manager.create("Body");
                addAnimator(*object, a);
                auto* body = object->addComponent<RigidbodyComponent>();
                require(body != nullptr, "add body");
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3::Zero, "dynamic rejects");
                body->setKinematic(true);
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3(0.25f, 0, 0), "kinematic accepts");
            });
        test("CharacterController root motion collides instead of teleporting", []
            {
                Assets a;
                Objects objects;
                auto* wall = objects.manager.create("Wall");
                wall->getTransform()->setPosition(Vector3(2, 1, 0));
                auto* collider = wall->addComponent<BoxColliderComponent>();
                require(collider != nullptr, "wall collider");
                collider->setSize(Vector3(0.2f, 4, 4));
                auto* object = objects.manager.create("Character");
                addAnimator(*object, a);
                auto* character = object->addComponent<CharacterControllerComponent>();
                require(character != nullptr, "add character");
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3(0.25f, 0, 0), "controller moved");
                objects.manager.update(3.0f);
                const float x = object->getWorldPosition().x;
                require(x > 1.0f && x < 1.5f, "controller stopped at wall");
                PhysicsSystem::instance().setPaused(true);
                const Vector3 stopped = object->getWorldPosition();
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), stopped, "paused physics does not fall back to teleport");
                PhysicsSystem::instance().setPaused(false);
            });
        test("CharacterController retains small and vertical root motion", []
            {
                Assets a(Vector3(0.01f, 0.02f, 0));
                Objects objects;
                auto* object = objects.manager.create("SlowCharacter");
                addAnimator(*object, a);
                require(object->addComponent<CharacterControllerComponent>() != nullptr, "slow controller");
                objects.manager.update(0.025f);
                require(object->getWorldPosition().x > 0.0002f, "sub-millimeter root movement retained");
                require(object->getWorldPosition().y > 0.0004f, "vertical root movement retained");
                expectNear(object->getWorldPosition(), Vector3(0.00025f, 0.0005f, 0), "slow controller result");
            });
        test("CharacterController honors rotated scaled and moving parents", []
            {
                Assets a;
                Objects objects;
                auto* parent = objects.manager.create("ControllerParent");
                parent->getTransform()->setPosition(Vector3(10, 2, 0));
                parent->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(DirectX::XM_PIDIV2, 0, 0));
                parent->getTransform()->setScale(Vector3(2, 3, 4));
                auto* object = objects.manager.create("ParentedCharacter");
                require(object->setParent(parent, false), "controller parent");
                addAnimator(*object, a);
                require(object->addComponent<CharacterControllerComponent>() != nullptr, "parented controller");
                Vector3 expected = object->getWorldPosition()
                    + Vector3::TransformNormal(Vector3(0.25f, 0, 0), object->getWorldMatrix());
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), expected, "parented controller movement");
                parent->getTransform()->translate(Vector3(5, 0, 0));
                expected = object->getWorldPosition()
                    + Vector3::TransformNormal(Vector3(0.25f, 0, 0), object->getWorldMatrix());
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), expected, "controller follows parent changes");
            });
        test("invalid singular transform does not receive root motion", []
            {
                Assets a;
                Objects objects;
                auto* object = objects.manager.create("Singular");
                object->getTransform()->setScale(Vector3(0, 1, 1));
                addAnimator(*object, a);
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3::Zero, "singular setup rejected");
                object->getTransform()->setScale(Vector3::One);
                objects.manager.update(0.25f);
                expectNear(object->getWorldPosition(), Vector3(0.25f, 0, 0), "valid setup resumes without backlog");
            });
        test("root joint configuration clone and v1/v2 payload compatibility", []
            {
                Assets a;
                Objects objects;
                auto* object = objects.manager.create("Persisted");
                auto* animator = addAnimator(*object, a);
                require(animator->setRootMotionJoint("Motion"), "set root joint");
                require(!animator->setRootMotionJoint("Absent"), "reject invalid bone");
                auto* copyObject = objects.manager.create("Clone");
                auto* copy = addAnimator(*copyObject, a);
                require(animator->copyConfigurationTo(*copy), "copy config");
                require(copy->getRootMotionJoint() == "Motion" && copy->getApplyRootMotion(), "clone settings");

                auto& cache = AnimationAssetManager::instance();
                const auto directory = std::filesystem::temp_directory_path();
                const auto suffix = std::to_string(GetCurrentProcessId());
                const auto skeletonPath = directory / ("root-motion-" + suffix + ".skel");
                const auto clipPath = directory / ("root-motion-" + suffix + ".anim");
                require(cache.saveSkeleton(animator->getSkeleton(), skeletonPath), "save skeleton");
                require(cache.saveClip(animator->getClip(), clipPath), "save clip");
                std::vector<std::uint8_t> payload;
                require(animator->serializePayload(payload), "serialize v2");
                require(copy->deserializePayload(Serialization::CURRENT_ANIMATOR_COMPONENT_VERSION, payload), "load v2");
                require(copy->getRootMotionJoint() == "Motion" && copy->getApplyRootMotion(), "v2 settings");

                flatbuffers::FlatBufferBuilder builder;
                const Serialization::AssetGuid empty(0, 0);
                const auto old = Serialization::CreateAnimatorComponentPayload(builder, &empty, &empty, &empty);
                Serialization::FinishAnimatorComponentPayloadBuffer(builder, old);
                require(copy->deserializePayload(1, { builder.GetBufferPointer(), builder.GetSize() }), "load v1");
                require(copy->getRootMotionJoint().empty() && !copy->getApplyRootMotion(), "v1 defaults");
                require(std::filesystem::remove(skeletonPath), "remove temporary skeleton");
                require(std::filesystem::remove(clipPath), "remove temporary clip");
            });
    }
}

int main()
{
    using namespace Engine;
    LoggerConfig logging;
    logging.useFile = false;
    logging.useDebugOutput = false;
    if (!Logger::instance().initialize(logging) || !JobSystem::instance().initialize(2)
        || !PhysicsSystem::instance().initialize())
        return 1;
    int result = 0;
    try
    {
        runtimeTests();
        integrationTests();
        std::printf("%d root motion tests passed.\n", passed);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAILED after %d tests: %s\n", passed, error.what());
        result = 1;
    }
    PhysicsSystem::instance().shutdown();
    JobSystem::instance().finalize();
    Logger::instance().finalize();
    return result;
}
