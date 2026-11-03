#pragma once

#include <atomic>
#include <memory>
#include "Assets\Model\Resource\ModelResource.h"
#include "Terrain\Heightmap.h"
#include "Terrain\TerrainChunks.h"
#include "Terrain\TerrainLod.h"
#include "Terrain\TerrainMaterial.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Terrain\TerrainNoise.h"
#include "Terrain\TerrainErosion.h"

namespace Engine
{
    class PhysicsMeshAsset;

    /** @brief Background request source; all settings are captured when requested. */
    enum class TerrainBuildSource { Heightmap, Noise, Flat, Image };
    /** @brief Stage of a background build. Cancellation is checked between CPU stages and cooked chunks. */
    enum class TerrainGenerationStage
    {
        Idle, Queued, Source, GpuNoise, GpuReadback, Erosion, Mesh, Layers, Chunks, Collision, Lod, Ready, Applied, Cancelled, Failed
    };

    /** @brief Value-only input. Builders never access components, managers, render resources or PhysX actors. */
    struct TerrainBuildRequest
    {
        TerrainBuildSource source = TerrainBuildSource::Heightmap;
        Heightmap heightmap;
        TerrainNoiseSettings noise;
        bool gpuNoise = false;
        TerrainErosionSettings erosion;
        bool applyErosion = false;
        TerrainMeshSettings mesh;
        TerrainMaterialSettings material;
        TerrainChunkSettings chunks;
        TerrainLodSettings lod;
        std::filesystem::path image;
        std::uint32_t flatWidth = 65, flatHeight = 65;
        float flatHeightValue = 0;
        bool collision = false;
    };

    /** @brief Prepared CPU result; live resources are replaced separately at a frame barrier. */
    struct TerrainBuildResult
    {
        TerrainBuildRequest applied;
        ModelResource model;
        std::vector<TerrainChunk> chunks;
        std::shared_ptr<TerrainLodSnapshot> lod;
        std::vector<std::shared_ptr<const PhysicsMeshAsset>> collision;
    };

    /** @brief Validates input settings without image I/O, generation or manager registration; logs failures. */
    bool validateTerrainBuildRequest(const TerrainBuildRequest& request);
    /**
     * @brief Cooks only full-resolution submesh-zero triangles into row-major immutable assets.
     * @param model Generated chunk model, optionally containing LOD index tails.
     * @param assets Replaced only on success.
     * @param cancelled Optional cancellation flag checked between chunks.
     * @return False on logged failure or explicit cancellation.
     * @thread_safety Independent model/asset pairs may be cooked concurrently while PhysX is initialized.
     */
    bool cookTerrainChunks(const ModelResource& model, std::vector<std::shared_ptr<const PhysicsMeshAsset>>& assets,
        const std::atomic<bool>* cancelled = nullptr);
    /**
     * @brief Prepares terrain and cooked collision, without touching live scene objects or render resources.
     * @param request Independent input snapshot, including resolved material references.
     * @param result Receives prepared output on success.
     * @param cancelled Optional cancellation flag, checked between stages.
     * @param stage Optional atomic progress stage.
     * @return False on logged failure or explicit cancellation; no live terrain is modified.
     * @thread_safety Independent requests/results may be built concurrently while PhysX is initialized.
     * GPU noise additionally requires an initialized TerrainGpuNoise backend; only the caller waits for its compute fence.
     */
    bool buildTerrain(TerrainBuildRequest request, TerrainBuildResult& result,
        const std::atomic<bool>* cancelled = nullptr, std::atomic<TerrainGenerationStage>* stage = nullptr);
}
