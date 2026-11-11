#pragma once

#include "Assets\StateMachine\StateMachineAsset.h"
#include <array>
#include <unordered_map>

namespace Engine
{
    /**
     * @brief Dockable ImGui graph editor for shared State Machine assets.
     * @thread_safety Main thread only.
     */
    class StateMachineGraphEditor
    {
    public:
        /** @brief Draws the graph window when its visibility flag is enabled. */
        void draw(bool& visible);

    private:
        bool createAsset();
        bool openAsset(HWND owner);
        bool saveAsset(HWND owner);
        bool addState(float x, float y);
        bool addTransition(StateMachineID source, StateMachineID destination);
        void rebuildStateIndex() const;
        StateMachineState* findState(StateMachineID id);
        const StateMachineState* findState(StateMachineID id) const;
        void invalidateStateIndex() noexcept { m_stateIndexDirty = true; }
        void drawCanvas(float width, float height);
        void drawInspector();
        void drawParameters();
        void drawActions(std::vector<StateMachineAction>& actions, std::string_view label);
        bool selectAnimationClip(StateMachineAction& action, HWND owner);
        void markDirty() noexcept { m_dirty = true; }

        StateMachineAssetHandle m_handle;
        StateMachineAsset m_draft;
        mutable std::unordered_map<StateMachineID, std::size_t> m_stateIndices;
        StateMachineID m_selectedState = 0;
        StateMachineID m_linkSource = 0;
        float m_panX = 32.0f;
        float m_panY = 32.0f;
        float m_zoom = 1.0f;
        bool m_hasAsset = false;
        bool m_dirty = false;
        mutable bool m_stateIndexDirty = true;
        std::string m_status;
    };
}
