#pragma once

#include <array>
#include <memory>
#include <string>
#include "Animation\Runtime\FootIK.h"
#include "Core\GameObject\Component.h"
#include "Core\CoreDefines.h"
#include "Physics\PhysicsLayer.h"

namespace Engine
{
    class AnimatorInstance;
    struct SkeletonAsset;

    /** @brief Persisted joint names and joint-local axes for one foot. Distances are world units. */
    struct FootIKLegSettings
    {
        std::string hip;
        std::string knee;
        std::string ankle;
        Vector3 kneeAxis = -Vector3::UnitX;
        Vector3 poleAxis = Vector3::UnitZ;
        Vector3 footUp = Vector3::UnitY;
        Vector3 footForward = Vector3::UnitZ;
        float weight = 1.0f;
        float footHeight = 0.08f;
    };

    /** @brief Foot contact tuning. A zero responseSpeed disables temporal smoothing. */
    struct FootIKSettings
    {
        std::array<FootIKLegSettings, 2> legs;
        float weight = 1.0f;
        float soften = 1.0f;
        float rayHeight = 0.5f;
        float rayDepth = 0.75f;
        float maxAdjustment = 0.5f;
        float maxSlope = 60.0f;
        bool pelvisCorrection = true;
        float maxPelvisOffset = 0.4f;
        bool alignFeet = true;
        float responseSpeed = 15.0f;
        PhysicsLayerMask layerMask = ALL_PHYSICS_LAYERS;
        bool includeDynamic = true;
    };

    /**
     * @brief Ground both feet with PhysX rays and ozz IK jobs after animation/root-motion update.
     * @details Only the rendered pose changes; GameObject, colliders and root-motion deltas do not.
     * @thread_safety Game update thread only, after animation workers join. Not thread-safe.
     */
    class FootIKComponent final : public Component
    {
    public:
        FootIKComponent() noexcept;
        GE_DISABLE_COPY_AND_MOVE(FootIKComponent);

        /** @brief Validate and replace configuration atomically; reject malformed values with a diagnostic. */
        bool setSettings(const FootIKSettings& settings);
        /** @brief Read persisted rig and contact settings. */
        const FootIKSettings& getSettings() const noexcept { return m_settings; }

        /** @brief Change overall influence without resetting rig/contact smoothing; finite [0,1] only. */
        bool setWeight(float weight);
        /** @brief Change a foot's live contact influence (0=left, 1=right), preserving smoothing. */
        bool setLegWeight(std::size_t leg, float weight);

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:
        void onImGui() override;
        void onDisable() override;

    private:
        friend class AnimatorComponent;
        bool evaluate(AnimatorInstance& animator, float deltaTime);
        bool resolveRig(const SkeletonAsset& skeleton);
        void resetRuntime() noexcept;
        bool fail(const char* reason);

        struct Contact
        {
            float offset = 0.0f;
            float weight = 0.0f;
            Vector3 normal = Vector3::UnitY;
        };

        FootIKSettings m_settings;
        std::array<FootIKTarget, 2> m_rig;
        std::array<Contact, 2> m_contacts;
        std::shared_ptr<const SkeletonAsset> m_resolvedSkeleton;
        bool m_rigValid = false;
        bool m_errorLogged = false;
        float m_pelvisOffset = 0.0f;
        std::string m_status;
    };
}
