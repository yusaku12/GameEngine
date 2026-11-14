#include "Pch.h"
#include "Graphics\Renderer\SkinningBounds.h"

#include <limits>

namespace
{
    using namespace Engine;
    using Float4 = std::array<float, 4>;
    std::size_t checkedPositions = 0;

    float gpuFloat(const float value, const bool ftz)
    {
        return ftz && std::abs(value) < FLT_MIN ? std::copysign(0.0f, value) : value;
    }

    float dot(const Float4& left, const Float4& right, const std::array<int, 4>& order,
        const int mode, const bool ftz)
    {
        Float4 products{};
        for (std::size_t index = 0; index < 4; ++index)
            products[index] = gpuFloat(gpuFloat(left[index], ftz) * gpuFloat(right[index], ftz), ftz);
        const auto add = [ftz](const float a, const float b) { return gpuFloat(a + b, ftz); };
        if (mode == 0)
            return add(add(add(products[order[0]], products[order[1]]), products[order[2]]), products[order[3]]);
        if (mode == 1)
            return add(add(products[order[0]], products[order[1]]), add(products[order[2]], products[order[3]]));
        float result = products[order[0]];
        for (std::size_t index = 1; index < 4; ++index)
            result = gpuFloat(std::fma(gpuFloat(left[order[index]], ftz),
                gpuFloat(right[order[index]], ftz), result), ftz);
        return result;
    }

    bool contains(const AABB& bounds, const Float4& point)
    {
        const Float4 center{ bounds.Center.x, bounds.Center.y, bounds.Center.z, 0 };
        const Float4 extent{ bounds.Extents.x, bounds.Extents.y, bounds.Extents.z, 0 };
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(point[axis])
                || static_cast<double>(point[axis]) < static_cast<double>(center[axis]) - extent[axis]
                || static_cast<double>(point[axis]) > static_cast<double>(center[axis]) + extent[axis]
                || point[axis] < center[axis] - extent[axis] || point[axis] > center[axis] + extent[axis])
                return false;
        return true;
    }

    Float4 shaderPosition(const ModelVertex& vertex, const SkinningPaletteSnapshot& palette,
        const Matrix& world, const std::array<int, 4>& order, const int mode, const bool ftz)
    {
        Float4 point{ vertex.position.x, vertex.position.y, vertex.position.z, 1.0f };
        const Float4 weights{ vertex.boneWeights.x, vertex.boneWeights.y, vertex.boneWeights.z, vertex.boneWeights.w };
        const float total = dot(weights, { 1, 1, 1, 1 }, order, mode, ftz);
        if (total > 0.00001f)
        {
            Float4 normalized{};
            for (std::size_t index = 0; index < 4; ++index)
                normalized[index] = mode == 3
                    ? gpuFloat(gpuFloat(weights[index], ftz) * gpuFloat(1.0f / total, ftz), ftz)
                    : gpuFloat(gpuFloat(weights[index], ftz) / total, ftz);
            Matrix skin;
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                {
                    Float4 coefficients{};
                    for (std::size_t index = 0; index < 4; ++index)
                        coefficients[index] = palette.constants.boneMatrices[
                            std::min<std::size_t>(vertex.boneIndices[index], MAX_SKINNING_BONES - 1)].m[row][column];
                    skin.m[row][column] = dot(coefficients, normalized, order, mode, ftz);
                }
            const Float4 input = point;
            for (std::size_t column = 0; column < 4; ++column)
                point[column] = dot(input, { skin.m[0][column], skin.m[1][column],
                    skin.m[2][column], skin.m[3][column] }, order, mode, ftz);
        }
        Float4 result{};
        for (std::size_t column = 0; column < 4; ++column)
            result[column] = dot(point, { world.m[0][column], world.m[1][column],
                world.m[2][column], world.m[3][column] }, order, mode, ftz);
        return result;
    }

    SkinningPaletteSnapshot makePalette()
    {
        SkinningPaletteSnapshot palette{};
        palette.jointCount = 1;
        palette.constants.boneMatrices.fill(Matrix::Identity);
        return palette;
    }

    bool checkShader(const std::span<const ModelVertex> vertices, const SkinningPaletteSnapshot& palette,
        const Matrix& world)
    {
        AABB bounds;
        if (!SkinningBounds{}.calculateVertices(vertices, palette, world, bounds))
            return false;
        std::array order{ 0, 1, 2, 3 };
        do
        {
            for (const int mode : { 0, 1, 2, 3 })
                for (const bool ftz : { false, true })
                    for (const auto& vertex : vertices)
                    {
                        ++checkedPositions;
                        if (!contains(bounds, shaderPosition(vertex, palette, world, order, mode, ftz)))
                            return false;
                    }
        } while (std::next_permutation(order.begin(), order.end()));
        return true;
    }

    bool testSignedWeightsAndClamp()
    {
        auto palette = makePalette();
        palette.constants.boneMatrices[0]._41 = 10.0f;
        palette.constants.boneMatrices[255]._41 = -5.0f;
        ModelVertex vertex;
        vertex.position = Vector3(2, 3, 4);
        vertex.boneIndices = { 0, 65535, 255, 0 };
        vertex.boneWeights = Vector4(2, -1, 0, 0);
        SkinningBounds cache;
        AABB result;
        return !cache.initialize(std::span(&vertex, 1))
            && cache.calculateVertices(std::span(&vertex, 1), palette, Matrix::Identity, result)
            && contains(result, { 27, 3, 4, 1 })
            && checkShader(std::span(&vertex, 1), palette, Matrix::Identity);
    }

    bool testThresholdAndHomogeneousW()
    {
        auto palette = makePalette();
        palette.constants.boneMatrices[0]._41 = 30;
        palette.constants.boneMatrices[1]._41 = -20;
        Matrix world = Matrix::Identity;
        world._41 = 10000000;
        world._42 = -20000000;
        std::vector<ModelVertex> vertices;
        for (const float residual : { 0.0f, std::nextafter(0.00001f, 0.0f), 0.00001f,
            std::nextafter(0.00001f, 1.0f), 0.00002f, -0.00002f })
        {
            ModelVertex vertex;
            vertex.position = Vector3(1, -3, 7);
            vertex.boneIndices = { 0, 1, 0, 1 };
            vertex.boneWeights = Vector4(1, -1, residual, 0);
            vertices.push_back(vertex);
        }
        if (!checkShader(vertices, palette, world))
            return false;
        // A finite non-affine palette/world is legal for the shader's non-projective xyz output.
        palette.constants.boneMatrices[0]._14 = 0.25f;
        palette.constants.boneMatrices[1]._44 = 0.75f;
        world._24 = 0.5f;
        return checkShader(vertices, palette, world);
    }

    bool testRandomized()
    {
        std::mt19937 random(15473);
        std::uniform_real_distribution<float> coordinate(-1000, 1000);
        std::uniform_real_distribution<float> weight(-2, 3);
        auto palette = makePalette();
        for (std::size_t bone = 0; bone < 4; ++bone)
            for (auto& row : palette.constants.boneMatrices[bone].m)
                for (float& value : row)
                    value = coordinate(random) * 0.01f;
        Matrix world = Matrix::Identity;
        world._41 = 1000000;
        world._42 = -2000000;
        std::vector<ModelVertex> vertices(1024);
        for (auto& vertex : vertices)
        {
            vertex.position = Vector3(coordinate(random), coordinate(random), coordinate(random));
            vertex.boneIndices = { 0, 1, 2, 3 };
            vertex.boneWeights = Vector4(weight(random), weight(random), weight(random), weight(random));
            // Test each vertex separately so a huge neighboring bound cannot hide a missed point.
            if (!checkShader(std::span(&vertex, 1), palette, world))
                return false;
        }
        return checkShader(vertices, palette, world);
    }

    bool testSubnormalsAndRigid()
    {
        auto palette = makePalette();
        ModelVertex vertex;
        vertex.position = Vector3(FLT_MIN * 0.5f, -FLT_MIN * 0.5f, FLT_MIN);
        vertex.boneWeights = Vector4(-2, 1, FLT_MIN * 0.5f, 0);
        palette.constants.boneMatrices[0]._11 = std::numeric_limits<float>::quiet_NaN();
        Matrix world = Matrix::Identity;
        world._11 = 1e30f;
        if (!checkShader(std::span(&vertex, 1), palette, world))
            return false;
        // The CPU's DAZ mode must not lose the GPU input bits before interval construction.
        AABB bounds;
        const unsigned int control = _mm_getcsr();
        _mm_setcsr(control | 0x8040u);
        const bool succeeded = SkinningBounds{}.calculateVertices(std::span(&vertex, 1), palette, world, bounds);
        _mm_setcsr(control);
        if (!succeeded || !contains(bounds, shaderPosition(vertex, palette, world, { 0, 1, 2, 3 }, 0, false)))
            return false;
        palette = makePalette();
        vertex.boneWeights = Vector4(1, FLT_MIN * 0.5f, 0, 0);
        if (!checkShader(std::span(&vertex, 1), palette, world))
            return false;
        vertex.boneWeights = Vector4(4e37f, 4e37f, 0, 0);
        return checkShader(std::span(&vertex, 1), palette, Matrix::Identity);
    }

    bool testRejectionsAndCache()
    {
        auto palette = makePalette();
        ModelVertex vertex;
        vertex.position = Vector3(1, 2, 3);
        vertex.boneWeights = Vector4(1, 0, 0, 0);
        SkinningBounds cache;
        AABB before;
        AABB after;
        if (!cache.initialize(std::span(&vertex, 1)) || !cache.calculate(palette, Matrix::Identity, before)
            || !cache.calculateVertices(std::span(&vertex, 1), palette, Matrix::Identity, after)
            || !cache.calculate(palette, Matrix::Identity, after)
            || std::memcmp(&before, &after, sizeof(AABB)) != 0)
            return false;
        const auto rejects = [&](const std::span<const ModelVertex> vertices,
            const SkinningPaletteSnapshot& inputPalette, const Matrix& world)
        {
            AABB output(Vector3(123, 456, 789), Vector3(4, 5, 6));
            const AABB sentinel = output;
            return !cache.calculateVertices(vertices, inputPalette, world, output)
                && std::memcmp(&output, &sentinel, sizeof(AABB)) == 0;
        };
        const auto one = std::span(&vertex, 1);
        if (!rejects({}, palette, Matrix::Identity))
            return false;
        palette.jointCount = 0;
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        palette.jointCount = MAX_SKINNING_BONES + 1;
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        palette = makePalette();
        vertex.position.x = std::numeric_limits<float>::infinity();
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        vertex.position.x = 1;
        vertex.boneWeights.y = std::numeric_limits<float>::quiet_NaN();
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        vertex.boneWeights.y = 0;
        Matrix world = Matrix::Identity;
        world._41 = std::numeric_limits<float>::infinity();
        if (!rejects(one, palette, world))
            return false;
        vertex.boneIndices[1] = 65535;
        palette.constants.boneMatrices[255]._11 = std::numeric_limits<float>::quiet_NaN();
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        palette = makePalette();
        vertex.boneWeights = Vector4(FLT_MAX, -FLT_MAX, 1, 0);
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        vertex.boneIndices = { 0, 1, 0, 0 };
        vertex.boneWeights = Vector4(2, -1, 0, 0);
        palette.constants.boneMatrices[0]._11 = FLT_MAX * 0.75f;
        palette.constants.boneMatrices[1]._11 = FLT_MAX * 0.75f;
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        // Finite blend, overflowing local transform; then finite local, overflowing world.
        palette = makePalette();
        vertex.position.x = 1e20f;
        vertex.boneWeights = Vector4(1, 0, 0, 0);
        palette.constants.boneMatrices[0]._11 = 1e20f;
        if (!rejects(one, palette, Matrix::Identity))
            return false;
        palette = makePalette();
        world = Matrix::Identity;
        world._11 = 1e20f;
        return rejects(one, palette, world);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "signed weights and shader index clamp", testSignedWeightsAndClamp },
        std::pair{ "threshold cancellation and homogeneous w", testThresholdAndHomogeneousW },
        std::pair{ "randomized shader reductions", testRandomized },
        std::pair{ "subnormals and rigid branch", testSubnormalsAndRigid },
        std::pair{ "atomic rejections and unchanged cache", testRejectionsAndCache }
    };
    for (const auto& [name, test] : tests)
    {
        if (!test())
        {
            std::fprintf(stderr, "FAIL: %s\n", name);
            return 1;
        }
        std::printf("PASS: %s\n", name);
    }
    std::printf("Skinning bounds: %zu groups passed; %zu shader positions enclosed.\n",
        tests.size(), checkedPositions);
    return 0;
}
