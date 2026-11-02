#pragma once

#include "Core\GameObject\Component.h"
#include "Assets\PostProcess\PostProcessProfile.h"
#include <memory>

namespace Engine
{
    /** @brief Spatial influence shape, independent of physics colliders. */
    enum class PostProcessVolumeShape : std::uint8_t { Global, Box, Sphere };
    /** @brief Authoring geometry in local space; Blend Distance is an outer world-space shell. */
    struct PostProcessVolumeSettings
    {
        PostProcessVolumeShape shape = PostProcessVolumeShape::Global;
        float priority = 0; //!< Lower priorities blend first.
        float weight = 1; //!< Volume contribution, 0-1.
        float blendDistance = 0; //!< World units outside the shape; zero is a hard boundary.
        Vector3 center = Vector3::Zero; //!< Local center.
        Vector3 boxSize = Vector3::One; //!< Positive local full dimensions.
        float sphereRadius = 0.5f; //!< Positive local radius; world scale uses the largest absolute axis.
        bool operator==(const PostProcessVolumeSettings&) const = default;
    };
    /** @brief Validates all authored values, even values unused by the selected shape. */
    bool validatePostProcessVolumeSettings(const PostProcessVolumeSettings& settings) noexcept;
    /**
     * @brief Profile-driven Global/Box/Sphere influence in active and persistent scenes.
     * @details One per GameObject. GameObject Layer and active hierarchy control participation.
     * @thread_safety Main thread only.
     */
    class PostProcessVolumeComponent final : public Component
    {
    public:
        /** @brief Gets immutable authoring geometry. */
        const PostProcessVolumeSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces geometry; failure retains the previous state. */
        bool setSettings(const PostProcessVolumeSettings& settings) noexcept;
        /** @brief Gets the referenced shared Profile handle. */
        PostProcessProfileHandle getPostProcessProfile() const noexcept { return m_profile; }
        /** @brief Assigns an existing Profile, or clears it with an empty handle. */
        bool setPostProcessProfile(PostProcessProfileHandle profile);
        /** @brief Computes the weighted spatial contribution; invalid transforms are diagnosed once until recovery. */
        float influence(const Vector3& cameraPosition) const noexcept;
        /** @brief Gets the immutable Profile snapshot, diagnosing stale handles once. */
        std::shared_ptr<const PostProcessProfile> profileSnapshot() const;
        /** @brief Versioned Volume payload. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves geometry and a saved Profile reference, never shared asset edits. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Verifies and restores a payload atomically. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Clones authoring configuration including unsaved immutable Profiles. */
        bool copyConfigurationTo(Component& destination) const override;
    protected:
        void onImGui() override;
    private:
        PostProcessVolumeSettings m_settings;
        PostProcessProfileHandle m_profile;
        mutable bool m_invalidTransformReported = false;
        mutable bool m_missingProfileReported = false;
    };
}
