#pragma once

namespace Engine
{
    class CameraComponent;
    class PostProcessVolumeComponent;
    /**
     * @brief Draws Profile selection/shared editing, including ordered custom effects, and independent camera overrides.
     * @thread_safety Main (UI) thread only.
     */
    void drawPostProcessInspector(CameraComponent& camera);
    /**
     * @brief Draws the shared Profile controls, including ordered custom effects, for a Volume without Camera-only overrides.
     * @thread_safety Main (UI) thread only.
     */
    void drawPostProcessVolumeInspector(PostProcessVolumeComponent& volume);
}
