#include "Pch.h"
#include "Graphics\Renderer\SkinningBounds.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"
#include "Core\Math\Geometry.h"

#include <bit>

namespace Engine
{
    namespace
    {
        struct ShaderInterval
        {
            double lower = 0.0;
            double upper = 0.0;
        };

        ShaderInterval shaderInput(const float value) noexcept
        {
            // Decode subnormals without float arithmetic, even if the CPU thread enables DAZ.
            // The GPU may preserve them on load or flush them to signed zero.
            const auto bits = std::bit_cast<std::uint32_t>(value);
            if ((bits & 0x7f800000u) == 0)
            {
                const double magnitude = static_cast<double>(bits & 0x007fffffu) * 0x1p-149;
                return (bits & 0x80000000u) != 0
                    ? ShaderInterval{ -magnitude, 0.0 } : ShaderInterval{ 0.0, magnitude };
            }
            return { value, value };
        }

        bool isFiniteMatrix(const Matrix& matrix) noexcept
        {
            for (const auto& row : matrix.m)
                for (const float value : row)
                    if (!std::isfinite(value))
                        return false;
            return true;
        }

        bool shaderDot(const std::array<ShaderInterval, 4>& left,
            const std::array<ShaderInterval, 4>& right, ShaderInterval& result) noexcept
        {
            double lower = 0.0;
            double upper = 0.0;
            double magnitude = 0.0;
            for (std::size_t index = 0; index < 4; ++index)
            {
                const std::array products{ left[index].lower * right[index].lower,
                    left[index].lower * right[index].upper, left[index].upper * right[index].lower,
                    left[index].upper * right[index].upper };
                const auto [minimum, maximum] = std::minmax_element(products.begin(), products.end());
                lower += *minimum;
                upper += *maximum;
                magnitude += std::max(std::abs(*minimum), std::abs(*maximum));
            }
            // Four products and three adds, in any order, with or without FMA. Eight epsilons
            // dominate gamma(7), double evaluation error, and 16*FLT_MIN covers DAZ/FTZ.
            // Limit the absolute sum, not the cancelled result, to exclude intermediate overflow.
            if (!std::isfinite(magnitude) || magnitude > static_cast<double>(FLT_MAX) * 0.25)
                return false;
            const double error = 8.0 * FLT_EPSILON * magnitude + 16.0 * FLT_MIN;
            result = { lower - error, upper + error };
            return true;
        }

        bool shaderDivide(const ShaderInterval numerator, const ShaderInterval denominator,
            ShaderInterval& result) noexcept
        {
            const std::array quotients{ numerator.lower / denominator.lower, numerator.lower / denominator.upper,
                numerator.upper / denominator.lower, numerator.upper / denominator.upper };
            const auto [minimum, maximum] = std::minmax_element(quotients.begin(), quotients.end());
            const double magnitude = std::max(std::abs(*minimum), std::abs(*maximum));
            // Also covers the shader's approximate reciprocal/multiply implementation of division
            // (including its 2.5-ULP division tolerance), rather than assuming CPU division matches.
            const double error = 8.0 * FLT_EPSILON * magnitude + 4.0 * FLT_MIN;
            result = { *minimum - error, *maximum + error };
            // A reciprocal near the normal/subnormal boundary may flush before the multiply.
            if (denominator.upper >= 0.5 / FLT_MIN)
            {
                result.lower = std::min(result.lower, 0.0);
                result.upper = std::max(result.upper, 0.0);
            }
            return std::isfinite(magnitude) && result.lower >= -FLT_MAX && result.upper <= FLT_MAX;
        }

        bool shaderTransform(const std::array<ShaderInterval, 4>& point, const Matrix& matrix,
            std::array<ShaderInterval, 4>& result) noexcept
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                std::array<ShaderInterval, 4> coefficients{};
                for (std::size_t row = 0; row < 4; ++row)
                    coefficients[row] = shaderInput(matrix.m[row][column]);
                if (!shaderDot(point, coefficients, result[column]))
                    return false;
            }
            return true;
        }

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

    bool SkinningBounds::calculateVertices(const std::span<const ModelVertex> vertices,
        const SkinningPaletteSnapshot& palette, const Matrix& world, AABB& bounds) const
    {
        const auto fail = []()
        {
            LOG_ERROR("[SkinningBounds] Vertex fallback cannot prove finite GPU bounds; skipping culling.");
            return false;
        };
        if (vertices.empty() || palette.jointCount == 0 || palette.jointCount > MAX_SKINNING_BONES
            || !isFiniteMatrix(world))
            return fail();

        std::array<double, 3> minimum{ DBL_MAX, DBL_MAX, DBL_MAX };
        std::array<double, 3> maximum{ -DBL_MAX, -DBL_MAX, -DBL_MAX };
        const auto includePosition = [&](const std::array<ShaderInterval, 4>& local)
        {
            std::array<ShaderInterval, 4> transformed{};
            if (!shaderTransform(local, world, transformed))
                return false;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], transformed[axis].lower);
                maximum[axis] = std::max(maximum[axis], transformed[axis].upper);
            }
            return true;
        };
        for (const ModelVertex& vertex : vertices)
        {
            const std::array positions{ vertex.position.x, vertex.position.y, vertex.position.z, 1.0f };
            const std::array weights{ vertex.boneWeights.x, vertex.boneWeights.y,
                vertex.boneWeights.z, vertex.boneWeights.w };
            std::array<ShaderInterval, 4> point{};
            std::array<ShaderInterval, 4> weightIntervals{};
            std::array<ShaderInterval, 4> ones{};
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (!std::isfinite(positions[index]) || !std::isfinite(weights[index]))
                    return fail();
                point[index] = shaderInput(positions[index]);
                weightIntervals[index] = shaderInput(weights[index]);
                ones[index] = { 1.0, 1.0 };
            }
            ShaderInterval total;
            if (!shaderDot(weightIntervals, ones, total))
                return fail();
            constexpr double threshold = static_cast<double>(0.00001f);
            // Cancellation can put different valid GPU reductions on opposite sides of the
            // threshold. Enclose both branches, and never normalize using a CPU-only sum.
            if (total.lower <= threshold && !includePosition(point))
                return fail();
            if (total.upper <= threshold)
                continue;
            const ShaderInterval denominator{ std::max(threshold, total.lower), total.upper };
            std::array<ShaderInterval, 4> normalized{};
            std::array<const Matrix*, 4> bones{};
            for (std::size_t index = 0; index < 4; ++index)
            {
                const auto bone = std::min<std::size_t>(vertex.boneIndices[index], MAX_SKINNING_BONES - 1);
                bones[index] = &palette.constants.boneMatrices[bone];
                // All four matrices are read, including zero-weight lanes, and the shader
                // clamps to the buffer size, NOT jointCount.
                if (!isFiniteMatrix(*bones[index])
                    || !shaderDivide(weightIntervals[index], denominator, normalized[index]))
                    return fail();
            }
            std::array<std::array<ShaderInterval, 4>, 4> skin{};
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                {
                    std::array<ShaderInterval, 4> coefficients{};
                    for (std::size_t index = 0; index < 4; ++index)
                        coefficients[index] = shaderInput(bones[index]->m[row][column]);
                    if (!shaderDot(coefficients, normalized, skin[row][column]))
                        return fail();
                }
            std::array<ShaderInterval, 4> local{};
            for (std::size_t column = 0; column < 4; ++column)
            {
                std::array<ShaderInterval, 4> coefficients{};
                for (std::size_t row = 0; row < 4; ++row)
                    coefficients[row] = skin[row][column];
                if (!shaderDot(point, coefficients, local[column]))
                    return fail();
            }
            // Preserve the blended homogeneous w: negative-weight cancellation/rounding can
            // change it even for affine bones, which changes the following world translation.
            if (!includePosition(local))
                return fail();
        }
        std::array<float, 3> center{};
        std::array<float, 3> extents{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis])
                || minimum[axis] < -FLT_MAX || maximum[axis] > FLT_MAX)
                return fail();
            center[axis] = static_cast<float>((minimum[axis] + maximum[axis]) * 0.5);
            const double extent = std::max(static_cast<double>(center[axis]) - minimum[axis],
                maximum[axis] - static_cast<double>(center[axis]));
            if (!std::isfinite(extent) || extent < 0.0 || extent >= FLT_MAX)
                return fail();
            // Account for rounding both the stored center/extents and AABB corner arithmetic.
            const double padded = extent + 2.0 * FLT_EPSILON
                * (std::abs(static_cast<double>(center[axis])) + extent) + 4.0 * FLT_MIN;
            if (padded >= FLT_MAX)
                return fail();
            extents[axis] = std::nextafter(static_cast<float>(padded), FLT_MAX);
            if (!std::isfinite(center[axis] - extents[axis]) || !std::isfinite(center[axis] + extents[axis]))
                return fail();
        }
        bounds = AABB(Vector3(center[0], center[1], center[2]), Vector3(extents[0], extents[1], extents[2]));
        return true;
    }
}
