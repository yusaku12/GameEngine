#pragma once

#include "Core\Scene\Scene.h"

namespace Engine::Serialization
{
    /**
     * @brief SceneをFlatBuffersバイナリへ保存・復元するクラス。
     * @thread_safety Main thread only.
     */
    class SceneSerializer
    {
    public:
        bool save(const std::filesystem::path& path, const Scene& scene) const;
        bool load(const std::filesystem::path& path, Scene& scene) const;
    };
} // namespace Engine::Serialization