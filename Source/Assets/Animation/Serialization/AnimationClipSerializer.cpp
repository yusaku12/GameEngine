#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\AnimationClip_generated.h"
#include "Assets\Animation\Serialization\AnimationClipSerializer.h"
#include "Assets\Animation\Serialization\OzzArchiveUtils.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        AssetGuid toFlat(const AssetGUID& value) { return { value.high, value.low }; }
    }

    bool AnimationClipSerializer::save(const std::filesystem::path& path, const AnimationClipAsset& clip) const
    {
        if (!clip.guid.isValid() || !clip.skeletonGuid.isValid() || !clip.skeletonSignature.isValid()
            || clip.duration <= 0.0f || clip.animation.num_tracks() <= 0)
            return false;
        std::vector<std::uint8_t> archive;
        if (!saveOzzArchive(clip.animation, archive))
            return false;

        flatbuffers::FlatBufferBuilder builder(archive.size() + 512);
        std::vector<flatbuffers::Offset<Engine::Serialization::AnimationEvent>> events;
        events.reserve(clip.events.size());
        for (const Engine::AnimationEvent& event : clip.events)
            events.push_back(CreateAnimationEvent(builder, event.normalizedTime, event.eventId));
        const AssetGuid guid = toFlat(clip.guid);
        const AssetGuid skeletonGuid = toFlat(clip.skeletonGuid);
        const auto data = CreateAnimationClipAssetData(builder, &guid, builder.CreateString(clip.name), &skeletonGuid,
            clip.skeletonSignature.high, clip.skeletonSignature.low, clip.duration,
            static_cast<Engine::Serialization::AnimationWrapMode>(clip.wrapMode), clip.additive, clip.applyRootMotion,
            builder.CreateVector(events), builder.CreateVector(archive), calculateArchiveChecksum(archive),
            builder.CreateString(clip.sourcePath.generic_string()), builder.CreateString(clip.sourceClipName));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_ANIMATION_CLIP_VERSION, 0);
        FinishAnimationClipFileBuffer(builder, CreateAnimationClipFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool AnimationClipSerializer::load(const std::filesystem::path& path, AnimationClipAsset& clip) const
    {
        FlatBufferReader reader;
        if (!reader.open(path) || !reader.hasIdentifier("ANIM"))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyAnimationClipFileBuffer(verifier))
            return false;
        const AnimationClipFile* file = GetAnimationClipFile(reader.data());
        const AnimationClipAssetData* source = file ? file->clip() : nullptr;
        if (file == nullptr || file->header() == nullptr || source == nullptr || source->guid() == nullptr
            || source->skeleton_guid() == nullptr || source->name() == nullptr || source->ozz_archive() == nullptr
            || source->wrap_mode() > AnimationWrapMode_PingPong
            || file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() != CURRENT_ANIMATION_CLIP_VERSION)
            return false;

        const auto* blob = source->ozz_archive();
        const std::span archive(blob->data(), blob->size());
        if (calculateArchiveChecksum(archive) != source->archive_checksum())
            return false;

        AnimationClipAsset loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        loaded.skeletonGuid = { source->skeleton_guid()->high(), source->skeleton_guid()->low() };
        loaded.skeletonSignature = { source->signature_high(), source->signature_low() };
        loaded.duration = source->duration();
        loaded.wrapMode = static_cast<Engine::AnimationWrapMode>(source->wrap_mode());
        loaded.additive = source->additive();
        loaded.applyRootMotion = source->apply_root_motion();
        loaded.sourcePath = source->source_path() ? source->source_path()->str() : "";
        loaded.sourceClipName = source->source_clip_name() ? source->source_clip_name()->str() : "";
        if (!loaded.guid.isValid() || !loaded.skeletonGuid.isValid() || !loaded.skeletonSignature.isValid()
            || !loadOzzArchive(archive, loaded.animation) || loaded.duration != loaded.animation.duration())
            return false;
        if (const auto* events = source->events())
            for (const Engine::Serialization::AnimationEvent* event : *events)
            {
                if (event == nullptr || event->normalized_time() < 0.0f || event->normalized_time() > 1.0f)
                    return false;
                loaded.events.push_back({ event->normalized_time(), event->event_id() });
            }
        clip = std::move(loaded);
        return true;
    }
}