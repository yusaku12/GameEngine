#pragma once

#include "Core\Math\MathTypes.h"

namespace Engine
{
    using MaterialParameterID = std::uint32_t;

    /**
     * @brief Material Parameterの型。
     */
    enum class MaterialParameterType : std::uint8_t
    {
        Float,
        Vector3,
        Vector4,
        Color,
    };

    /**
     * @brief GPUへ連続転送できる固定Standard Material値。
     */
    struct MaterialParameterValues
    {
        Vector4 baseColor = Vector4::One;      //!< Base Color (RGBA)
        float metallic = 0.0f;                 //!< Metallic
        float roughness = 1.0f;                //!< Roughness
        float emissiveIntensity = 0.0f;        //!< Emissive Intensity
        float normalScale = 1.0f;              //!< Normal Map Scale
        Vector3 emissiveColor = Vector3::Zero; //!< Emissive Color (RGB)
        float occlusionStrength = 1.0f;        //!< Occlusion Strength
        float alphaCutoff = 0.5f;              //!< Alpha Cutoff
        std::array<float, 3> padding{};        //!< Padding for alignment
    };

    static_assert(sizeof(MaterialParameterValues) == 64);

    /**
     * @brief Shader Parameterの固定Layout要素。
     */
    struct MaterialParameterDescriptor
    {
        MaterialParameterID id;     //!< Parameter ID (安定ハッシュ値)
        std::string_view name;      //!< Parameter名 (UI表示用)
        MaterialParameterType type; //!< Parameter型
        std::uint32_t offset;       //!< MaterialParameterValues内のオフセット
        std::uint32_t size;         //!< Parameterサイズ
        float minimum;              //!< Parameter最小値 (UI表示用)
        float maximum;              //!< Parameter最大値 (UI表示用)
    };

    /**
     * @brief Parameter名から安定IDを生成する。
     * @param name Parameter名
     */
    constexpr MaterialParameterID makeMaterialParameterID(const std::string_view name) noexcept
    {
        MaterialParameterID hash = 2166136261u;
        for (const char character : name)
            hash = (hash ^ static_cast<std::uint8_t>(character)) * 16777619u;
        return hash;
    }

    namespace MaterialParameters
    {
        inline constexpr MaterialParameterID BaseColor = makeMaterialParameterID("BaseColor");
        inline constexpr MaterialParameterID Metallic = makeMaterialParameterID("Metallic");
        inline constexpr MaterialParameterID Roughness = makeMaterialParameterID("Roughness");
        inline constexpr MaterialParameterID EmissiveColor = makeMaterialParameterID("EmissiveColor");
        inline constexpr MaterialParameterID EmissiveIntensity = makeMaterialParameterID("EmissiveIntensity");
        inline constexpr MaterialParameterID NormalScale = makeMaterialParameterID("NormalScale");
        inline constexpr MaterialParameterID OcclusionStrength = makeMaterialParameterID("OcclusionStrength");
        inline constexpr MaterialParameterID AlphaCutoff = makeMaterialParameterID("AlphaCutoff");
    }

    /**
     * @brief 固定Standard ShaderのParameter Layout。
     */
    class MaterialParameterLayout
    {
    public:

        /**
         * @brief Standard MaterialのParameter一覧を返す。
         */
        static std::span<const MaterialParameterDescriptor> standard() noexcept
        {
            static constexpr std::array descriptors = {
                MaterialParameterDescriptor{ MaterialParameters::BaseColor, "Base Color", MaterialParameterType::Color, offsetof(MaterialParameterValues, baseColor), sizeof(Vector4), 0.0f, 1.0f },
                MaterialParameterDescriptor{ MaterialParameters::Metallic, "Metallic", MaterialParameterType::Float, offsetof(MaterialParameterValues, metallic), sizeof(float), 0.0f, 1.0f },
                MaterialParameterDescriptor{ MaterialParameters::Roughness, "Roughness", MaterialParameterType::Float, offsetof(MaterialParameterValues, roughness), sizeof(float), 0.0f, 1.0f },
                MaterialParameterDescriptor{ MaterialParameters::EmissiveColor, "Emissive Color", MaterialParameterType::Vector3, offsetof(MaterialParameterValues, emissiveColor), sizeof(Vector3), 0.0f, 1.0f },
                MaterialParameterDescriptor{ MaterialParameters::EmissiveIntensity, "Emissive Intensity", MaterialParameterType::Float, offsetof(MaterialParameterValues, emissiveIntensity), sizeof(float), 0.0f, 64.0f },
                MaterialParameterDescriptor{ MaterialParameters::NormalScale, "Normal Scale", MaterialParameterType::Float, offsetof(MaterialParameterValues, normalScale), sizeof(float), 0.0f, 2.0f },
                MaterialParameterDescriptor{ MaterialParameters::OcclusionStrength, "Occlusion Strength", MaterialParameterType::Float, offsetof(MaterialParameterValues, occlusionStrength), sizeof(float), 0.0f, 1.0f },
                MaterialParameterDescriptor{ MaterialParameters::AlphaCutoff, "Alpha Cutoff", MaterialParameterType::Float, offsetof(MaterialParameterValues, alphaCutoff), sizeof(float), 0.0f, 1.0f },
            };
            return descriptors;
        }
    };
} // namespace Engine