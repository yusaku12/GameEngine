#include "Pch.h"
#include "Graphics\Material\MaterialPropertyBlock.h"

namespace Engine
{
    namespace
    {
        constexpr std::uint32_t bit(const std::uint32_t index) noexcept { return 1u << index; }
    }

    bool MaterialPropertyBlock::setFloat(const MaterialParameterID id, const float value) noexcept
    {
        if (id == MaterialParameters::Metallic) { m_values.metallic = value; m_overrideMask |= bit(1); }
        else if (id == MaterialParameters::Roughness) { m_values.roughness = value; m_overrideMask |= bit(2); }
        else if (id == MaterialParameters::EmissiveIntensity) { m_values.emissiveIntensity = value; m_overrideMask |= bit(4); }
        else if (id == MaterialParameters::NormalScale) { m_values.normalScale = value; m_overrideMask |= bit(5); }
        else if (id == MaterialParameters::OcclusionStrength) { m_values.occlusionStrength = value; m_overrideMask |= bit(6); }
        else if (id == MaterialParameters::AlphaCutoff) { m_values.alphaCutoff = value; m_overrideMask |= bit(7); }
        else return false;
        return true;
    }

    bool MaterialPropertyBlock::setVector3(const MaterialParameterID id, const Vector3& value) noexcept
    {
        if (id != MaterialParameters::EmissiveColor)
            return false;
        m_values.emissiveColor = value;
        m_overrideMask |= bit(3);
        return true;
    }

    bool MaterialPropertyBlock::setVector4(const MaterialParameterID id, const Vector4& value) noexcept
    {
        if (id != MaterialParameters::BaseColor)
            return false;
        m_values.baseColor = value;
        m_overrideMask |= bit(0);
        return true;
    }

    void MaterialPropertyBlock::clear(const MaterialParameterID id) noexcept
    {
        const auto parameters = MaterialParameterLayout::standard();
        const auto found = std::find_if(parameters.begin(), parameters.end(),
            [id](const MaterialParameterDescriptor& parameter) { return parameter.id == id; });
        if (found != parameters.end())
            m_overrideMask &= ~bit(static_cast<std::uint32_t>(found - parameters.begin()));
    }

    void MaterialPropertyBlock::clear() noexcept
    {
        m_values = {};
        m_overrideMask = 0;
    }

    void MaterialPropertyBlock::applyTo(MaterialAsset& material) const noexcept
    {
        if (m_overrideMask & bit(0)) material.baseColor = m_values.baseColor;
        if (m_overrideMask & bit(1)) material.metallic = m_values.metallic;
        if (m_overrideMask & bit(2)) material.roughness = m_values.roughness;
        if (m_overrideMask & bit(3)) material.emissiveColor = m_values.emissiveColor;
        if (m_overrideMask & bit(4)) material.emissiveIntensity = m_values.emissiveIntensity;
        if (m_overrideMask & bit(5)) material.normalScale = m_values.normalScale;
        if (m_overrideMask & bit(6)) material.occlusionStrength = m_values.occlusionStrength;
        if (m_overrideMask & bit(7)) material.alphaCutoff = m_values.alphaCutoff;
    }
} // namespace Engine