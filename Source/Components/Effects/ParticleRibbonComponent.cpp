#include "Pch.h"
#include "Components\Effects\ParticleRibbonComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Logging\Logger.h"
#include "Generated\FlatBuffers\ParticleRibbonComponent_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <imgui.h>

namespace Engine
{
	namespace
	{
		constexpr float RIBBON_PI = 3.14159265358979323846f;

		const auto particleRibbonType =
			ComponentRegistry::instance().registerType<ParticleRibbonComponent>(
				"Particle Ribbon", false, false, true);

		bool isFinite(const Vector3& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool isFinite(const Vector4& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z) && std::isfinite(value.w);
		}

		bool validPoint(const ParticleRibbonPoint& point) noexcept
		{
			return isFinite(point.position) && std::abs(point.position.x) <= 1000000.0f
				&& std::abs(point.position.y) <= 1000000.0f && std::abs(point.position.z) <= 1000000.0f
				&& isFinite(point.color) && point.color.x >= 0.0f && point.color.x <= 100.0f
				&& point.color.y >= 0.0f && point.color.y <= 100.0f
				&& point.color.z >= 0.0f && point.color.z <= 100.0f
				&& point.color.w >= 0.0f && point.color.w <= 1.0f
				&& std::isfinite(point.width) && point.width > 0.0f && point.width <= 10000.0f;
		}

		std::uint32_t nextRandom(std::uint32_t& state) noexcept
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return state;
		}
	}

	bool ParticleRibbonComponent::validateSettings(const ParticleRibbonSettings& settings) const
	{
		if (settings.mode > ParticleRibbonMode::Lightning || settings.controlPoints.size() < 2
			|| settings.controlPoints.size() > MAX_POINTS
			|| (settings.mode != ParticleRibbonMode::Ribbon && settings.controlPoints.size() != 2)
			|| settings.lightningSegments < 2 || settings.lightningSegments > MAX_LIGHTNING_SEGMENTS
			|| !std::isfinite(settings.lightningAmplitude) || settings.lightningAmplitude < 0.0f
			|| settings.lightningAmplitude > 10000.0f
			|| !std::isfinite(settings.updateInterval) || settings.updateInterval < 0.01f
			|| settings.updateInterval > 10.0f || settings.seed == 0
			|| !isParticleBlendModeValid(settings.blendMode))
		{
			LOG_ERROR("[ParticleRibbon] Invalid mode, point count, or lightning settings.");
			return false;
		}
		for (const ParticleRibbonPoint& point : settings.controlPoints)
			if (!validPoint(point))
			{
				LOG_ERROR("[ParticleRibbon] Control points contain invalid position, color, or width.");
				return false;
			}
		return true;
	}

	bool ParticleRibbonComponent::setSettings(const ParticleRibbonSettings& settings)
	{
		if (!validateSettings(settings))
			return false;
		m_settings = settings;
		if (m_settings.mode == ParticleRibbonMode::Lightning)
			regenerateLightning();
		else
		{
			m_generatedPoints.clear();
			m_lightningGenerationFailed = false;
		}
		return true;
	}

	bool ParticleRibbonComponent::setWorldPoints(const std::span<const ParticleRibbonPoint> points)
	{
		if (points.size() < 2 || points.size() > MAX_POINTS
			|| (m_settings.mode != ParticleRibbonMode::Ribbon && points.size() != 2)
			|| !std::all_of(points.begin(), points.end(), validPoint))
		{
			LOG_ERROR("[ParticleRibbon] Runtime point list must contain 2 to {} valid world-space points.", MAX_POINTS);
			return false;
		}
		try
		{
			m_runtimePoints.assign(points.begin(), points.end());
		}
		catch (const std::bad_alloc&)
		{
			LOG_ERROR("[ParticleRibbon] Failed to allocate runtime point storage.");
			return false;
		}
		catch (const std::length_error&)
		{
			LOG_ERROR("[ParticleRibbon] Runtime point list exceeds vector capacity.");
			return false;
		}
		m_hasRuntimePoints = true;
		if (m_settings.mode == ParticleRibbonMode::Lightning)
		{
			regenerateLightning();
			return !m_lightningGenerationFailed;
		}
		return true;
	}

	bool ParticleRibbonComponent::setWorldEndpoints(const Vector3& start, const Vector3& end)
	{
		if (m_settings.mode == ParticleRibbonMode::Ribbon)
		{
			LOG_ERROR("[ParticleRibbon] Runtime endpoints are supported only in Beam or Lightning mode.");
			return false;
		}
		const auto& authored = m_settings.controlPoints;
		const ParticleRibbonPoint first{ start, authored.front().color, authored.front().width };
		const ParticleRibbonPoint last{ end, authored.back().color, authored.back().width };
		const std::array endpoints{ first, last };
		return setWorldPoints(endpoints);
	}

	void ParticleRibbonComponent::clearRuntimePoints() noexcept
	{
		m_runtimePoints.clear();
		m_hasRuntimePoints = false;
		if (m_settings.mode == ParticleRibbonMode::Lightning)
			regenerateLightning();
	}

	void ParticleRibbonComponent::onStart()
	{
		if (m_settings.mode == ParticleRibbonMode::Lightning)
			regenerateLightning();
	}

	void ParticleRibbonComponent::onLateUpdate(const float deltaTime)
	{
		if (m_settings.mode != ParticleRibbonMode::Lightning)
			return;
		if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
		{
			LOG_ERROR("[ParticleRibbon] Lightning delta time must be finite and non-negative.");
			return;
		}
		if (deltaTime == 0.0f)
			return;
		m_lightningRemainder += deltaTime;
		if (m_lightningRemainder < m_settings.updateInterval)
			return;
		const double pulses = std::floor(m_lightningRemainder / m_settings.updateInterval);
		m_lightningRemainder = std::fmod(m_lightningRemainder, m_settings.updateInterval);
		constexpr double PULSE_MODULUS = 18446744073709551616.0;
		m_lightningPulse += static_cast<std::uint64_t>(std::fmod(pulses, PULSE_MODULUS));
		regenerateLightning();
	}

	void ParticleRibbonComponent::regenerateLightning()
	{
		m_lightningGenerationFailed = false;
		const auto& source = m_hasRuntimePoints ? m_runtimePoints : m_settings.controlPoints;
		if (source.size() < 2)
		{
			m_generatedPoints.clear();
			m_lightningGenerationFailed = true;
			return;
		}
		const std::uint32_t segments = m_settings.lightningSegments;
		std::uint32_t state = m_settings.seed
			^ static_cast<std::uint32_t>(m_lightningPulse * 0x9e3779b9u);
		if (state == 0)
			state = 0x6d2b79f5u;
		const Vector3 start = source.front().position;
		const Vector3 end = source.back().position;
		Vector3 axis = end - start;
		const float lengthSquared = axis.LengthSquared();
		if (!std::isfinite(lengthSquared))
		{
			LOG_ERROR("[ParticleRibbon] Lightning endpoint separation is invalid.");
			m_generatedPoints.clear();
			m_lightningGenerationFailed = true;
			return;
		}
		if (lengthSquared > 1e-8f)
			axis /= std::sqrt(lengthSquared);
		else
			axis = Vector3::Up;
		const Vector3 helper = std::abs(axis.y) < 0.9f ? Vector3::UnitY : Vector3::UnitX;
		Vector3 side = axis.Cross(helper);
		side.Normalize();
		Vector3 secondSide = axis.Cross(side);
		secondSide.Normalize();
		try
		{
			m_generatedPoints.clear();
			m_generatedPoints.reserve(static_cast<std::size_t>(segments) + 1);
			for (std::uint32_t index = 0; index <= segments; ++index)
			{
				const float t = static_cast<float>(index) / static_cast<float>(segments);
				const ParticleRibbonPoint& first = source.front();
				const ParticleRibbonPoint& last = source.back();
				ParticleRibbonPoint point{
					Vector3::Lerp(start, end, t),
					first.color + (last.color - first.color) * t,
					std::lerp(first.width, last.width, t)
				};
				if (index != 0 && index != segments)
				{
					const float envelope = std::sin(RIBBON_PI * t) * m_settings.lightningAmplitude;
					const float jitterA = static_cast<float>(nextRandom(state) & 0x00ffffffu) / 16777215.0f * 2.0f - 1.0f;
					const float jitterB = static_cast<float>(nextRandom(state) & 0x00ffffffu) / 16777215.0f * 2.0f - 1.0f;
					point.position += (side * jitterA + secondSide * jitterB) * envelope;
				}
				m_generatedPoints.push_back(point);
			}
		}
		catch (const std::bad_alloc&)
		{
			LOG_ERROR("[ParticleRibbon] Failed to allocate procedural lightning points.");
			m_generatedPoints.clear();
			m_lightningGenerationFailed = true;
		}
		catch (const std::length_error&)
		{
			LOG_ERROR("[ParticleRibbon] Procedural lightning exceeds vector capacity.");
			m_generatedPoints.clear();
			m_lightningGenerationFailed = true;
		}
		m_generatedWorldSpace = m_hasRuntimePoints || !m_settings.localSpace;
	}

	void ParticleRibbonComponent::resolvePoints(std::vector<ParticleRibbonPoint>& points,
		bool& worldSpace) const
	{
		const std::vector<ParticleRibbonPoint>* source = nullptr;
		if (m_settings.mode == ParticleRibbonMode::Lightning && !m_generatedPoints.empty())
		{
			source = &m_generatedPoints;
			worldSpace = m_generatedWorldSpace;
		}
		else if (m_hasRuntimePoints)
		{
			source = &m_runtimePoints;
			worldSpace = true;
		}
		else
		{
			source = &m_settings.controlPoints;
			worldSpace = !m_settings.localSpace;
		}
		points.clear();
		if (m_settings.mode == ParticleRibbonMode::Beam && source->size() > 2)
		{
			points.push_back(source->front());
			points.push_back(source->back());
		}
		else
			points.assign(source->begin(), source->end());
	}

	bool ParticleRibbonComponent::submitRenderData()
	{
		if (m_settings.mode == ParticleRibbonMode::Lightning && m_lightningGenerationFailed)
		{
			LOG_ERROR("[ParticleRibbon] Lightning geometry is unavailable; render submission was skipped.");
			return false;
		}
		std::vector<ParticleRibbonPoint> resolved;
		bool worldSpace = false;
		try
		{
			resolvePoints(resolved, worldSpace);
			if (resolved.size() < 2)
				return true;
			const GameObject* const object = getGameObject();
			const Matrix world = object ? object->getWorldMatrix() : Matrix::Identity;
			m_renderPoints.clear();
			m_renderPoints.reserve(resolved.size());
			for (const ParticleRibbonPoint& point : resolved)
			{
				const Vector3 position = worldSpace ? point.position : Vector3::Transform(point.position, world);
				if (!isFinite(position))
				{
					LOG_ERROR("[ParticleRibbon] World-space point transformation is invalid.");
					return false;
				}
				m_renderPoints.push_back({ position, point.color, point.width });
			}
			ParticleRenderRibbon ribbon;
			ribbon.points = m_renderPoints;
			ribbon.blendMode = m_settings.blendMode;
			return ParticleRenderSubmissionQueue::instance().submit({},
				{}, std::span<const ParticleRenderRibbon>(&ribbon, 1));
		}
		catch (const std::bad_alloc&)
		{
			LOG_ERROR("[ParticleRibbon] Failed to allocate render geometry.");
			return false;
		}
		catch (const std::length_error&)
		{
			LOG_ERROR("[ParticleRibbon] Render geometry exceeds vector capacity.");
			return false;
		}
	}

	bool ParticleRibbonComponent::serializePayload(std::vector<std::uint8_t>& payload) const
	{
		flatbuffers::FlatBufferBuilder builder;
		std::vector<Serialization::ParticleRibbonPoint> controlPoints;
		controlPoints.reserve(m_settings.controlPoints.size());
		for (const auto& point : m_settings.controlPoints)
		{
			const Serialization::Vec3 position(point.position.x, point.position.y, point.position.z);
			const Serialization::Vec4 color(point.color.x, point.color.y, point.color.z, point.color.w);
			controlPoints.emplace_back(position, color, point.width);
		}
		const auto points = builder.CreateVectorOfStructs(controlPoints);
		const auto root = Serialization::CreateParticleRibbonComponentPayload(builder,
			static_cast<std::uint8_t>(m_settings.mode), m_settings.localSpace, points,
			m_settings.lightningSegments, m_settings.lightningAmplitude, m_settings.updateInterval,
			m_settings.seed, static_cast<std::uint8_t>(m_settings.blendMode));
		Serialization::FinishParticleRibbonComponentPayloadBuffer(builder, root);
		payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
		return true;
	}

	bool ParticleRibbonComponent::deserializePayload(const std::uint32_t version,
		const std::span<const std::uint8_t> payload)
	{
		flatbuffers::Verifier verifier(payload.data(), payload.size());
		if (version != 1 || payload.size() < 8
			|| !Serialization::VerifyParticleRibbonComponentPayloadBuffer(verifier))
		{
			LOG_ERROR("[ParticleRibbon] Invalid ribbon component payload.");
			return false;
		}
		const auto* const source = Serialization::GetParticleRibbonComponentPayload(payload.data());
		const auto* const sourcePoints = source->control_points();
		if (sourcePoints == nullptr || sourcePoints->size() > MAX_POINTS)
		{
			LOG_ERROR("[ParticleRibbon] Ribbon payload is missing control points or exceeds the point limit.");
			return false;
		}
		ParticleRibbonSettings settings;
		settings.mode = static_cast<ParticleRibbonMode>(source->mode());
		settings.localSpace = source->local_space();
		settings.lightningSegments = source->lightning_segments();
		settings.lightningAmplitude = source->lightning_amplitude();
		settings.updateInterval = source->update_interval();
		settings.seed = source->seed();
		settings.blendMode = static_cast<ParticleBlendMode>(source->blend_mode());
		settings.controlPoints.clear();
		settings.controlPoints.reserve(sourcePoints->size());
		for (const auto* point : *sourcePoints)
			settings.controlPoints.push_back({
				Vector3(point->position().x(), point->position().y(), point->position().z()),
				Vector4(point->color().x(), point->color().y(), point->color().z(), point->color().w()),
				point->width()
				});
		return setSettings(settings);
	}

	bool ParticleRibbonComponent::copyConfigurationTo(Component& destination) const
	{
		auto* const target = dynamic_cast<ParticleRibbonComponent*>(&destination);
		if (target == nullptr)
		{
			LOG_ERROR("[ParticleRibbon] Cannot copy settings to a different component type.");
			return false;
		}
		return target->setSettings(m_settings);
	}

	void ParticleRibbonComponent::onImGui()
	{
		ParticleRibbonSettings value = m_settings;
		bool changed = false;
		int mode = static_cast<int>(value.mode);
		if (ImGui::Combo("Mode", &mode, "Ribbon\0Beam\0Lightning\0"))
		{
			value.mode = static_cast<ParticleRibbonMode>(mode);
			if (value.mode != ParticleRibbonMode::Ribbon && value.controlPoints.size() > 2)
			{
				const auto endpoints = std::array{ value.controlPoints.front(), value.controlPoints.back() };
				value.controlPoints.assign(endpoints.begin(), endpoints.end());
			}
			changed = true;
		}
		changed |= ImGui::Checkbox("Local Space", &value.localSpace);
		int blend = static_cast<int>(value.blendMode);
		if (ImGui::Combo("Blend", &blend, "Alpha\0Additive\0Premultiplied Alpha\0Multiply\0Screen\0"))
		{
			value.blendMode = static_cast<ParticleBlendMode>(blend);
			changed = true;
		}
		if (value.mode == ParticleRibbonMode::Lightning)
		{
			int segments = static_cast<int>(value.lightningSegments);
			if (ImGui::SliderInt("Segments", &segments, 2, static_cast<int>(MAX_LIGHTNING_SEGMENTS)))
			{
				value.lightningSegments = static_cast<std::uint32_t>(segments);
				changed = true;
			}
			changed |= ImGui::DragFloat("Amplitude", &value.lightningAmplitude, 0.01f, 0.0f, 10000.0f);
			changed |= ImGui::DragFloat("Update Interval", &value.updateInterval, 0.01f, 0.01f, 10.0f);
			int seed = static_cast<int>(value.seed);
			if (ImGui::InputInt("Seed", &seed))
			{
				value.seed = static_cast<std::uint32_t>(std::max(seed, 1));
				changed = true;
			}
		}
		const std::size_t pointCount = value.mode == ParticleRibbonMode::Ribbon
			? value.controlPoints.size() : std::min<std::size_t>(2, value.controlPoints.size());
		for (std::size_t index = 0; index < pointCount; ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			const std::string label = "Point " + std::to_string(index);
			if (ImGui::TreeNode(label.c_str()))
			{
				changed |= ImGui::DragFloat3("Position", &value.controlPoints[index].position.x, 0.01f);
				changed |= ImGui::ColorEdit4("Color", &value.controlPoints[index].color.x);
				changed |= ImGui::DragFloat("Width", &value.controlPoints[index].width, 0.01f, 0.001f, 10000.0f);
				if (value.mode == ParticleRibbonMode::Ribbon && value.controlPoints.size() > 2
					&& ImGui::Button("Remove Point"))
				{
					value.controlPoints.erase(value.controlPoints.begin()
						+ static_cast<std::ptrdiff_t>(index));
					changed = true;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (value.mode == ParticleRibbonMode::Ribbon)
		{
			ImGui::BeginDisabled(value.controlPoints.size() >= MAX_POINTS);
			if (ImGui::Button("Add Ribbon Point"))
			{
				auto point = value.controlPoints.back();
				point.position.x += 0.5f;
				value.controlPoints.push_back(point);
				changed = true;
			}
			ImGui::EndDisabled();
		}
		ImGui::TextDisabled("Runtime API points are world-space and do not alter saved authoring points.");
		if (changed && !setSettings(value))
			ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Invalid ribbon settings; previous values were retained.");
	}
}