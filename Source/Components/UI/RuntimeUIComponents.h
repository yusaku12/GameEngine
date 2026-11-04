#pragma once

#include <span>

#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Screen-space Canvas sizing and stable draw order. */
    struct CanvasSettings { Vector2 referenceSize = Vector2(1280, 720); int sortOrder = 0; };
    /** @brief Rect edges are interpolated anchors plus offsets, in parent Canvas units. */
    struct RectSettings
    {
        Vector2 anchorMin = Vector2(0.5f, 0.5f);
        Vector2 anchorMax = Vector2(0.5f, 0.5f);
        Vector2 offsetMin = Vector2(-100, -30);
        Vector2 offsetMax = Vector2(100, 30);
    };
    /** @brief Image path and RGBA modulation; empty path draws a solid rectangle. */
    struct UIImageSettings { std::filesystem::path path; Color color = Color(1, 1, 1, 1); };
    /** @brief UTF-8 text, installed font family, font size and horizontal alignment. */
    struct UITextSettings
    {
        std::string text = "Text";
        std::string font = "Segoe UI";
        float size = 24;
        Color color = Color(1, 1, 1, 1);
        std::uint8_t alignment = 0;
    };
    /** @brief Button appearance and a named GameUpdate event. */
    struct UIButtonSettings
    {
        bool interactable = true;
        std::string event;
        Color normal = Color(0.15f, 0.2f, 0.3f, 1);
        Color hovered = Color(0.25f, 0.35f, 0.5f, 1);
        Color pressed = Color(0.1f, 0.15f, 0.25f, 1);
    };

    /**
     * @brief Root of a screen-space UI hierarchy, scaled uniformly with letterboxing.
     * @thread_safety Configuration access follows the joined frame barrier.
     */
    class CanvasComponent final : public Component
    {
    public:
        /** @brief Returns Canvas configuration. */
        const CanvasSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and assigns the reference resolution and draw order. */
        bool setSettings(const CanvasSettings& value) noexcept;
        /** @brief Returns the versioned Canvas configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves reference resolution and draw order. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores Canvas configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        CanvasSettings m_settings;
    };
    /**
     * @brief Anchor-based layout relative to a parent RectTransform or Canvas.
     * @thread_safety Configuration access follows the joined frame barrier.
     */
    class RectTransformComponent final : public Component
    {
    public:
        /** @brief Returns anchors and offsets. */
        const RectSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates finite ordered anchors and assigns offsets. */
        bool setSettings(const RectSettings& value) noexcept;
        /** @brief Returns the versioned rectangle configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves parent-relative anchors and offsets. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores rectangle configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        RectSettings m_settings;
    };
    /**
     * @brief Screen-space image rendered in the same object's RectTransform.
     * @thread_safety Configuration access follows the joined frame barrier.
     */
    class UIImageComponent final : public Component
    {
    public:
        /** @brief Returns image configuration. */
        const UIImageSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates color and assigns the image path. */
        bool setSettings(const UIImageSettings& value);
        /** @brief Returns the versioned image configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves a UTF-8 image path and its tint. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores image configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        UIImageSettings m_settings;
    };
    /**
     * @brief Unicode text rendered by DirectWrite, with wrapping and clipping.
     * @thread_safety Configuration access follows the joined frame barrier.
     */
    class UITextComponent final : public Component
    {
    public:
        /** @brief Returns text configuration. */
        const UITextSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates UTF-8, font size, alignment and color before assignment. */
        bool setSettings(const UITextSettings& value);
        /** @brief Returns the versioned text configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves Unicode text, font and appearance. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores text configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        UITextSettings m_settings;
    };
    /**
     * @brief Release-inside button with hover/pressed states and named click events.
     * @thread_safety Click handlers run on GameUpdate; configuration follows the frame barrier.
     */
    class UIButtonComponent final : public Component
    {
    public:
        /** @brief Returns interaction and appearance configuration. */
        const UIButtonSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates colors and event-name UTF-8 before assignment. */
        bool setSettings(const UIButtonSettings& value);
        /** @brief Returns the versioned button configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Saves interaction settings and the named event, never callbacks. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores button configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    protected:
        void onImGui() override;
    private:
        UIButtonSettings m_settings;
    };
}
