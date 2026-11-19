#pragma once

#include <memory>
#include <string>
#include <vector>
#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
	class AnimatorInstance;
	struct SkeletonAsset;

	/** @brief Persisted LookAt chain, world-space target, and end-joint local axes. */
	struct LookAtSettings
	{
		std::string endJoint;
		std::string rootJoint;
		Vector3 target = Vector3::Zero;
		Vector3 forward = Vector3::UnitZ;
		Vector3 up = Vector3::UnitY;
		Vector3 offset = Vector3::Zero;
		float weight = 1.0f;
		float jointWeight = 0.5f;
	};

	/**
	 * @brief Aim an animated joint chain at a world-space point using ozz IKAimJob.
	 * @details Affects only the rendered pose, after Root Motion and Foot IK.
	 * @thread_safety Game update thread only; not thread-safe.
	 */
	class LookAtComponent final : public Component
	{
	public:
		LookAtComponent() noexcept;
		GE_DISABLE_COPY_AND_MOVE(LookAtComponent);

		/** @brief Replace valid settings atomically; malformed settings are rejected. */
		bool setSettings(const LookAtSettings& settings);
		/** @brief Read the current settings. */
		const LookAtSettings& getSettings() const noexcept { return m_settings; }
		/** @brief Update the world-space target without rebuilding the joint chain. */
		bool setTarget(const Vector3& target);
		/** @brief Set the overall influence in [0,1]. */
		bool setWeight(float weight);

		std::uint32_t getPayloadVersion() const noexcept override;
		bool serializePayload(std::vector<std::uint8_t>& payload) const override;
		bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

	protected:
		void onImGui() override;
		void onDisable() override;

	private:
		friend class AnimatorComponent;
		bool evaluate(AnimatorInstance& animator, bool hasFootIK);
		bool resolveRig(const SkeletonAsset& skeleton);
		bool fail(const char* reason);
		void resetRuntime() noexcept;

		LookAtSettings m_settings;
		std::shared_ptr<const SkeletonAsset> m_resolvedSkeleton;
		std::vector<int> m_chain;
		bool m_errorLogged = false;
		std::string m_status;
	};
}
