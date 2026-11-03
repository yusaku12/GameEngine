#pragma once

#include <memory>
#include "Assets\Model\ModelTypes.h"
#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"
#include "Terrain\Heightmap.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Terrain\TerrainNoise.h"
#include "Terrain\TerrainMaterial.h"
#include "Terrain\TerrainSurface.h"
#include "Terrain\TerrainChunks.h"
#include "Terrain\TerrainLod.h"
#include "Terrain\TerrainGeneration.h"

namespace Engine
{
    /**
     * @brief Owns height samples and generated terrain geometry, independent of source images.
     * @details Optional TerrainCollider provides PhysX collision. Surface queries do not require a collider.
     * @thread_safety Not thread-safe. Use on the game/editor thread without concurrent rendering updates.
     */
    class TerrainComponent final : public Component
    {
    public:
        /** @brief Creates a flat terrain; GPU resources are allocated on first submission. */
        TerrainComponent();
        ~TerrainComponent() override;
        GE_DISABLE_COPY_AND_MOVE(TerrainComponent);

        /**
         * @brief Atomically rebuilds terrain from validated height samples and mesh settings.
         * @param heightmap Normalized samples to copy into the component.
         * @param settings Local dimensions and UV repeats.
         * @return True on success. Failure preserves the previous terrain and is logged.
         */
        bool setTerrain(const Heightmap& heightmap, const TerrainMeshSettings& settings);

        /**
         * @brief Generates and applies Perlin/FBM heights and mesh settings transactionally.
         * @param noise Generation recipe to retain for reproducible editing.
         * @param mesh Local-space dimensions and UV tiling.
         * @return True on success. Failure preserves the previous geometry and applied recipe.
         */
        bool generateNoise(const TerrainNoiseSettings& noise, const TerrainMeshSettings& mesh);

        /**
         * @brief Synchronously erodes applied heights and rebuilds all dependent resources transactionally.
         * @param settings Seeded droplet recipe in grid-cell/normalized-height units.
         * @return True on success. Failure preserves applied terrain, recipe and pending work.
         * @details Explicit, cumulative operation; ordinary rebuilds never repeat erosion.
         */
        bool applyErosion(const TerrainErosionSettings& settings);
        /**
         * @brief Queues erosion of a snapshot of applied heights, retaining applied mesh/material/chunk/LOD settings.
         * @param settings Recipe copied at acceptance; repeated requests do not accumulate pending results.
         * @return True if accepted. Latest accepted request wins; previous terrain stays live until application.
         * @thread_safety Owner thread only, as with other generation requests.
         */
        bool requestErosion(const TerrainErosionSettings& settings);
        /** @brief Gets the last applied erosion recipe, or defaults before any erosion. Not a replay flag. */
        const TerrainErosionSettings& getErosionSettings() const noexcept { return m_erosionSettings; }

        /**
         * @brief Queues a background heightmap build using the other applied settings.
         * @param heightmap Samples copied at request time; caller may change them after return.
         * @param mesh Dimensions/UVs captured for this request.
         * @return True if accepted, not necessarily applied. A newer accepted request supersedes this one.
         * @thread_safety Owner thread only; do not call concurrently with rendering/scene updates.
         */
        bool requestTerrain(const Heightmap& heightmap, const TerrainMeshSettings& mesh);
        /** @brief Queues noise generation and terrain preparation; last applied noise recipe changes only on success. */
        bool requestNoise(const TerrainNoiseSettings& noise, const TerrainMeshSettings& mesh);
        /** @brief Queues flat sample generation and terrain preparation; invalid input preserves existing work. */
        bool requestFlat(std::uint32_t width, std::uint32_t height, float value, const TerrainMeshSettings& mesh);
        /** @brief Queues image decode and terrain preparation; image I/O happens on the background worker. */
        bool requestHeightmap(const std::filesystem::path& path, const TerrainMeshSettings& mesh);
        /** @brief Queues rebuild of applied heights with captured mesh/material/chunk/LOD settings. */
        bool requestRebuild(const TerrainMeshSettings& mesh, const TerrainMaterialSettings& material,
            const TerrainChunkSettings& chunks, const TerrainLodSettings& lod);
        /** @brief Cancels pending work without waiting; the previous terrain remains active. */
        void cancelGeneration() noexcept;
        /** @brief Gets owner-thread progress/telemetry; only Applied means the new terrain is live. */
        TerrainGenerationStatus getGenerationStatus() const noexcept;

        /** @brief Gets the last applied noise recipe, or defaults if none was applied. */
        const TerrainNoiseSettings& getNoiseSettings() const noexcept { return m_noiseSettings; }

        /**
         * @brief Applies four-layer material data and rebuilds automatic vertex weights transactionally.
         * @param settings Validated layers. Disabled settings restore the standard default material.
         * @return True on success. Failure preserves geometry and applied material settings.
         */
        bool setMaterialSettings(const TerrainMaterialSettings& settings);
        /** @brief Gets applied surface settings, independent of unapplied Inspector edits. */
        const TerrainMaterialSettings& getMaterialSettings() const noexcept { return m_materialSettings; }
        /** @brief Gets the generated terrain material handle; invalid when layers are disabled. Do not unload externally. */
        MaterialHandle getMaterial() const noexcept { return m_material; }

        /** @brief Gets local height and geometric normal; nullopt outside the centered local XZ grid. */
        std::optional<TerrainSurface> sampleLocalSurface(float x, float z) const noexcept;
        /**
         * @brief Gets the highest terrain intersection at world XZ, ignoring input Y.
         * @param position Finite world coordinates. Uses the full GameObject/parent transform.
         * @return World position and upward geometric normal, or nullopt if no intersection.
         */
        std::optional<TerrainSurface> sampleWorldSurface(const Vector3& position) const noexcept;

        /** @brief Gets immutable height data, including all samples saved with the scene. */
        const Heightmap& getHeightmap() const noexcept { return m_heightmap; }
        /** @brief Gets the applied mesh settings. */
        const TerrainMeshSettings& getSettings() const noexcept { return m_settings; }
        /**
         * @brief Repartitions the applied terrain transactionally, including active PhysX shapes.
         * @param settings Chunk edge in cells (16..1024). Generation remains synchronous.
         * @return True on success; failure logs an error and preserves applied terrain/collision.
         */
        bool setChunkSettings(const TerrainChunkSettings& settings);
        /** @brief Gets applied chunk dimensions, independent of Inspector drafts. */
        const TerrainChunkSettings& getChunkSettings() const noexcept { return m_chunkSettings; }
        /** @brief Gets row-major chunks; references expire after successful regeneration. Empty before generation. */
        std::span<const TerrainChunk> getChunks() const noexcept { return m_chunks; }
        /**
         * @brief Applies opt-in distance LOD transactionally; physics and height queries keep full resolution.
         * @param settings First transition distance and maximum simplification level.
         * @return True on success; invalid settings are logged and preserve previous resources.
         */
        bool setLodSettings(const TerrainLodSettings& settings);
        /** @brief Gets applied LOD settings, independent of Inspector drafts. */
        const TerrainLodSettings& getLodSettings() const noexcept { return m_lodSettings; }
        /** @brief Gets immutable index ranges matching getModel(), or null before generation. */
        const std::shared_ptr<const TerrainLodSnapshot>& getLodSnapshot() const noexcept { return m_lodSnapshot; }
        /** @brief Gets the generated model handle, invalid before first generation. Do not unload it externally. */
        ModelHandle getModel() const noexcept { return m_model; }
        /** @brief Sets shadow participation. */
        void setCastShadows(bool value) noexcept { m_castShadows = value; }
        /** @brief Gets shadow participation. */
        bool getCastShadows() const noexcept { return m_castShadows; }

        /** @brief Gets the version of the embedded terrain payload. */
        std::uint32_t getPayloadVersion() const noexcept override { return 6; }
        /** @brief Saves samples and settings; no external image/model reference is required. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Restores and validates samples and settings before replacing the terrain. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Copies authoring data into independent generated geometry for Play/duplication. */
        bool copyConfigurationTo(Component& destination) const override;

    protected:
        void onLateUpdate(float deltaTime) override;
        void onImGui() override;
        void onDisable() override;
        void onDestroy() override;

    private:
        friend class GameObjectManager;
        struct DraftSettings
        {
            TerrainMeshSettings mesh;
            TerrainMaterialSettings material;
            TerrainChunkSettings chunks;
            TerrainLodSettings lod;
            TerrainNoiseSettings noise;
            TerrainErosionSettings erosion;
        };
        void submitRenderData();
        void requestHeightmapImport();
        bool rebuild(const Heightmap& heights, const TerrainMeshSettings& mesh, const TerrainMaterialSettings& surface,
            const TerrainChunkSettings& chunks, const TerrainLodSettings& lod);
        TerrainBuildRequest makeBuildRequest() const;
        bool queueBuild(TerrainBuildRequest request);
        bool applyBuild(TerrainBuildResult&& result, const DraftSettings* submittedDrafts = nullptr);
        void drawMaterialInspector();
        void requestLayerTexture(std::size_t layer, std::size_t texture);

        Heightmap m_heightmap;
        TerrainMeshSettings m_settings;
        TerrainMeshSettings m_editSettings;
        TerrainChunkSettings m_chunkSettings;
        TerrainChunkSettings m_editChunkSettings;
        std::vector<TerrainChunk> m_chunks;
        TerrainLodSettings m_lodSettings;
        TerrainLodSettings m_editLodSettings;
        std::shared_ptr<const TerrainLodSnapshot> m_lodSnapshot;
        std::shared_ptr<TerrainGenerationTask> m_generation;
        TerrainNoiseSettings m_noiseSettings;
        TerrainNoiseSettings m_editNoiseSettings;
        TerrainErosionSettings m_erosionSettings;
        TerrainErosionSettings m_editErosionSettings;
        TerrainMaterialSettings m_materialSettings;
        TerrainMaterialSettings m_editMaterialSettings;
        ModelHandle m_model;
        MaterialHandle m_material;
        std::uint32_t m_objectID = 0;
        bool m_castShadows = true;
        bool m_generationFailed = false;
        int m_flatWidth = 65;
        int m_flatHeight = 65;
        float m_flatValue = 0.0f;
        std::string m_status;
    };
}
