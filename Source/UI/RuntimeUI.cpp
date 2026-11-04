#include "Pch.h"
#include "UI\RuntimeUI.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Input\InputManager.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    RuntimeUI& RuntimeUI::instance() noexcept { static RuntimeUI ui; return ui; }
    bool RuntimeUI::registerClickEvent(const std::string& name, ClickHandler handler)
    {
        if (name.empty() || !handler || m_events.contains(name))
        {
            LOG_ERROR("[UI] Click event must have a unique nonempty name and a handler.");
            return false;
        }
        m_events.emplace(name, std::move(handler));
        return true;
    }
    void RuntimeUI::unregisterClickEvent(const std::string& name) { m_events.erase(name); }
    void RuntimeUI::setRenderSize(const Vector2& size) noexcept
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y)
            || size.x < 1 || size.y < 1 || size.x > 16384 || size.y > 16384)
        {
            LOG_ERROR("[UI] Invalid render resolution.");
            return;
        }
        m_renderSize = size;
    }
    void RuntimeUI::setSurface(const Vector2& origin, const Vector2& size, const bool focused) noexcept
    {
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(size.x)
            || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0)
        {
            LOG_ERROR("[UI] Invalid Game view input surface.");
            m_focused = false;
            return;
        }
        m_origin = origin;
        m_surfaceSize = size;
        m_focused = focused;
    }
    void RuntimeUI::clear() noexcept
    {
        m_drawCount = 0;
        m_hovered = {};
        m_pressed = {};
        m_canvases.clear();
    }
    void RuntimeUI::shutdown() noexcept
    {
        clear();
        m_draws.clear();
        m_events.clear();
    }
    void RuntimeUI::processInput(const bool running)
    {
        m_hovered = {};
        if (!running || !m_focused)
        {
            m_pressed = {};
            return;
        }
        const auto& input = InputManager::instance();
        const POINT cursor = input.getMousePosition();
        const Vector2 point((static_cast<float>(cursor.x) - m_origin.x) * m_renderSize.x / m_surfaceSize.x,
            (static_cast<float>(cursor.y) - m_origin.y) * m_renderSize.y / m_surfaceSize.y);
        for (std::size_t index = m_drawCount; index > 0; --index)
        {
            const auto& draw = m_draws[index - 1];
            if (draw.kind == UIDraw::Kind::Button
                && draw.rect.contains(point) && draw.clip.contains(point))
            {
                if (draw.interactable)
                    m_hovered = draw.object;
                break;
            }
        }
        if (input.isMousePressed(0))
            m_pressed = m_hovered;
        if (!input.isMouseReleased(0))
            return;
        const ObjectGUID pressed = std::exchange(m_pressed, ObjectGUID{});
        if (!pressed.isValid() || pressed != m_hovered)
            return;
        SceneManager& manager = SceneManager::instance();
        GameObject* object = manager.getActiveScene() ? manager.getActiveScene()->find(pressed) : nullptr;
        if (!object) object = manager.getPersistentScene()->find(pressed);
        if (!object || !object->isActiveInHierarchy()) return;
        const auto* rect = object->getComponent<RectTransformComponent>();
        if (!rect || !rect->isEnabled()) return;
        const GameObject* canvasObject = object;
        while (canvasObject && !canvasObject->getComponent<CanvasComponent>())
            canvasObject = canvasObject->getParent();
        if (!canvasObject || !canvasObject->getComponent<CanvasComponent>()->isEnabled()) return;
        const auto* button = object->getComponent<UIButtonComponent>();
        if (!button || !button->isEnabled() || !button->settings().interactable || button->settings().event.empty()) return;
        const auto found = m_events.find(button->settings().event);
        if (found == m_events.end())
        {
            LOG_ERROR("[UI] Click event '{}' has no registered handler.", button->settings().event);
            return;
        }
        ClickHandler handler = found->second;
        try { handler(*object); }
        catch (const std::exception& error) { LOG_ERROR("[UI] Click handler failed: {}", error.what()); }
    }
    UIDraw& RuntimeUI::append()
    {
        if (m_drawCount == m_draws.size())
            m_draws.emplace_back();
        return m_draws[m_drawCount++];
    }
    void RuntimeUI::visit(const GameObject& object, const UIRect& parent, const UIRect& clip,
        const Vector2& origin, const float scale, const std::size_t depth)
    {
        if (!object.isActiveInHierarchy()) return;
        if (depth > 128)
        {
            LOG_ERROR("[UI] Canvas hierarchy exceeds 128 layout levels.");
            return;
        }
        UIRect rect = parent;
        const auto* transform = object.getComponent<RectTransformComponent>();
        if (transform && transform->isEnabled())
        {
            const auto& settings = transform->settings();
            const Vector2 size = parent.max - parent.min;
            rect.min = parent.min + size * settings.anchorMin + settings.offsetMin;
            rect.max = parent.min + size * settings.anchorMax + settings.offsetMax;
        }
        const UIRect pixels{ origin + rect.min * scale, origin + rect.max * scale };
        if (pixels.max.x > pixels.min.x && pixels.max.y > pixels.min.y && transform && transform->isEnabled())
        {
            const auto* button = object.getComponent<UIButtonComponent>();
            const auto* image = object.getComponent<UIImageComponent>();
            const bool hasButton = button && button->isEnabled();
            const bool hasImage = image && image->isEnabled();
            if (hasButton)
            {
                auto& draw = append();
                draw.kind = UIDraw::Kind::Button;
                draw.object = object.getGUID();
                draw.rect = pixels;
                draw.clip = clip;
                draw.interactable = button->settings().interactable;
                draw.color = m_pressed == object.getGUID() && m_hovered == object.getGUID() ? button->settings().pressed
                    : (m_hovered == object.getGUID() ? button->settings().hovered : button->settings().normal);
                if (!draw.interactable) draw.color.w *= 0.5f;
                if (hasImage)
                {
                    const auto& tint = image->settings().color;
                    draw.color = Color(draw.color.x * tint.x, draw.color.y * tint.y,
                        draw.color.z * tint.z, draw.color.w * tint.w);
                    draw.image = image->settings().path;
                }
                else draw.image.clear();
                draw.text.clear(); draw.font.clear();
            }
            if (hasImage && !hasButton)
            {
                auto& draw = append();
                draw.kind = UIDraw::Kind::Image; draw.object = object.getGUID(); draw.rect = pixels; draw.clip = clip;
                draw.color = image->settings().color; draw.image = image->settings().path;
                draw.text.clear(); draw.font.clear(); draw.interactable = false;
            }
            if (const auto* text = object.getComponent<UITextComponent>(); text && text->isEnabled())
            {
                auto& draw = append();
                draw.kind = UIDraw::Kind::Text; draw.object = object.getGUID(); draw.rect = pixels; draw.clip = clip;
                draw.color = text->settings().color; draw.text = text->settings().text; draw.font = text->settings().font;
                draw.fontSize = text->settings().size * scale; draw.alignment = text->settings().alignment;
                draw.image.clear(); draw.interactable = false;
            }
        }
        for (std::size_t index = 0; index < object.getChildCount(); ++index)
        {
            const auto& child = *object.getChild(index);
            if (child.getComponent<CanvasComponent>()) continue;
            visit(child, rect, clip, origin, scale, depth + 1);
        }
    }
    void RuntimeUI::capture(const Scene* active, const Scene& persistent)
    {
        const Vector2 renderSize = m_renderSize;
        m_drawCount = 0;
        m_canvases.clear();
        if (renderSize.x <= 0 || renderSize.y <= 0) return;
        m_renderSize = renderSize;
        const auto collect = [&](const Scene& scene)
        {
            for (const auto& object : scene.getGameObjects())
                if (const auto* canvas = object->getComponent<CanvasComponent>();
                    canvas && canvas->isEnabled() && object->isActiveInHierarchy())
                    m_canvases.push_back({ object.get(), canvas->settings().sortOrder });
        };
        if (active) collect(*active);
        collect(persistent);
        std::stable_sort(m_canvases.begin(), m_canvases.end(),
            [](const CanvasEntry& left, const CanvasEntry& right) { return left.order < right.order; });
        for (const auto& entry : m_canvases)
        {
            const auto& canvas = entry.object->getComponent<CanvasComponent>()->settings();
            const float scale = std::min(renderSize.x / canvas.referenceSize.x, renderSize.y / canvas.referenceSize.y);
            const Vector2 origin = (renderSize - canvas.referenceSize * scale) * 0.5f;
            const UIRect clip{ origin, origin + canvas.referenceSize * scale };
            visit(*entry.object, { Vector2::Zero, canvas.referenceSize }, clip, origin, scale, 0);
        }
        m_canvases.clear();
    }
}
