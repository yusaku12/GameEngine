#pragma once

#include <span>

namespace Engine
{
    class Scene;

    /** @brief A source scene and empty destination for an in-memory clone. */
    struct SceneClonePair
    {
        const Scene& source;
        Scene& destination;
    };

    /**
     * @brief Clones scene configuration, preserving object GUIDs and cross-scene references.
     * @details Destinations must be empty and distinct. No lifecycle callbacks execute.
     * Failure clears every destination. Shared immutable engine assets remain shared.
     * @param scenes Source/destination pairs, including persistent objects when applicable.
     * @return True if all objects, hierarchy, components and references were copied.
     * @thread_safety Not thread-safe. Call between frame jobs.
     */
    bool cloneScenes(std::span<const SceneClonePair> scenes);
}
