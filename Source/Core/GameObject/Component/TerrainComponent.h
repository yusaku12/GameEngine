#pragma once

#include "Assets\Model\ModelTypes.h"
#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"
#include "Terrain\Heightmap.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Terrain\TerrainNoise.h"
#include "Terrain\TerrainMaterial.h"
#include "Terrain\TerrainSurface.h"

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
        /** @brief Gets the generated model handle, invalid before first generation. Do not unload it externally. */
        ModelHandle getModel() const noexcept { return m_model; }
        /** @brief Sets shadow participation. */
        void setCastShadows(bool value) noexcept { m_castShadows = value; }
        /** @brief Gets shadow participation. */
        bool getCastShadows() const noexcept { return m_castShadows; }

        /** @brief Gets the version of the embedded terrain payload. */
        std::uint32_t getPayloadVersion() const noexcept override { return 3; }
        /** @brief Saves samples and settings; no external image/model reference is required. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Restores and validates samples and settings before replacing the terrain. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Copies authoring data into independent generated geometry for Play/duplication. */
        bool copyConfigurationTo(Component& destination) const override;

    protected:
        void onLateUpdate(float deltaTime) override;
        void onImGui() override;
        void onDestroy() override;

    private:
        friend class GameObjectManager;
        void submitRenderData();
        void requestHeightmapImport();
        bool rebuild(const Heightmap& heights, const TerrainMeshSettings& mesh, const TerrainMaterialSettings& surface);
        void drawMaterialInspector();
        void requestLayerTexture(std::size_t layer, std::size_t texture);

        Heightmap m_heightmap;
        TerrainMeshSettings m_settings;
        TerrainMeshSettings m_editSettings;
        TerrainNoiseSettings m_noiseSettings;
        TerrainNoiseSettings m_editNoiseSettings;
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
