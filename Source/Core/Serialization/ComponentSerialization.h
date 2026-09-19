#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{
    class GameObject;

    /** @brief Scene/Prefabが保持するversion付きComponent payload。 */
    struct ComponentSnapshot
    {
        std::string type;
        bool enabled = true;
        std::uint32_t payloadVersion = 0;
        std::vector<std::uint8_t> payload;
    };
}

namespace Engine::Serialization
{
    /** @brief GameObjectのComponentを決定的な順序でsnapshot化する。 */
    bool captureComponents(const GameObject& object, std::vector<ComponentSnapshot>& snapshots);

    /** @brief Component snapshotを復元する。未知型は安全に無視する。 */
    bool restoreComponents(GameObject& object, const std::vector<ComponentSnapshot>& snapshots);
}
