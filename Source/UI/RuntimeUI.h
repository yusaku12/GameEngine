#pragma once

#include <span>
#include <functional>
#include <unordered_map>
#include "Components\UI\RuntimeUIComponents.h"

namespace Engine
{
    class Scene;
    class GameObject;
    /** @brief Pixel-space rectangle used for drawing and hit testing. */
    struct UIRect
    {
        Vector2 min = Vector2::Zero;
        Vector2 max = Vector2::Zero;
        bool contains(const Vector2& point) const noexcept
        {
            return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
        }
    };
    /** @brief Immutable draw description, with no pointers into gameplay objects. */
    struct UIDraw
    {
        enum class Kind { Image, Text, Button };
        Kind kind = Kind::Image;
        ObjectGUID object;
        UIRect rect;
        UIRect clip;
        Color color = Color(1, 1, 1, 1);
        std::filesystem::path image;
        std::string text;
        std::string font;
        float fontSize = 24;
        std::uint8_t alignment = 0;
        bool interactable = false;
    };
    /**
     * @brief Canvas layout snapshots and GameUpdate button events.
     * @thread_safety Not thread-safe. Set surface on main/render between joined jobs,
     * process/capture on GameUpdate, read draw snapshots only after joining.
     */
    class RuntimeUI final
    {
    public:
        using ClickHandler = std::function<void(GameObject&)>;
        /** @brief Returns the process-wide UI service. */
        static RuntimeUI& instance() noexcept;
        /** @brief Registers a unique named click handler; receives the runtime object, never its editing original. */
        bool registerClickEvent(const std::string& name, ClickHandler handler);
        /** @brief Removes a previously registered event. */
        void unregisterClickEvent(const std::string& name);
        /** @brief Sets screen-space origin and size of the actual Game image/client area. */
        void setSurface(const Vector2& screenOrigin, const Vector2& screenSize, bool focused) noexcept;
        /** @brief Sets the frame's Game render-target resolution at the main-thread boundary. */
        void setRenderSize(const Vector2& size) noexcept;
        /** @brief Handles release-inside input using the last rendered layout; no events when gameplay is frozen. */
        void processInput(bool gameplayRunning);
        /** @brief Captures current active/persistent Canvas hierarchies at the given render resolution. */
        void capture(const Scene* active, const Scene& persistent);
        /** @brief Returns the immutable current drawing descriptions. */
        std::span<const UIDraw> draws() const noexcept { return { m_draws.data(), m_drawCount }; }
        /** @brief Clears transient snapshots and pointer capture, retaining event registration. */
        void clear() noexcept;
        /** @brief Releases process-wide callbacks and transient data at shutdown. */
        void shutdown() noexcept;
    private:
        struct CanvasEntry { const GameObject* object; int order; };
        std::vector<CanvasEntry> m_canvases;
        std::vector<UIDraw> m_draws;
        std::size_t m_drawCount = 0;
        std::unordered_map<std::string, ClickHandler> m_events;
        Vector2 m_origin = Vector2::Zero;
        Vector2 m_surfaceSize = Vector2::One;
        Vector2 m_renderSize = Vector2(1280, 720);
        bool m_focused = false;
        ObjectGUID m_hovered;
        ObjectGUID m_pressed;
        UIDraw& append();
        void visit(const GameObject& object, const UIRect& parent, const UIRect& clip,
            const Vector2& origin, float scale, std::size_t depth);
    };
}
