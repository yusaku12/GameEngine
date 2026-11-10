#include "Pch.h"
#include <fbxsdk.h>
#include <iostream>
#include <numbers>
#include <ozz/animation/runtime/local_to_model_job.h>
#include "Animation\Runtime\AnimatorInstance.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Model\Import\FbxModelImporter.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\Transform.h"
#include "Graphics\Renderer\SkinningBounds.h"

namespace
{
    using namespace Engine;

    void require(const bool condition, const std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }

    float matrixError(const Matrix& left, const Matrix& right)
    {
        float error = 0.0f;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
            {
                const float difference = std::abs(left.m[row][column] - right.m[row][column]);
                require(std::isfinite(difference), "Nonfinite matrix.");
                error = std::max(error, difference);
            }
        return error;
    }

    Matrix rigid(Matrix matrix)
    {
        Vector3 scale;
        Vector3 position;
        Quaternion rotation;
        require(matrix.Decompose(scale, rotation, position), "Rigid decomposition failed.");
        rotation.Normalize();
        return Matrix::CreateFromQuaternion(rotation) * Matrix::CreateTranslation(position);
    }

    Matrix power(Matrix matrix, int exponent)
    {
        if (exponent < 0)
            matrix = matrix.Invert();
        Matrix result = Matrix::Identity;
        for (int count = 0; count < std::abs(exponent); ++count)
            result *= matrix;
        return result;
    }

    Matrix toMatrix(const ozz::math::Float4x4& value)
    {
        Matrix result;
        for (std::size_t column = 0; column < 4; ++column)
            ozz::math::StorePtrU(value.cols[column], result.m[column]);
        return result;
    }

    std::vector<Matrix> restModels(const SkeletonAsset& skeleton)
    {
        std::vector<ozz::math::Float4x4> models(skeleton.jointNames.size());
        ozz::animation::LocalToModelJob job;
        job.skeleton = &skeleton.skeleton;
        job.input = skeleton.skeleton.joint_rest_poses();
        job.output = ozz::make_span(models);
        require(job.Run(), "Rest hierarchy evaluation failed.");
        std::vector<Matrix> result;
        for (const auto& model : models)
            result.push_back(toMatrix(model));
        return result;
    }

    void checkPalette(const SkinningPaletteSnapshot& pose)
    {
        for (std::size_t joint = 0; joint < pose.jointCount; ++joint)
        {
            const Matrix& matrix = pose.constants.boneMatrices[joint];
            require(matrix._14 == 0.0f && matrix._24 == 0.0f && matrix._34 == 0.0f && matrix._44 == 1.0f,
                "Palette lost its exact affine column.");
            matrixError(matrix, matrix);
            matrixError(pose.constants.boneNormalMatrices[joint], pose.constants.boneNormalMatrices[joint]);
        }
    }

    void comparePalettes(const SkinningPaletteSnapshot& plain, const SkinningPaletteSnapshot& extracted,
        const Matrix& world, const float tolerance)
    {
        checkPalette(extracted);
        for (std::size_t joint = 0; joint < plain.jointCount; ++joint)
            require(matrixError(plain.constants.boneMatrices[joint],
                extracted.constants.boneMatrices[joint] * world) <= tolerance,
                "Root extraction changed the world-space pose.");
    }

    struct Rig
    {
        std::shared_ptr<SkeletonAsset> skeleton;
        std::shared_ptr<AnimationClipAsset> clip;
    };

    Rig makeRig(const AnimationWrapMode wrap)
    {
        SkeletonResource resource;
        const std::array locals{
            Matrix::CreateScale(0.01f) * Matrix::CreateRotationY(0.35f) * Matrix::CreateTranslation(0.4f, 0.1f, 0.2f),
            Matrix::CreateRotationX(-std::numbers::pi_v<float> / 2.0f),
            Matrix::CreateTranslation(0.0f, 40.0f, 0.0f)
        };
        const std::array names{ "Group", "root", "Child" };
        Matrix model = Matrix::Identity;
        for (std::uint32_t joint = 0; joint < names.size(); ++joint)
        {
            model = locals[joint] * model;
            Matrix inverseBind = model.Invert();
            inverseBind._14 = inverseBind._24 = inverseBind._34 = 0.0f;
            inverseBind._44 = 1.0f;
            resource.bones.push_back({ joint, names[joint], static_cast<std::int32_t>(joint) - 1,
                inverseBind, locals[joint] });
        }
        Rig rig{ std::make_shared<SkeletonAsset>(), std::make_shared<AnimationClipAsset>() };
        AnimationAssetBuilder builder;
        require(builder.buildSkeleton(resource, "Synthetic.fbx", *rig.skeleton), "Synthetic skeleton build failed.");
        AnimationResource animation;
        animation.name = "TurnAndTravel";
        animation.duration = 1.0f;
        AnimationChannel root;
        root.nodeName = "root";
        AnimationChannel child;
        child.nodeName = "Child";
        for (int frame = 0; frame <= 60; ++frame)
        {
            const float time = static_cast<float>(frame) / 60.0f;
            root.positions.push_back({ Vector3(10.0f + 50.0f * time, 20.0f + 5.0f * time, 30.0f - 15.0f * time), time });
            root.rotations.push_back({ Quaternion::CreateFromYawPitchRoll(0.4f + 0.8f * time, 0.2f, -0.1f), time });
            child.rotations.push_back({ Quaternion::CreateFromAxisAngle(Vector3::UnitZ, 0.25f * std::sin(time)), time });
        }
        animation.channels.push_back(std::move(root));
        animation.channels.push_back(std::move(child));
        require(builder.buildClip(animation, *rig.skeleton, "Synthetic.fbx", *rig.clip), "Synthetic clip build failed.");
        rig.clip->wrapMode = wrap;
        return rig;
    }

    void seek(AnimatorInstance& instance, const float ratio)
    {
        require(instance.play(ratio) && instance.update(0.0f), "Seek evaluation failed.");
    }

    void testMotion()
    {
        int cases = 0;
        for (const AnimationWrapMode wrap : { AnimationWrapMode::Once, AnimationWrapMode::Loop, AnimationWrapMode::PingPong })
            for (const std::string_view root : { "", "root", "Child" })
                for (const float start : { 0.0f, 0.37f })
                    for (const float step : { 1.0f / 60.0f, 1.0f / 144.0f, 0.173f, 3.25f, -0.137f, -2.35f })
                    {
                        const Rig rig = makeRig(wrap);
                        const auto rest = restModels(*rig.skeleton);
                        const std::size_t joint = root.empty() ? 0 : rig.skeleton->jointLookup.at(std::string(root));
                        AnimatorInstance plain;
                        AnimatorInstance moving;
                        require(plain.setAssets(rig.skeleton, rig.clip) && moving.setAssets(rig.skeleton, rig.clip),
                            "Asset binding failed.");
                        seek(plain, start);
                        seek(moving, start);
                        const Matrix reference = rigid(rest[joint] * plain.getSnapshot()->constants.boneMatrices[joint]);
                        require(moving.configureRootMotion(true, root) && moving.update(0.0f), "Root configuration failed.");
                        comparePalettes(*plain.getSnapshot(), *moving.getSnapshot(), Matrix::Identity, 0.00002f);
                        seek(plain, 0.0f);
                        const Matrix beginning = rigid(rest[joint] * plain.getSnapshot()->constants.boneMatrices[joint]);
                        seek(plain, 1.0f);
                        const Matrix ending = rigid(rest[joint] * plain.getSnapshot()->constants.boneMatrices[joint]);
                        Matrix accumulated = Matrix::Identity;
                        double time = start;
                        for (int frame = 0; frame < 32; ++frame)
                        {
                            if (wrap == AnimationWrapMode::Once)
                                time = std::clamp(time + step, 0.0, 1.0);
                            else
                                time += step;
                            double sample = time;
                            int cycles = 0;
                            if (wrap == AnimationWrapMode::Loop)
                            {
                                cycles = static_cast<int>(std::floor(time));
                                sample -= cycles;
                            }
                            else if (wrap == AnimationWrapMode::PingPong)
                            {
                                sample = std::fmod(time, 2.0);
                                if (sample < 0.0) sample += 2.0;
                                if (sample > 1.0) sample = 2.0 - sample;
                            }
                            seek(plain, static_cast<float>(sample));
                            require(moving.update(step), "Root-motion update failed.");
                            accumulated = moving.getRootMotionDelta() * accumulated;
                            const Matrix current = rigid(rest[joint] * plain.getSnapshot()->constants.boneMatrices[joint]);
                            const Matrix expected = reference.Invert() * current * power(beginning.Invert() * ending, cycles);
                            require(matrixError(accumulated, expected) < 0.003f, "Root trajectory disagrees with independent reference.");
                            if (cycles == 0)
                                comparePalettes(*plain.getSnapshot(), *moving.getSnapshot(), accumulated, 0.003f);
                            checkPalette(*moving.getSnapshot());
                        }
                        ++cases;
                    }
        std::cout << "PASS " << cases << " analytic motion cases (nonidentity bind/start, scaled hierarchy, "
            "all wraps, reverse, multiple loops, two start times, three root joints).\n";
    }

    void testReconfigurationAndController()
    {
        const Rig rig = makeRig(AnimationWrapMode::Loop);
        AnimatorInstance instance;
        require(instance.setAssets(rig.skeleton, rig.clip), "Binding failed.");
        seek(instance, 0.37f);
        auto before = instance.getSnapshot();
        require(instance.configureRootMotion(true, "root") && instance.update(0), "Mid-clip enable failed.");
        comparePalettes(*before, *instance.getSnapshot(), Matrix::Identity, 0.00002f);
        require(instance.update(0.1f), "Advance failed.");
        before = instance.getSnapshot();
        require(instance.configureRootMotion(true, "Child") && instance.update(0), "Joint change failed.");
        comparePalettes(*before, *instance.getSnapshot(), Matrix::Identity, 0.00002f);
        instance.pause();
        require(instance.update(0.2f), "Paused evaluation failed.");
        require(matrixError(instance.getRootMotionDelta(), Matrix::Identity) == 0.0f, "Pause generated movement.");

        auto controller = std::make_shared<AnimatorControllerAsset>();
        controller->skeletonGuid = rig.skeleton->guid;
        controller->skeletonSignature = rig.skeleton->signature;
        controller->layerId = 1;
        controller->defaultState = 1;
        controller->states.push_back({ 1, "A", rig.clip->guid });
        controller->states.push_back({ 2, "B", rig.clip->guid });
        AnimatorInstance plain;
        AnimatorInstance moving;
        require(plain.setController(rig.skeleton, controller, { rig.clip, rig.clip })
            && moving.setController(rig.skeleton, controller, { rig.clip, rig.clip }), "Controller binding failed.");
        require(plain.play(1, 0.37f) && plain.update(0) && moving.play(1, 0.37f)
            && moving.configureRootMotion(true, "root") && moving.update(0), "Controller restore failed.");
        comparePalettes(*plain.getSnapshot(), *moving.getSnapshot(), Matrix::Identity, 0.00002f);
        require(plain.crossFade(2, 0.25f) && moving.crossFade(2, 0.25f), "Crossfade failed.");
        for (int frame = 0; frame < 120; ++frame)
        {
            require(plain.update(1.0f / 60.0f) && moving.update(1.0f / 60.0f), "Crossfade evaluation failed.");
            checkPalette(*moving.getSnapshot());
        }
        AnimatorInstance buffered;
        require(buffered.setAssets(rig.skeleton, rig.clip) && buffered.configureRootMotion(true, "root"),
            "Buffered instance setup failed.");
        std::vector<std::shared_ptr<const SkinningPaletteSnapshot>> held;
        held.push_back(buffered.getSnapshot());
        const Matrix firstMatrix = held.front()->constants.boneMatrices[1];
        for (int frame = 0; frame < 2; ++frame)
        {
            require(buffered.update(0.1f), "Snapshot acquisition failed.");
            held.push_back(buffered.getSnapshot());
        }
        const float time = buffered.getNormalizedTime();
        require(!buffered.update(0.1f) && buffered.getNormalizedTime() == time,
            "Snapshot backpressure advanced animation time.");
        require(matrixError(firstMatrix, held.front()->constants.boneMatrices[1]) == 0.0f,
            "Published snapshot was mutated.");
        held.erase(held.begin());
        require(buffered.update(0.1f), "Snapshot backpressure did not recover.");
        std::cout << "PASS mid-clip enable, root-joint changes, pause, restored controller, crossfade and snapshot backpressure.\n";
    }

    struct FbxDestroy
    {
        template<class T> void operator()(T* object) const { if (object != nullptr) object->Destroy(); }
    };

    struct Fixture
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("GameEngineRootMotion-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::path path = directory / "Wrapped.fbx";

        Fixture() { require(std::filesystem::create_directory(directory), "Cannot create fixture directory."); }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error) std::cerr << "Fixture cleanup failed: " << error.message() << '\n';
            std::filesystem::remove(directory, error);
            if (error) std::cerr << "Fixture directory cleanup failed: " << error.message() << '\n';
        }
    };

    void testEulerImport()
    {
        const Fixture fixture;
        int cases = 0;
        for (const EFbxRotationOrder order : { eEulerXYZ, eEulerXZY, eEulerYXZ, eEulerYZX, eEulerZXY, eEulerZYX })
        for (const std::size_t rotatingAxis : { 0u, 1u, 2u })
        for (const bool sparse : { false, true })
        {
            std::unique_ptr<FbxManager, FbxDestroy> manager(FbxManager::Create());
            manager->SetIOSettings(FbxIOSettings::Create(manager.get(), IOSROOT));
            FbxScene* scene = FbxScene::Create(manager.get(), "Fixture");
            scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem(FbxAxisSystem::eYAxis,
                FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded));
            scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit(100.0));
            FbxNode* node = FbxNode::Create(scene, "Joint");
            node->SetRotationOrder(FbxNode::eSourcePivot, order);
            const FbxDouble3 defaults(17.0, 23.0, -31.0);
            node->LclRotation.Set(defaults);
            scene->GetRootNode()->AddChild(node);
            FbxAnimStack* stack = FbxAnimStack::Create(scene, "Wrapped");
            FbxAnimLayer* layer = FbxAnimLayer::Create(scene, "Layer");
            stack->AddMember(layer);
            FbxTime start, end;
            start.SetSecondDouble(0);
            end.SetSecondDouble(1);
            FbxTimeSpan span(start, end);
            stack->SetLocalTimeSpan(span);
            const std::array axes{ FBXSDK_CURVENODE_COMPONENT_X, FBXSDK_CURVENODE_COMPONENT_Y, FBXSDK_CURVENODE_COMPONENT_Z };
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                if (sparse && axis != rotatingAxis)
                    continue;
                auto* curve = node->LclRotation.GetCurve(layer, axes[axis], true);
                curve->KeyModifyBegin();
                for (int index = 0; index < 3; ++index)
                {
                    FbxTime time;
                    time.SetSecondDouble(index * 0.5);
                    const int key = curve->KeyAdd(time);
                    curve->KeySetValue(key, axis == rotatingAxis
                        ? (index == 0 ? 359.0f : static_cast<float>(index - 1))
                        : static_cast<float>(defaults[static_cast<int>(axis)]));
                    curve->KeySetInterpolation(key, FbxAnimCurveDef::eInterpolationCubic);
                    curve->KeySetTangentMode(key, FbxAnimCurveDef::eTangentAuto);
                }
                curve->KeyModifyEnd();
            }
            scene->SetCurrentAnimationStack(stack);
            FbxAMatrix reflection;
            reflection.SetIdentity();
            reflection.SetS(FbxVector4(-1, 1, 1, 1));
            std::array<Quaternion, 3> authored;
            for (std::size_t key = 0; key < authored.size(); ++key)
            {
                FbxTime time;
                time.SetSecondDouble(static_cast<double>(key) * 0.5);
                const FbxQuaternion value = (reflection * node->EvaluateLocalTransform(time) * reflection).GetQ();
                authored[key] = Quaternion(static_cast<float>(value[0]), static_cast<float>(value[1]),
                    static_cast<float>(value[2]), static_cast<float>(value[3]));
            }
            std::unique_ptr<FbxExporter, FbxDestroy> exporter(FbxExporter::Create(manager.get(), ""));
            require(exporter->Initialize(fixture.path.string().c_str(), -1, manager->GetIOSettings())
                && exporter->Export(scene), "Cannot save generated FBX fixture.");
            const auto imported = FbxModelImporter{}.importAnimations(fixture.path);
            require(imported.size() == 1, "Generated FBX import failed.");
            const auto found = std::find_if(imported[0].channels.begin(), imported[0].channels.end(),
                [](const auto& channel) { return channel.nodeName == "RootNode/Joint"; });
            require(found != imported[0].channels.end(), "Missing fixture channel.");
            for (std::size_t key = 0; key < authored.size(); ++key)
            {
                const float time = static_cast<float>(key) * 0.5f;
                const auto sample = std::find_if(found->rotations.begin(), found->rotations.end(),
                    [time](const AnimationKeyRotation& value) { return std::abs(value.time - time) < 0.00001f; });
                require(sample != found->rotations.end() && std::abs(sample->value.Dot(authored[key])) > 0.99999f,
                    "Euler unroll changed an authored key or a missing axis default.");
            }
            const auto initial = found->rotations.front().value;
            for (const auto& key : found->rotations)
            {
                const float angle = 2.0f * std::acos(std::clamp(std::abs(initial.Dot(key.value)), 0.0f, 1.0f));
                require(angle < 0.05f, "Euler wrap caused a spurious full turn.");
            }
            ++cases;
        }
        std::cout << "PASS " << cases << " Euler-wrap FBX fixtures (all six rotation orders, all axes, dense/sparse curves).\n";
    }

    Vector3 skinVertex(const ModelVertex& vertex, const SkinningPaletteSnapshot& pose)
    {
        const std::array weights{ vertex.boneWeights.x, vertex.boneWeights.y, vertex.boneWeights.z, vertex.boneWeights.w };
        const float total = weights[0] + weights[1] + weights[2] + weights[3];
        if (total <= 0.00001f)
            return vertex.position;
        Vector3 result = Vector3::Zero;
        for (std::size_t influence = 0; influence < 4; ++influence)
            result += Vector3::Transform(vertex.position, pose.constants.boneMatrices[vertex.boneIndices[influence]])
                * (weights[influence] / total);
        return result;
    }

    void testIdleModel(const std::filesystem::path& path)
    {
        const auto model = FbxModelImporter{}.importModel(path);
        require(model != nullptr && model->skeleton && !model->animations.empty(), "Idle model import failed.");
        auto skeleton = std::make_shared<SkeletonAsset>();
        AnimationAssetBuilder builder;
        require(builder.buildSkeleton(*model->skeleton, path, *skeleton), "Idle skeleton build failed.");
        float maximumStep = 0.0f;
        float maximumReconstructionError = 0.0f;
        for (const auto& resource : model->animations)
        {
            for (const auto& channel : resource.channels)
                for (std::size_t key = 1; key < channel.rotations.size(); ++key)
                {
                    const float dot = std::abs(channel.rotations[key - 1].value.Dot(channel.rotations[key].value));
                    require(2.0f * std::acos(std::clamp(dot, 0.0f, 1.0f)) < 0.1f,
                        "Idle model contains a large frame-to-frame source rotation.");
                }
            auto clip = std::make_shared<AnimationClipAsset>();
            require(builder.buildClip(resource, *skeleton, path, *clip), "Idle clip build failed.");
            for (const std::string_view root : { "", "root", "pelvis" })
            {
                if (!root.empty() && !skeleton->jointLookup.contains(std::string(root)))
                    continue;
                AnimatorInstance plain;
                AnimatorInstance moving;
                require(plain.setAssets(skeleton, clip) && moving.setAssets(skeleton, clip)
                    && moving.configureRootMotion(true, root) && moving.update(0), "Idle root setup failed.");
                comparePalettes(*plain.getSnapshot(), *moving.getSnapshot(), Matrix::Identity, 0.00002f);
                Matrix world = Matrix::Identity;
                std::vector<SkinningBounds> bounds(model->meshes.size());
                for (std::size_t mesh = 0; mesh < bounds.size(); ++mesh)
                    require(bounds[mesh].initialize(model->meshes[mesh].vertices), "Idle bounds setup failed.");
                const int frames = static_cast<int>(std::floor(clip->duration * 120.0f));
                auto previous = plain.getSnapshot();
                for (int frame = 0; frame < frames; ++frame)
                {
                    require(plain.update(1.0f / 120.0f) && moving.update(1.0f / 120.0f), "Idle evaluation failed.");
                    const auto raw = plain.getSnapshot();
                    const auto extracted = moving.getSnapshot();
                    world = moving.getRootMotionDelta() * world;
                    comparePalettes(*raw, *extracted, world, 0.0001f);
                    for (std::size_t mesh = 0; mesh < model->meshes.size(); ++mesh)
                    {
                        AABB box;
                        require(bounds[mesh].calculate(*extracted, world, box), "Root-motion bounds invalid.");
                        for (const auto& vertex : model->meshes[mesh].vertices)
                        {
                            const auto point = skinVertex(vertex, *raw);
                            maximumStep = std::max(maximumStep, Vector3::Distance(point, skinVertex(vertex, *previous)));
                            maximumReconstructionError = std::max(maximumReconstructionError,
                                Vector3::Distance(point, Vector3::Transform(skinVertex(vertex, *extracted), world)));
                        }
                    }
                    previous = raw;
                }
            }
        }
        require(maximumStep < 0.05f, "Idle mesh jumps more than 5 cm in one 120-Hz step.");
        require(maximumReconstructionError < 0.0002f, "Root motion changed a skinned vertex by more than 0.2 mm.");
        std::cout << "PASS idle mesh: max 120-Hz vertex step=" << maximumStep
            << " m, max root-motion reconstruction error=" << maximumReconstructionError << " m.\n";
    }
}

int main(const int argc, char** argv)
{
    Engine::LoggerConfig config;
    config.useFile = false;
    config.useDebugOutput = false;
    try
    {
        require(Engine::Logger::instance().initialize(config), "Logger initialization failed.");
        testMotion();
        testReconfigurationAndController();
        testEulerImport();
        if (argc == 3 && std::string_view(argv[1]) == "--idle-model")
            testIdleModel(argv[2]);
        else
            require(argc == 1, "Usage: RootMotionTests [--idle-model path.fbx]");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
