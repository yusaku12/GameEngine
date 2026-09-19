#include <cmath>
#include <iostream>
#include <vector>

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

bool runAnimationAssetRoundTripTests();

int main()
{
    ozz::animation::offline::RawSkeleton rawSkeleton;
    rawSkeleton.roots.resize(1);
    rawSkeleton.roots[0].name = "root";
    if (!check(rawSkeleton.Validate(), "Raw skeleton validation failed.")) return 1;
    ozz::animation::offline::SkeletonBuilder skeletonBuilder;
    auto skeleton = skeletonBuilder(rawSkeleton);
    if (!check(skeleton != nullptr, "Skeleton build failed.")) return 1;

    ozz::animation::offline::RawAnimation rawAnimation;
    rawAnimation.duration = 1.0f;
    rawAnimation.tracks.resize(1);
    rawAnimation.tracks[0].translations.push_back({ 0.0f, { 0.0f, 0.0f, 0.0f } });
    rawAnimation.tracks[0].translations.push_back({ 1.0f, { 2.0f, 0.0f, 0.0f } });
    ozz::animation::offline::AnimationBuilder animationBuilder;
    auto animation = animationBuilder(rawAnimation);
    if (!check(animation != nullptr, "Animation build failed.")) return 1;

    ozz::io::MemoryStream stream;
    {
        ozz::io::OArchive archive(&stream, ozz::GetNativeEndianness());
        archive << *skeleton;
        archive << *animation;
    }
    if (!check(stream.Seek(0, ozz::io::Stream::kSet) == 0, "Archive rewind failed.")) return 1;

    ozz::animation::Skeleton loadedSkeleton;
    ozz::animation::Animation loadedAnimation;
    {
        ozz::io::IArchive archive(&stream);
        if (!check(archive.TestTag<ozz::animation::Skeleton>(), "Skeleton archive tag mismatch.")) return 1;
        archive >> loadedSkeleton;
        if (!check(archive.TestTag<ozz::animation::Animation>(), "Animation archive tag mismatch.")) return 1;
        archive >> loadedAnimation;
    }

    ozz::animation::SamplingJob::Context context(loadedSkeleton.num_joints());
    std::vector<ozz::math::SoaTransform> locals(static_cast<std::size_t>(loadedSkeleton.num_soa_joints()));
    std::vector<ozz::math::Float4x4> models(static_cast<std::size_t>(loadedSkeleton.num_joints()));
    ozz::animation::SamplingJob sampling{ .ratio = 0.5f, .animation = &loadedAnimation,
        .context = &context, .output = ozz::make_span(locals) };
    if (!check(sampling.Run(), "SamplingJob failed.")) return 1;
    ozz::animation::LocalToModelJob localToModel;
    localToModel.skeleton = &loadedSkeleton;
    localToModel.input = ozz::make_span(locals);
    localToModel.output = ozz::make_span(models);
    if (!check(localToModel.Run(), "LocalToModelJob failed.")) return 1;
    float translation[4]{};
    ozz::math::StorePtrU(models[0].cols[3], translation);
    if (!check(std::abs(translation[0] - 1.0f) < 0.001f, "Unexpected sampled translation.")) return 1;
    return runAnimationAssetRoundTripTests() ? 0 : 1;
}
