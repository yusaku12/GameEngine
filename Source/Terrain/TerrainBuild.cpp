#include "Pch.h"
#include "Terrain\TerrainBuild.h"
#include "Physics\PhysicsMeshAsset.h"
#include "Terrain\TerrainGpuNoise.h"

namespace Engine
{
    bool cookTerrainChunks(const ModelResource& model, std::vector<std::shared_ptr<const PhysicsMeshAsset>>& assets,
        const std::atomic<bool>* cancelled)
    {
        if (model.meshes.empty())
        {
            LOG_ERROR("[TerrainCollider] Terrain must contain at least one chunk.");
            return false;
        }
        std::vector<std::shared_ptr<const PhysicsMeshAsset>> replacement;
        replacement.reserve(model.meshes.size());
        for (const auto& mesh : model.meshes)
        {
            if (cancelled && cancelled->load(std::memory_order_acquire))
                return false;
            std::vector<Vector3> vertices;
            vertices.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices)
                vertices.push_back(vertex.position);
            if (mesh.subMeshes.size() != 1 || mesh.subMeshes[0].indexStart != 0
                || mesh.subMeshes[0].indexCount > mesh.indices.size())
            {
                LOG_ERROR("[TerrainCollider] Expected the full-resolution chunk in submesh zero.");
                return false;
            }
            auto asset = PhysicsMeshAsset::cookTriangle(vertices,
                std::span(mesh.indices).first(mesh.subMeshes[0].indexCount));
            if (!asset)
                return false;
            replacement.push_back(std::move(asset));
        }
        assets = std::move(replacement);
        return true;
    }

    bool validateTerrainBuildRequest(const TerrainBuildRequest& request)
    {
        if (!validateTerrainMeshSettings(request.mesh) || !validateTerrainMaterial(request.material)
            || !validateTerrainChunkSettings(request.chunks) || !validateTerrainLodSettings(request.lod)
            || !validateTerrainNoiseSettings(request.noise) || !validateTerrainErosionSettings(request.erosion)
            || !validateTerrainVegetation(request.vegetation, request.mesh))
            return false;
        if (request.gpuNoise && (request.source != TerrainBuildSource::Noise || !TerrainGpuNoise::instance().isAvailable()))
        {
            LOG_ERROR("[TerrainBuild] GPU noise requires a Noise request and an initialized available GPU backend.");
            return false;
        }
        switch (request.source)
        {
        case TerrainBuildSource::Heightmap:
            if (request.heightmap.width() >= 2 && request.heightmap.width() <= Heightmap::MAX_DIMENSION
                && request.heightmap.height() >= 2 && request.heightmap.height() <= Heightmap::MAX_DIMENSION
                && request.heightmap.samples().size() == static_cast<std::size_t>(request.heightmap.width()) * request.heightmap.height())
                return true;
            break;
        case TerrainBuildSource::Noise:
            return true;
        case TerrainBuildSource::Flat:
            if (request.flatWidth >= 2 && request.flatWidth <= Heightmap::MAX_DIMENSION
                && request.flatHeight >= 2 && request.flatHeight <= Heightmap::MAX_DIMENSION
                && std::isfinite(request.flatHeightValue) && request.flatHeightValue >= 0 && request.flatHeightValue <= 1)
                return true;
            break;
        case TerrainBuildSource::Image:
            if (!request.image.empty())
                return true;
            break;
        }
        LOG_ERROR("[TerrainBuild] Invalid terrain source, flat grid or image path.");
        return false;
    }

    bool buildTerrain(TerrainBuildRequest request, TerrainBuildResult& result,
        const std::atomic<bool>* cancelled, std::atomic<TerrainGenerationStage>* stage)
    {
        const auto enter = [&](const TerrainGenerationStage value)
        {
            if (cancelled && cancelled->load(std::memory_order_acquire))
                return false;
            if (stage)
                stage->store(value, std::memory_order_relaxed);
            return true;
        };
        if (!enter(TerrainGenerationStage::Source) || !validateTerrainBuildRequest(request))
            return false;
        switch (request.source)
        {
        case TerrainBuildSource::Noise:
            if (request.gpuNoise
                ? (!enter(TerrainGenerationStage::GpuNoise)
                    || !TerrainGpuNoise::instance().generate(request.noise, request.heightmap, cancelled, stage))
                : !generateTerrainNoise(request.noise, request.heightmap, cancelled))
                return false;
            break;
        case TerrainBuildSource::Flat:
            if (!request.heightmap.createFlat(request.flatWidth, request.flatHeight, request.flatHeightValue))
                return false;
            break;
        case TerrainBuildSource::Image:
            if (!request.heightmap.loadImage(request.image))
                return false;
            break;
        case TerrainBuildSource::Heightmap:
            break;
        }
        TerrainBuildResult prepared;
        if (request.applyErosion && (!enter(TerrainGenerationStage::Erosion)
            || !erodeTerrain(request.heightmap, request.erosion, request.heightmap, cancelled)))
            return false;
        if (!enter(TerrainGenerationStage::Mesh) || !buildTerrainModel(request.heightmap, request.mesh, prepared.model)
            || !enter(TerrainGenerationStage::Layers) || !applyTerrainLayerWeights(request.heightmap, request.material, prepared.model)
            || !enter(TerrainGenerationStage::Chunks) || !partitionTerrainModel(prepared.model,
                request.heightmap.width(), request.heightmap.height(), request.chunks, prepared.chunks)
            || !enter(TerrainGenerationStage::Collision))
            return false;
        if (request.collision && !cookTerrainChunks(prepared.model, prepared.collision, cancelled))
            return false;
        if (!enter(TerrainGenerationStage::Lod))
            return false;
        prepared.lod = std::make_shared<TerrainLodSnapshot>();
        if (!buildTerrainLods(prepared.model, prepared.chunks, request.lod, *prepared.lod)
            || !enter(TerrainGenerationStage::Vegetation)
            || !buildTerrainVegetation(request.heightmap, request.mesh, request.vegetation, prepared.vegetation, cancelled)
            || (cancelled && cancelled->load(std::memory_order_acquire)))
            return false;
        prepared.applied = std::move(request);
        result = std::move(prepared);
        return true;
    }
}
