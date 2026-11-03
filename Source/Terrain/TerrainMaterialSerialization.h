#pragma once

#include "Terrain\TerrainMaterial.h"
#include "Generated\FlatBuffers\TerrainMaterial_generated.h"

namespace Engine::Serialization
{
    /**
     * @brief Writes validated surface data, optionally rebasing hints to an owning asset.
     * @param builder Destination FlatBuffer builder.
     * @param settings Resolved layer data.
     * @param result Written table offset, set only on success.
     * @param owner Destination asset file; empty preserves absolute hints for Component payloads.
     * @return True on success; failures are logged.
     */
    bool writeTerrainMaterial(flatbuffers::FlatBufferBuilder& builder, const Engine::TerrainMaterialSettings& settings,
        flatbuffers::Offset<TerrainMaterialData>& result, const std::filesystem::path& owner = {});

    /**
     * @brief Validates and reads a complete four-slot layer table without changing output on failure.
     * @param source Verified FlatBuffer table; null is invalid.
     * @param settings Output authoring data.
     * @param owner Owning asset file for relative hints; empty means working-directory-relative.
     * @return True when all fields and texture references are valid.
     */
    bool readTerrainMaterial(const TerrainMaterialData* source, Engine::TerrainMaterialSettings& settings,
        const std::filesystem::path& owner = {});
}
