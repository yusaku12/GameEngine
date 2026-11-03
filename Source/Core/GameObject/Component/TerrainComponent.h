#pragma once

#include "Assets\Model\ModelTypes.h"
#include "Core\CoreDefines.h"
#include "Core\GameObject\Component.h"
#include "Terrain\Heightmap.h"
#include "Terrain\TerrainMeshBuilder.h"

namespace Engine
{
    /**
     * @brief Owns height samples and generated terrain geometry, independent of source images.
     * @details Uses the standard model rendering path and default material; no collider is created.
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
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves samples and settings; no external image/model reference is required. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Restores and validates samples and settings before replacing the terrain. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Copies authoring data into independent generated geometry for Play/duplication. */
        bool copyConfigurationTo(Component& destination) const override;

    protected:
        void onLateUpdate(float deltaTime) override;
        void onImGui() override;

    private:
        friend class GameObjectManager;
        void submitRenderData();
        void requestHeightmapImport();

        Heightmap m_heightmap;
        TerrainMeshSettings m_settings;
        TerrainMeshSettings m_editSettings;
        ModelHandle m_model;
        std::uint32_t m_objectID = 0;
        bool m_castShadows = true;
        bool m_generationFailed = false;
        int m_flatWidth = 65;
        int m_flatHeight = 65;
        float m_flatValue = 0.0f;
        std::string m_status;
    };
}
