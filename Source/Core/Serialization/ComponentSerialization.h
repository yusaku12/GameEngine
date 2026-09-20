#pragma once

namespace Engine
{
    class GameObject;

    /**
     * @brief Scene/Prefabが保持するversion付きComponent payload。
     */
    struct ComponentSnapshot
    {
        std::string type;                  //!< Componentの型名
        bool enabled = true;               //!< Componentの有効状態
        std::uint32_t payloadVersion = 0;  //!< payloadのバージョン
        std::vector<std::uint8_t> payload; //!< Componentのシリアライズされたpayload
    };
}

namespace Engine::Serialization
{
    /**
     * @brief GameObjectのComponentを決定的な順序でsnapshot化する。
     * @param object snapshot化するGameObject
     * @param snapshots snapshot化されたComponentのリスト
     */
    bool captureComponents(const GameObject& object, std::vector<ComponentSnapshot>& snapshots);

    /**
     * @brief Component snapshotを復元する。未知型は安全に無視する。
     * @param object 復元先のGameObject
     * @param snapshots 復元するComponentのsnapshotリスト
     */
    bool restoreComponents(GameObject& object, const std::vector<ComponentSnapshot>& snapshots);
}
