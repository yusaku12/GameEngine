#include "Pch.h"
#include "Graphics\Renderer\SkinningBounds.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"
#include "Core\Math\Geometry.h"

namespace Engine
{
    namespace
    {
        bool isFiniteAffine(const Matrix& matrix) noexcept
        {
            for (const auto& row : matrix.m)
                for (const float value : row)
                    if (!std::isfinite(value))
                        return false;
            return matrix._14 == 0.0f && matrix._24 == 0.0f && matrix._34 == 0.0f && matrix._44 == 1.0f;
        }
    }

    void SkinningBounds::Range::include(const Vector3& point) noexcept
    {
        if (!populated)
        {
            minimum = maximum = point;
            populated = true;
            return;
        }
        minimum = Vector3(std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z));
        maximum = Vector3(std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z));
    }

    bool SkinningBounds::initialize(const std::span<const ModelVertex> vertices)
    {
        m_initialized = false;
        m_influences.clear();
        m_referencedBones.reset();
        m_rigid = {};
        if (vertices.empty())
        {
            LOG_ERROR("[SkinningBounds] Cannot build bounds for an empty mesh.");
            return false;
        }
        std::array<Range, MAX_SKINNING_BONES> ranges{};
        for (const ModelVertex& vertex : vertices)
        {
            const std::array weights{ vertex.boneWeights.x, vertex.boneWeights.y,
                vertex.boneWeights.z, vertex.boneWeights.w };
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)
                || !std::isfinite(vertex.position.z)
                || std::any_of(weights.begin(), weights.end(), [](const float weight)
                    { return !std::isfinite(weight) || weight < 0.0f; }))
            {
                LOG_ERROR("[SkinningBounds] Nonfinite vertex or invalid skinning weight; culling must remain disabled.");
                return false;
            }
            const double total = static_cast<double>(weights[0]) + weights[1] + weights[2] + weights[3];
            if (total > static_cast<double>(FLT_MAX) * 0.5)
            {
                LOG_ERROR("[SkinningBounds] Skinning weight sum is too large for safe shader arithmetic.");
                return false;
            }
            // Enclose both shader branches near its 1e-5 weight threshold, including float/FTZ rounding.
            if (total <= 0.00002)
                m_rigid.include(vertex.position);
            if (total >= 0.000005)
            {
                for (std::size_t index = 0; index < weights.size(); ++index)
                {
                    const std::uint32_t bone = std::min<std::uint32_t>(vertex.boneIndices[index],
                        static_cast<std::uint32_t>(MAX_SKINNING_BONES - 1));
                    // The shader reads all four matrices, even when an individual weight is zero.
                    m_referencedBones.set(bone);
                    if (weights[index] > 0.0f)
                        ranges[bone].include(vertex.position);
                }
            }
        }
        for (std::uint32_t bone = 0; bone < ranges.size(); ++bone)
            if (ranges[bone].populated)
                m_influences.push_back({ ranges[bone], bone });
        m_initialized = true;
        return true;
    }

    bool SkinningBounds::calculate(const SkinningPaletteSnapshot& palette, const Matrix& world, AABB& bounds) const
    {
        if (!m_initialized || palette.jointCount == 0 || palette.jointCount > MAX_SKINNING_BONES
            || !isFiniteAffine(world))
        {
            LOG_ERROR("[SkinningBounds] Bounds cache, palette count or world transform is invalid; skipping culling.");
            return false;
        }
        for (std::size_t bone = 0; bone < MAX_SKINNING_BONES; ++bone)
            if (m_referencedBones.test(bone) && !isFiniteAffine(palette.constants.boneMatrices[bone]))
            {
                const Matrix& matrix = palette.constants.boneMatrices[bone];
                LOG_ERROR("[SkinningBounds] Bone {} is not a finite affine transform "
                    "(last column: {}, {}, {}, {}); skipping culling.",
                    bone, matrix._14, matrix._24, matrix._34, matrix._44);
                return false;
            }

        std::array<double, 3> minimum{ DBL_MAX, DBL_MAX, DBL_MAX };
        std::array<double, 3> maximum{ -DBL_MAX, -DBL_MAX, -DBL_MAX };
        bool finiteShaderRange = true;
        const auto includeRange = [&](const Range& range, const Matrix& bone)
        {
            for (std::uint32_t corner = 0; corner < 8; ++corner)
            {
                const std::array<double, 4> point{
                    (corner & 1) != 0 ? range.maximum.x : range.minimum.x,
                    (corner & 2) != 0 ? range.maximum.y : range.minimum.y,
                    (corner & 4) != 0 ? range.maximum.z : range.minimum.z, 1.0 };
                std::array<double, 4> local{};
                std::array<double, 4> localMagnitude{};
                for (std::size_t column = 0; column < 4; ++column)
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        const double product = point[row] * bone.m[row][column];
                        local[column] += product;
                        localMagnitude[column] += std::abs(product);
                    }
                for (const double magnitude : localMagnitude)
                    finiteShaderRange = finiteShaderRange && magnitude <= static_cast<double>(FLT_MAX) * 0.0625;
                for (std::size_t column = 0; column < 3; ++column)
                {
                    double value = 0.0;
                    double magnitude = 0.0;
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        value += local[row] * world.m[row][column];
                        magnitude += localMagnitude[row] * std::abs(static_cast<double>(world.m[row][column]));
                    }
                    finiteShaderRange = finiteShaderRange && magnitude <= static_cast<double>(FLT_MAX) * 0.0625;
                    // Cover float weight normalization, matrix blending, two transforms and cancellation.
                    const double error = 64.0 * FLT_EPSILON * std::max(1.0, magnitude);
                    minimum[column] = std::min(minimum[column], value - error);
                    maximum[column] = std::max(maximum[column], value + error);
                }
            }
        };
        if (m_rigid.populated)
            includeRange(m_rigid, Matrix::Identity);
        for (const Influence& influence : m_influences)
            includeRange(influence.range, palette.constants.boneMatrices[influence.bone]);
        if (!finiteShaderRange)
        {
            LOG_ERROR("[SkinningBounds] Skinning arithmetic may overflow GPU floats; skipping culling.");
            return false;
        }

        std::array<float, 3> center{};
        std::array<float, 3> extents{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const double midpoint = (minimum[axis] + maximum[axis]) * 0.5;
            if (!std::isfinite(midpoint) || std::abs(midpoint) > FLT_MAX)
            {
                LOG_ERROR("[SkinningBounds] Animated bounds exceed finite float coordinates; skipping culling.");
                return false;
            }
            center[axis] = static_cast<float>(midpoint);
            const double extent = std::max(static_cast<double>(center[axis]) - minimum[axis],
                maximum[axis] - static_cast<double>(center[axis]));
            if (!std::isfinite(extent) || extent < 0.0 || extent >= FLT_MAX)
            {
                LOG_ERROR("[SkinningBounds] Animated bounds exceed finite float extents; skipping culling.");
                return false;
            }
            extents[axis] = std::nextafter(static_cast<float>(extent), FLT_MAX);
        }
        bounds = AABB(Vector3(center[0], center[1], center[2]), Vector3(extents[0], extents[1], extents[2]));
        return true;
    }
}
