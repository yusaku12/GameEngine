#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "Animation/Runtime/AnimatorInstance.h"
#include "Assets/Animation/AnimationAssetBuilder.h"
#include "Assets/Animation/Serialization/AnimationClipSerializer.h"
#include "Assets/Animation/Serialization/AnimatorControllerSerializer.h"
#include "Assets/Animation/Serialization/SkeletonSerializer.h"
#include "Generated/FlatBuffers/AnimationClip_generated.h"

namespace
{
    bool checkAssetTest(const bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

bool runAnimationAssetRoundTripTests()
{
    Engine::SkeletonResource sourceSkeleton;
    sourceSkeleton.bones.push_back({ 0, "root", -1, Engine::Matrix::Identity, Engine::Matrix::Identity });
    sourceSkeleton.boneMap.emplace("root", 0);

    Engine::AnimationResource sourceClip;
    sourceClip.name = "Move";
    sourceClip.duration = 1.0f;
    sourceClip.channels.resize(1);
    sourceClip.channels[0].nodeName = "root";
    sourceClip.channels[0].positions.push_back({ Engine::Vector3::Zero, 0.0f });
    sourceClip.channels[0].positions.push_back({ Engine::Vector3(2.0f, 0.0f, 0.0f), 1.0f });
    sourceClip.channels[0].rotations.push_back({ Engine::Quaternion::Identity, 0.0f });
    sourceClip.channels[0].rotations.push_back({
        Engine::Quaternion::CreateFromAxisAngle(Engine::Vector3::UnitZ, DirectX::XM_PIDIV2), 1.0f });

    Engine::AnimationAssetBuilder builder;
    Engine::SkeletonAsset skeleton;
    Engine::AnimationClipAsset clip;
    const std::filesystem::path sourcePath = "Assets/Model/Test.fbx";
    if (!checkAssetTest(builder.buildSkeleton(sourceSkeleton, sourcePath, skeleton), "Skeleton asset build failed.")
        || !checkAssetTest(builder.buildClip(sourceClip, skeleton, sourcePath, clip), "Animation clip build failed."))
        return false;
        Engine::AnimationClipAsset fastClip;
        sourceClip.name = "FastMove";
        sourceClip.channels[0].positions[1].value = Engine::Vector3(4.0f, 0.0f, 0.0f);
        if (!checkAssetTest(builder.buildClip(sourceClip, skeleton, sourcePath, fastClip), "Fast animation clip build failed."))
            return false;

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "GameEngineAnimationTests";
    const std::filesystem::path skeletonPath = directory / "test.skeleton";
    const std::filesystem::path clipPath = directory / "test.animclip";
    const std::filesystem::path controllerPath = directory / "test.animcontroller";
    Engine::Serialization::SkeletonSerializer skeletonSerializer;
    Engine::Serialization::AnimationClipSerializer clipSerializer;
    Engine::Serialization::AnimatorControllerSerializer controllerSerializer;
    if (!checkAssetTest(skeletonSerializer.save(skeletonPath, skeleton), "Skeleton asset save failed.")
        || !checkAssetTest(clipSerializer.save(clipPath, clip), "Animation clip save failed."))
        return false;

    Engine::AnimatorControllerAsset controller;
    controller.guid = { 0x1234, 0x5678 };
    controller.name = "Locomotion";
    controller.skeletonGuid = skeleton.guid;
    controller.skeletonSignature = skeleton.signature;
    controller.layerId = Engine::makeAnimatorID("Base Layer");
    controller.layerName = "Base Layer";
    controller.defaultState = Engine::makeAnimatorID("Idle");
    controller.parameters = {
        { Engine::makeAnimatorID("Speed"), "Speed", Engine::AnimatorParameterType::Float, 0.25f },
        { Engine::makeAnimatorID("Stance"), "Stance", Engine::AnimatorParameterType::Int, 0.0f, 2 },
        { Engine::makeAnimatorID("Grounded"), "Grounded", Engine::AnimatorParameterType::Bool, 0.0f, 0, true },
        { Engine::makeAnimatorID("Jump"), "Jump", Engine::AnimatorParameterType::Trigger },
    };
    controller.states = {
        { controller.defaultState, "Idle", clip.guid },
        { Engine::makeAnimatorID("Move"), "Move", fastClip.guid, 1.0f, Engine::AnimatorWrapOverride::Loop },
    };
    controller.transitions = {
        { Engine::makeAnimatorID("IdleToMove"), controller.defaultState, Engine::makeAnimatorID("Move"), 0.2f, 0.75f, true, false,
            {
                { Engine::makeAnimatorID("Speed"), Engine::AnimatorConditionMode::Greater, 0.5f },
                { Engine::makeAnimatorID("Stance"), Engine::AnimatorConditionMode::Equals, 0.0f, 2 },
                { Engine::makeAnimatorID("Grounded"), Engine::AnimatorConditionMode::If },
                { Engine::makeAnimatorID("Jump"), Engine::AnimatorConditionMode::Triggered },
            } },
        { Engine::makeAnimatorID("AnyToIdle"), 0, controller.defaultState, 0.1f, 0.0f, false, true,
            { { Engine::makeAnimatorID("Grounded"), Engine::AnimatorConditionMode::IfNot } } },
    };
    if (!checkAssetTest(controllerSerializer.save(controllerPath, controller), "Controller asset save failed."))
        return false;

    Engine::SkeletonAsset loadedSkeleton;
    Engine::AnimationClipAsset loadedClip;
    Engine::AnimatorControllerAsset loadedController;
    const bool valid = checkAssetTest(skeletonSerializer.load(skeletonPath, loadedSkeleton), "Skeleton asset load failed.")
        && checkAssetTest(clipSerializer.load(clipPath, loadedClip), "Animation clip load failed.")
        && checkAssetTest(controllerSerializer.load(controllerPath, loadedController), "Controller asset load failed.")
        && checkAssetTest(loadedSkeleton.guid == skeleton.guid, "Skeleton GUID changed.")
        && checkAssetTest(loadedSkeleton.signature == skeleton.signature, "Skeleton signature changed.")
        && checkAssetTest(loadedSkeleton.skeleton.num_joints() == 1, "Skeleton joint count changed.")
        && checkAssetTest(loadedClip.guid == clip.guid, "Animation clip GUID changed.")
        && checkAssetTest(loadedClip.skeletonSignature == skeleton.signature, "Animation clip signature changed.")
        && checkAssetTest(loadedClip.animation.num_tracks() == 1, "Animation track count changed.")
        && checkAssetTest(loadedClip.duration == 1.0f, "Animation duration changed.")
        && checkAssetTest(loadedController.guid == controller.guid, "Controller GUID changed.")
        && checkAssetTest(loadedController.layerId == controller.layerId, "Controller layer changed.")
        && checkAssetTest(loadedController.defaultState == controller.defaultState, "Controller default state changed.")
        && checkAssetTest(loadedController.parameters.size() == 4, "Controller parameters changed.")
        && checkAssetTest(loadedController.states.size() == 2, "Controller states changed.")
        && checkAssetTest(loadedController.transitions.size() == 2, "Controller transitions changed.")
        && checkAssetTest(loadedController.transitions[0].conditions[0].floatThreshold == 0.5f,
            "Controller condition changed.")
        && checkAssetTest(loadedController.transitions[1].anyState, "Controller transition order changed.");

    if (valid)
    {
        auto runtimeSkeleton = std::make_shared<const Engine::SkeletonAsset>(std::move(loadedSkeleton));
        auto runtimeClip = std::make_shared<const Engine::AnimationClipAsset>(std::move(loadedClip));
        auto runtimeFastClip = std::make_shared<const Engine::AnimationClipAsset>(std::move(fastClip));
        auto runtimeController = std::make_shared<const Engine::AnimatorControllerAsset>(std::move(loadedController));
        Engine::AnimatorInstance animator;
        if (!checkAssetTest(animator.setAssets(runtimeSkeleton, runtimeClip), "Animator asset binding failed."))
            return false;
        auto snapshot = animator.getSnapshot();
        if (!checkAssetTest(snapshot != nullptr, "Animator bind-pose snapshot was not published.")
            || !checkAssetTest(std::abs(snapshot->constants.boneMatrices[0]._41) < 0.001f,
                "Animator bind-pose palette was not identity.")
            || !checkAssetTest(animator.update(0.5f), "Animator evaluation failed."))
            return false;
        snapshot = animator.getSnapshot();
        const Engine::Vector3 rotatedNormal = Engine::Vector3::TransformNormal(
            Engine::Vector3::UnitX, snapshot->constants.boneNormalMatrices[0]);
        if (!checkAssetTest(std::abs(snapshot->constants.boneMatrices[0]._41 - 1.0f) < 0.001f,
                "Animator sampled an unexpected translation.")
            || !checkAssetTest(std::abs(rotatedNormal.x - 0.7071f) < 0.001f
                && std::abs(rotatedNormal.y - 0.7071f) < 0.001f,
                "Animator sampled an unexpected normal rotation."))
            return false;
        if (!checkAssetTest(animator.update(0.75f), "Animator loop evaluation failed."))
            return false;
        snapshot = animator.getSnapshot();
        if (!checkAssetTest(std::abs(snapshot->constants.boneMatrices[0]._41 - 0.5f) < 0.001f,
                "Animator loop wrapping failed."))
            return false;
        animator.setSpeed(-1.0f);
        if (!checkAssetTest(animator.update(0.5f), "Animator reverse evaluation failed."))
            return false;
        snapshot = animator.getSnapshot();
        if (!checkAssetTest(std::abs(snapshot->constants.boneMatrices[0]._41 - 1.5f) < 0.001f,
                "Animator negative speed wrapping failed."))
            return false;

        Engine::AnimationClipAsset onceClip;
        if (!checkAssetTest(builder.buildClip(sourceClip, *runtimeSkeleton, sourcePath, onceClip),
                "Once animation clip build failed."))
            return false;
        onceClip.wrapMode = Engine::AnimationWrapMode::Once;
        animator.setSpeed(1.0f);
        if (!checkAssetTest(animator.setAssets(runtimeSkeleton,
                std::make_shared<const Engine::AnimationClipAsset>(std::move(onceClip))),
                "Once animation binding failed.")
            || !checkAssetTest(animator.update(2.0f), "Once animation evaluation failed.")
            || !checkAssetTest(!animator.isPlaying() && animator.getNormalizedTime() == 1.0f,
                "Once animation did not stop at the end."))
            return false;

        Engine::AnimationClipAsset pingPongClip;
        if (!checkAssetTest(builder.buildClip(sourceClip, *runtimeSkeleton, sourcePath, pingPongClip),
                "Ping-pong animation clip build failed."))
            return false;
        pingPongClip.wrapMode = Engine::AnimationWrapMode::PingPong;
        if (!checkAssetTest(animator.setAssets(runtimeSkeleton,
                std::make_shared<const Engine::AnimationClipAsset>(std::move(pingPongClip))),
                "Ping-pong animation binding failed.")
            || !checkAssetTest(animator.update(1.5f), "Ping-pong animation evaluation failed.")
            || !checkAssetTest(std::abs(animator.getNormalizedTime() - 0.5f) < 0.001f,
                "Ping-pong animation did not reverse."))
            return false;

        Engine::AnimatorInstance controllerAnimator;
        if (!checkAssetTest(controllerAnimator.setController(runtimeSkeleton, runtimeController,
                { runtimeClip, runtimeFastClip }), "Controller binding failed.")
            || !checkAssetTest(controllerAnimator.getCurrentState() == Engine::makeAnimatorID("Idle"),
                "Controller default state was not selected.")
            || !checkAssetTest(controllerAnimator.setFloat(Engine::makeAnimatorID("Speed"), 1.0f),
                "Float parameter was rejected.")
            || !checkAssetTest(controllerAnimator.setInt(Engine::makeAnimatorID("Stance"), 2),
                "Int parameter was rejected.")
            || !checkAssetTest(controllerAnimator.setBool(Engine::makeAnimatorID("Grounded"), true),
                "Bool parameter was rejected.")
            || !checkAssetTest(controllerAnimator.setTrigger(Engine::makeAnimatorID("Jump")),
                "Trigger parameter was rejected.")
            || !checkAssetTest(controllerAnimator.update(0.5f), "Controller pre-exit evaluation failed.")
            || !checkAssetTest(controllerAnimator.getTransitionProgress() == 0.0f,
                "Controller transitioned before exit time.")
            || !checkAssetTest(controllerAnimator.update(0.3f), "Controller transition start failed.")
            || !checkAssetTest(controllerAnimator.getTransitionProgress() == 0.0f,
                "Controller transition did not start deterministically.")
            || !checkAssetTest(controllerAnimator.update(0.1f), "Controller cross-fade midpoint failed."))
            return false;
        snapshot = controllerAnimator.getSnapshot();
        if (!checkAssetTest(std::abs(controllerAnimator.getTransitionProgress() - 0.5f) < 0.001f,
                "Controller cross-fade progress changed.")
            || !checkAssetTest(std::abs(snapshot->constants.boneMatrices[0]._41 - 1.1f) < 0.001f,
                "Controller cross-fade midpoint pose changed.")
            || !checkAssetTest(controllerAnimator.update(0.1f), "Controller transition completion failed.")
            || !checkAssetTest(controllerAnimator.getCurrentState() == Engine::makeAnimatorID("Move")
                && controllerAnimator.getTransitionProgress() == 0.0f,
                "Controller transition did not complete."))
            return false;
        if (!checkAssetTest(controllerAnimator.setBool(Engine::makeAnimatorID("Grounded"), false),
                "Any State bool parameter was rejected.")
            || !checkAssetTest(controllerAnimator.update(0.01f), "Any State transition failed.")
            || !checkAssetTest(controllerAnimator.update(0.01f), "Any State transition evaluation failed.")
            || !checkAssetTest(controllerAnimator.getTransitionProgress() > 0.0f,
                "Any State transition was not selected."))
            return false;

        std::ifstream input(clipPath, std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        input.close();
        const auto* serialized = Engine::Serialization::GetAnimationClipFile(bytes.data());
        const auto* archive = serialized->clip()->ozz_archive();
        const std::size_t archiveOffset = static_cast<std::size_t>(archive->data() - bytes.data());
        bytes[archiveOffset + archive->size() / 2] ^= 0x7f;
        std::ofstream output(clipPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        Engine::AnimationClipAsset corrupted;
        if (!checkAssetTest(!clipSerializer.load(clipPath, corrupted), "Corrupted animation asset was accepted."))
            return false;
    }

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (!valid)
        std::cerr << "Animation asset round trip failed.\n";
    return valid;
}
