#pragma once

#include "Core\Math\MathTypes.h"
#include "Core\Object\ObjectGUID.h"
#include <filesystem>
#include <vector>

namespace Engine
{
	/** @brief Particle color blending mode. */
	enum class ParticleBlendMode : std::uint8_t { Alpha, Additive, PremultipliedAlpha, Multiply, Screen };

	/** @brief Returns whether a particle blend mode is supported. */
	constexpr bool isParticleBlendModeValid(const ParticleBlendMode mode) noexcept
	{
		return mode >= ParticleBlendMode::Alpha && mode <= ParticleBlendMode::Screen;
	}

	/** @brief Local-space volume from which particle positions are sampled. */
	enum class ParticleEmissionShape : std::uint8_t { Sphere, Box, Disk };

	/** @brief World-space orientation of a particle billboard. */
	enum class ParticleOrientation : std::uint8_t { Camera, Velocity, Axis, Horizontal, Direction, Stretched };

	/** @brief Flipbook frame scheduling strategy. */
	enum class ParticleFlipbookPlayback : std::uint8_t { Lifetime, FramesPerSecond, Custom };

	/** @brief Particle-state simulation backend. */
	enum class ParticleSimulationBackend : std::uint8_t { CPU, GPU };

	/** @brief An additional burst at a time in the emitter's cycle, in seconds. */
	struct ParticleBurst
	{
		float time = 0.0f;
		std::uint32_t count = 0;
		bool randomCount = false; //!< Sample count from the inclusive range instead of using count.
		std::uint32_t countMin = 0; //!< Minimum randomized count.
		std::uint32_t countMax = 0; //!< Maximum randomized count.
		float probability = 1.0f; //!< Probability applied independently to each occurrence.
		std::uint32_t repeatCount = 1; //!< Total occurrences in each emitter cycle.
		float repeatInterval = 0.0f; //!< Time between occurrences in seconds.
	};

	/** @brief A scalar value at a normalized particle lifetime. */
	struct ParticleScalarKey
	{
		float time = 0.0f;
		float value = 0.0f;
	};

	/** @brief A linear RGBA value at a normalized particle lifetime. */
	struct ParticleColorKey
	{
		float time = 0.0f;
		Vector4 value = Vector4::One;
	};

	/** @brief A vector value at a normalized particle lifetime. */
	struct ParticleVectorKey
	{
		float time = 0.0f;
		Vector3 value = Vector3::Zero;
	};

	/** @brief One frame and its playback duration in a custom flipbook sequence. */
	struct ParticleFlipbookFrame
	{
		std::uint32_t frame = 0;
		float duration = 0.1f;
	};

	/**
	 * @brief Optional lifetime curves with linear or smoothstep interpolation.
	 * @details Keys must have strictly increasing normalized times in [0, 1].
	 * Empty color/size curves use start/end interpolation; an empty speed curve uses 1.
	 * Speed values multiply displacement without changing the simulated velocity.
	 */
	struct ParticleLifetimeCurves
	{
		std::vector<ParticleColorKey> color;
		std::vector<ParticleScalarKey> size;
		std::vector<ParticleScalarKey> speed;
		std::vector<ParticleScalarKey> angularSpeed; //!< Angular-speed multiplier; empty means 1.
		std::vector<ParticleScalarKey> width; //!< Width multiplier; empty means 1.
		std::vector<ParticleScalarKey> height; //!< Height multiplier; empty means 1.
		bool smooth = false; //!< Use smoothstep between keys instead of linear interpolation.
		std::vector<ParticleVectorKey> acceleration; //!< Additional acceleration in simulation space.
		std::vector<ParticleVectorKey> velocity; //!< Additional velocity in simulation space.
		std::vector<ParticleScalarKey> gravity; //!< Gravity multiplier; empty means 1.
		std::vector<ParticleScalarKey> drag; //!< Drag multiplier; empty means 1.
	};

	/** @brief Authoring settings for a particle emitter. */
	struct ParticleEffectSettings
	{
		bool playOnAwake = false;
		bool looping = true;
		bool localSpace = true;
		std::uint32_t maxParticles = 256;
		float emissionRate = 10.0f;
		std::uint32_t burstCount = 0;
		float duration = 1.0f;
		float lifetimeMin = 0.5f;
		float lifetimeMax = 1.5f;
		float shapeRadius = 0.0f;
		Vector3 direction = Vector3::Up;
		float spreadAngleDegrees = 20.0f;
		float speedMin = 0.5f;
		float speedMax = 1.5f;
		Vector3 gravity = Vector3(0.0f, -9.81f, 0.0f);
		float drag = 0.0f;
		float startSize = 0.2f;
		float endSize = 0.0f;
		Vector4 startColor = Vector4::One;
		Vector4 endColor = Vector4(1.0f, 1.0f, 1.0f, 0.0f);
		std::uint32_t seed = 1;
		std::filesystem::path texturePath;
		ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
		std::uint32_t flipbookColumns = 1;
		std::uint32_t flipbookRows = 1;
		ParticleFlipbookPlayback flipbookPlayback = ParticleFlipbookPlayback::Lifetime;
		float flipbookFramesPerSecond = 12.0f;
		std::uint32_t flipbookLoopCount = 1; //!< Zero repeats until particle death.
		bool flipbookRandomStartFrame = false;
		bool flipbookFrameBlending = false;
		std::vector<ParticleFlipbookFrame> flipbookFrames; //!< Custom sequence; frame durations are in seconds.
		ParticleEmissionShape shape = ParticleEmissionShape::Sphere; //!< Spawn volume in emitter-local space.
		Vector3 boxExtents = Vector3::One; //!< Box half-extents in emitter-local units.
		std::vector<ParticleBurst> bursts; //!< Additional bursts, repeated each positive-duration loop.
		ParticleLifetimeCurves curves; //!< Curves copied into each particle's immutable birth snapshot.
		bool trailEnabled = false; //!< Emit a world-space ribbon for each newly born particle.
		float trailLifetime = 0.5f; //!< Lifetime of each trail sample in seconds.
		float trailWidth = 0.1f; //!< Ribbon width in world units.
		float trailMinDistance = 0.05f; //!< Minimum world-space distance between retained samples.
		bool randomInitial = false; //!< Sample initial size and RGBA from the configured ranges.
		float initialSizeMin = 0.2f;
		float initialSizeMax = 0.2f;
		Vector4 initialColorMin = Vector4::One;
		Vector4 initialColorMax = Vector4::One;
		float initialRotationMin = 0.0f; //!< Initial rotation in radians.
		float initialRotationMax = 6.283185307179586f;
		float angularVelocityMin = 0.0f; //!< Initial angular velocity in radians per second.
		float angularVelocityMax = 0.0f;
		float simulationSpeed = 1.0f; //!< Simulation time multiplier.
		bool clearOnStop = false; //!< Clear particles and trails when emission is stopped.
		Vector2 initialSizeScale = Vector2::One; //!< Per-axis scale applied to the particle size.
		float startDelay = 0.0f; //!< Delay before the effect begins, in simulation seconds.
		bool prewarm = false; //!< Simulate one loop before the delayed effect becomes visible.
		std::vector<ParticleLifetimeCurves> curveVariants; //!< Additional equally likely lifetime curve sets.
		float distancePerParticle = 0.0f; //!< World distance between emissions; zero disables distance emission.
		float inheritVelocity = 0.0f; //!< Multiplier for emitter world velocity inherited at birth.
		ParticleOrientation orientation = ParticleOrientation::Camera; //!< Billboard orientation.
		Vector3 orientationDirection = Vector3::Up; //!< World-space axis or plane normal.
		float stretchBySpeed = 0.0f; //!< Additional billboard height per world-unit/second.
		float softDistance = 0.0f; //!< Intersection fade distance in world units; zero disables soft particles.
		Vector2 uvScroll = Vector2::Zero; //!< Flipbook-cell cycles per simulation second.
		std::filesystem::path secondaryTexturePath; //!< Additional color texture.
		std::filesystem::path maskTexturePath; //!< Linear alpha mask.
		std::filesystem::path distortionTexturePath; //!< Linear RG UV distortion map.
		float uvDistortion = 0.0f; //!< UV distortion amplitude in texture repetitions.
		float refractionPixels = 0.0f; //!< Scene background displacement in pixels; zero disables refraction.
		std::filesystem::path meshPath; //!< Static model geometry; empty selects billboard rendering.
		std::vector<ParticleScalarKey> emissionRateKeys; //!< Absolute particles/second over normalized emitter-cycle time.
		ParticleSimulationBackend simulationBackend = ParticleSimulationBackend::CPU;
	};

	/** @brief Immutable particle effect configuration and persistent identity. */
	struct ParticleEffectAsset
	{
		ObjectGUID guid;
		ParticleEffectSettings settings;
	};

	/** @brief Generation-checked reference to a registered particle effect asset. */
	struct ParticleEffectAssetHandle
	{
		std::uint32_t index = UINT32_MAX;
		std::uint32_t generation = 0;
		/** @brief Tests whether the handle is syntactically valid. */
		bool isValid() const noexcept { return index != UINT32_MAX && generation != 0; }
		bool operator==(const ParticleEffectAssetHandle&) const = default;
	};
}
