#pragma once

#include "Core\GameObject\Component.h"
#include "Graphics\Effects\ParticleRenderSubmission.h"

namespace Engine
{
	/** @brief Procedural geometry mode for an independent particle ribbon effect. */
	enum class ParticleRibbonMode : std::uint8_t { Ribbon, Beam, Lightning };

	/** @brief A ribbon control point with world/local position, linear RGBA, and world-unit width. */
	struct ParticleRibbonPoint
	{
		Vector3 position = Vector3::Zero;
		Vector4 color = Vector4::One;
		float width = 0.1f;
	};

	/** @brief Persistent authoring and procedural settings for ParticleRibbonComponent. */
	struct ParticleRibbonSettings
	{
		ParticleRibbonMode mode = ParticleRibbonMode::Ribbon;
		bool localSpace = true;
		std::vector<ParticleRibbonPoint> controlPoints{
			{ Vector3(-0.5f, 0.0f, 0.0f), Vector4::One, 0.1f },
			{ Vector3(0.5f, 0.0f, 0.0f), Vector4::One, 0.1f }
		};
		std::uint32_t lightningSegments = 12;
		float lightningAmplitude = 0.1f;
		float updateInterval = 0.05f;
		std::uint32_t seed = 1;
		ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
	};

	/**
	 * @brief Renders an independent ribbon, beam, or deterministic procedural lightning effect.
	 * @details Authored points use local or world space. Runtime API points are always world-space.
	 * @thread_safety Main thread only. Submitted geometry is copied to the render queue.
	 */
	class ParticleRibbonComponent final : public Component
	{
	public:
		ParticleRibbonComponent() = default;

		/** @brief Returns the persistent authoring configuration. */
		const ParticleRibbonSettings& settings() const noexcept { return m_settings; }
		/** @brief Validates and transactionally replaces authoring configuration. */
		bool setSettings(const ParticleRibbonSettings& settings);
		/** @brief Replaces the runtime world-space point list without changing saved authoring data. */
		bool setWorldPoints(std::span<const ParticleRibbonPoint> points);
		/** @brief Replaces runtime points with two world-space endpoints for Beam or Lightning modes. */
		bool setWorldEndpoints(const Vector3& start, const Vector3& end);
		/** @brief Clears runtime points and restores the authored points. */
		void clearRuntimePoints() noexcept;
		/** @brief Submits the current independent geometry without advancing its animation. */
		bool submitRenderData();
		/** @brief Returns the versioned payload format. */
		std::uint32_t getPayloadVersion() const noexcept override { return 1; }
		/** @brief Serializes authoring settings and excludes runtime points. */
		bool serializePayload(std::vector<std::uint8_t>& payload) const override;
		/** @brief Restores and validates authoring settings from a versioned payload. */
		bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
		/** @brief Copies authoring configuration without copying transient runtime points. */
		bool copyConfigurationTo(Component& destination) const override;

	protected:
		void onStart() override;
		void onLateUpdate(float deltaTime) override;
		void onImGui() override;

	private:
		static constexpr std::size_t MAX_POINTS = 256;
		static constexpr std::uint32_t MAX_LIGHTNING_SEGMENTS = 128;

		bool validateSettings(const ParticleRibbonSettings& settings) const;
		void regenerateLightning();
		void resolvePoints(std::vector<ParticleRibbonPoint>& points, bool& worldSpace) const;

		ParticleRibbonSettings m_settings;
		std::vector<ParticleRibbonPoint> m_runtimePoints;
		std::vector<ParticleRibbonPoint> m_generatedPoints;
		std::vector<ParticleRenderRibbonPoint> m_renderPoints;
		double m_lightningElapsed = 0.0;
		double m_lightningRemainder = 0.0;
		std::uint64_t m_lightningPulse = 0;
		bool m_hasRuntimePoints = false;
		bool m_generatedWorldSpace = false;
		bool m_lightningGenerationFailed = false;
	};
}
