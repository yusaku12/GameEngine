#include "Pch.h"
#include "Components\UI\RuntimeUIComponents.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Generated\FlatBuffers\RuntimeUI_generated.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const auto canvasType = ComponentRegistry::instance().registerType<CanvasComponent>("Canvas", false, false, false);
        const auto rectType = ComponentRegistry::instance().registerType<RectTransformComponent>("Rect Transform", false, false, false);
        std::string_view requireRect(const GameObject& object)
        {
            return object.getComponent<RectTransformComponent>() ? std::string_view{}
                : "Add Rect Transform before a UI visual or button.";
        }
        const auto imageType = ComponentRegistry::instance().registerType<UIImageComponent>("UI Image", false, false, false, requireRect);
        const auto textType = ComponentRegistry::instance().registerType<UITextComponent>("UI Text", false, false, false, requireRect);
        const auto buttonType = ComponentRegistry::instance().registerType<UIButtonComponent>("UI Button", false, false, false, requireRect);
        bool finite(const Vector2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }
        bool validColor(const Color& c)
        {
            return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && std::isfinite(c.w)
                && c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1 && c.w >= 0 && c.w <= 1;
        }
        bool utf8(const std::string& value)
        {
            return value.size() <= 65535 && value.find('\0') == std::string::npos
                && (value.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    value.data(), static_cast<int>(value.size()), nullptr, 0) > 0);
        }
        Serialization::Vec2 pack(const Vector2& v) { return { v.x, v.y }; }
        Serialization::Vec4 pack(const Color& c) { return { c.x, c.y, c.z, c.w }; }
        Vector2 unpack(const Serialization::Vec2& v) { return { v.x(), v.y() }; }
        Color unpack(const Serialization::Vec4& c) { return { c.x(), c.y(), c.z(), c.w() }; }
        void finish(flatbuffers::FlatBufferBuilder& builder, flatbuffers::Offset<Serialization::RuntimeUIPayload> root,
            std::vector<std::uint8_t>& payload)
        {
            Serialization::FinishRuntimeUIPayloadBuffer(builder, root);
            payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        }
        const Serialization::RuntimeUIPayload* read(const std::uint32_t version, std::span<const std::uint8_t> payload)
        {
            flatbuffers::Verifier verifier(payload.data(), payload.size());
            if (version != 1 || payload.size() < 8 || !Serialization::VerifyRuntimeUIPayloadBuffer(verifier))
            {
                LOG_ERROR("[UI] Invalid component payload.");
                return nullptr;
            }
            return Serialization::GetRuntimeUIPayload(payload.data());
        }
        bool invalid() { LOG_ERROR("[UI] Invalid or mismatched component configuration."); return false; }
        bool editString(const char* label, std::string& value)
        {
            std::array<char, 4096> buffer{};
            std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
            if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
            value = buffer.data();
            return true;
        }
    }
    bool CanvasComponent::setSettings(const CanvasSettings& value) noexcept
    {
        if (!finite(value.referenceSize) || value.referenceSize.x < 1 || value.referenceSize.y < 1
            || value.referenceSize.x > 16384 || value.referenceSize.y > 16384) return invalid();
        m_settings = value; return true;
    }
    bool RectTransformComponent::setSettings(const RectSettings& value) noexcept
    {
        if (!finite(value.anchorMin) || !finite(value.anchorMax) || !finite(value.offsetMin) || !finite(value.offsetMax)
            || std::abs(value.offsetMin.x) > 1000000 || std::abs(value.offsetMin.y) > 1000000
            || std::abs(value.offsetMax.x) > 1000000 || std::abs(value.offsetMax.y) > 1000000
            || value.anchorMin.x < 0 || value.anchorMin.y < 0 || value.anchorMax.x > 1 || value.anchorMax.y > 1
            || value.anchorMin.x > value.anchorMax.x || value.anchorMin.y > value.anchorMax.y) return invalid();
        m_settings = value; return true;
    }
    bool UIImageComponent::setSettings(const UIImageSettings& value)
    {
        const auto& path = value.path.native();
        if (!validColor(value.color) || path.size() > 32767 || path.find(L'\0') != std::wstring::npos
            || (!path.empty() && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr) == 0)) return invalid();
        m_settings = value; return true;
    }
    bool UITextComponent::setSettings(const UITextSettings& value)
    {
        if (!utf8(value.text) || !utf8(value.font) || value.font.empty() || !std::isfinite(value.size)
            || value.size < 1 || value.size > 512 || value.alignment > 2 || !validColor(value.color)) return invalid();
        m_settings = value; return true;
    }
    bool UIButtonComponent::setSettings(const UIButtonSettings& value)
    {
        if (!utf8(value.event) || !validColor(value.normal) || !validColor(value.hovered) || !validColor(value.pressed))
            return invalid();
        m_settings = value; return true;
    }
    bool CanvasComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto size = pack(m_settings.referenceSize);
        const auto value = Serialization::CreateCanvasSettings(builder, &size, m_settings.sortOrder);
        finish(builder, Serialization::CreateRuntimeUIPayload(builder, value), payload); return true;
    }
    bool RectTransformComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto amin = pack(m_settings.anchorMin), amax = pack(m_settings.anchorMax);
        const auto omin = pack(m_settings.offsetMin), omax = pack(m_settings.offsetMax);
        const auto value = Serialization::CreateRectSettings(builder, &amin, &amax, &omin, &omax);
        finish(builder, Serialization::CreateRuntimeUIPayload(builder, {}, value), payload); return true;
    }
    bool UIImageComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto color = pack(m_settings.color);
        const auto path = String::fromWide(m_settings.path.c_str());
        const auto value = Serialization::CreateImageSettings(builder, builder.CreateString(path.view()), &color);
        finish(builder, Serialization::CreateRuntimeUIPayload(builder, {}, {}, value), payload); return true;
    }
    bool UITextComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto color = pack(m_settings.color);
        const auto text = builder.CreateString(m_settings.text);
        const auto font = builder.CreateString(m_settings.font);
        const auto value = Serialization::CreateTextSettings(builder, text, font, m_settings.size, &color, m_settings.alignment);
        finish(builder, Serialization::CreateRuntimeUIPayload(builder, {}, {}, {}, value), payload); return true;
    }
    bool UIButtonComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto normal = pack(m_settings.normal), hovered = pack(m_settings.hovered), pressed = pack(m_settings.pressed);
        const auto value = Serialization::CreateButtonSettings(builder, m_settings.interactable,
            builder.CreateString(m_settings.event), &normal, &hovered, &pressed);
        finish(builder, Serialization::CreateRuntimeUIPayload(builder, {}, {}, {}, {}, value), payload); return true;
    }
    bool CanvasComponent::deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload)
    {
        const auto* root = read(version, payload);
        if (!root) return false;
        const auto* value = root->canvas();
        if (!value || !value->reference_size()) return invalid();
        return setSettings({ unpack(*value->reference_size()), value->sort_order() });
    }
    bool RectTransformComponent::deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload)
    {
        const auto* root = read(version, payload);
        if (!root) return false;
        const auto* value = root->rect();
        if (!value || !value->anchor_min() || !value->anchor_max() || !value->offset_min() || !value->offset_max()) return invalid();
        return setSettings({ unpack(*value->anchor_min()), unpack(*value->anchor_max()),
            unpack(*value->offset_min()), unpack(*value->offset_max()) });
    }
    bool UIImageComponent::deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload)
    {
        const auto* root = read(version, payload);
        if (!root) return false;
        const auto* value = root->image();
        if (!value || !value->color()) return invalid();
        if (value->path() && !utf8(value->path()->str())) return invalid();
        return setSettings({ value->path() ? std::filesystem::path(String(value->path()->string_view()).toWide())
            : std::filesystem::path{}, unpack(*value->color()) });
    }
    bool UITextComponent::deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload)
    {
        const auto* root = read(version, payload);
        if (!root) return false;
        const auto* value = root->text();
        if (!value || !value->text() || !value->font() || !value->color()) return invalid();
        return setSettings({ value->text()->str(), value->font()->str(), value->size(), unpack(*value->color()), value->alignment() });
    }
    bool UIButtonComponent::deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload)
    {
        const auto* root = read(version, payload);
        if (!root) return false;
        const auto* value = root->button();
        if (!value || !value->normal() || !value->hovered() || !value->pressed()) return invalid();
        return setSettings({ value->interactable(), value->event() ? value->event()->str() : std::string{},
            unpack(*value->normal()), unpack(*value->hovered()), unpack(*value->pressed()) });
    }
    void CanvasComponent::onImGui()
    {
        auto value = m_settings;
        bool changed = ImGui::DragFloat2("Reference Size", &value.referenceSize.x, 1, 1, 16384);
        changed |= ImGui::InputInt("Sort Order", &value.sortOrder);
        if (changed) setSettings(value);
    }
    void RectTransformComponent::onImGui()
    {
        auto value = m_settings;
        bool changed = ImGui::DragFloat2("Anchor Min", &value.anchorMin.x, 0.01f, 0, 1);
        changed |= ImGui::DragFloat2("Anchor Max", &value.anchorMax.x, 0.01f, 0, 1);
        changed |= ImGui::DragFloat2("Offset Min", &value.offsetMin.x);
        changed |= ImGui::DragFloat2("Offset Max", &value.offsetMax.x);
        if (changed) setSettings(value);
    }
    void UIImageComponent::onImGui()
    {
        auto value = m_settings;
        std::string path(String::fromWide(value.path.c_str()).view());
        bool changed = editString("Image Path", path);
        if (changed) value.path = String(path).toWide();
        const GameObject* const object = getGameObject();
        ImGui::BeginDisabled(object == nullptr);
        if (ImGui::Button("Browse...") && object != nullptr)
        {
            const ObjectGUID objectGuid = object->getGUID();
            const std::filesystem::path initialPath = value.path.empty() ? std::filesystem::path(L"Assets") : value.path;
            const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([objectGuid, initialPath, ownerWindow]
                {
                    static constexpr std::array filters = {
                        FileDialogFilter{ L"Image Files", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.wdp;*.jxr" },
                        FileDialogFilter{ L"All Files", L"*.*" },
                    };
                    std::vector<std::filesystem::path> paths;
                    const DialogResult result = Dialog::openFile(
                        paths, L"Select UI Image", initialPath, filters, false, ownerWindow);
                    if (result == DialogResult::Cancel)
                        return;
                    if (result != DialogResult::Ok || paths.size() != 1)
                    {
                        LOG_ERROR("[UI] Image file selection failed.");
                        return;
                    }

                    SceneManager& scenes = SceneManager::instance();
                    GameObject* target = nullptr;
                    if (Scene* scene = scenes.getActiveScene())
                        target = scene->find(objectGuid);
                    if (target == nullptr)
                        target = scenes.getPersistentScene()->find(objectGuid);
                    UIImageComponent* component = target != nullptr ? target->getComponent<UIImageComponent>() : nullptr;
                    if (component == nullptr)
                    {
                        LOG_WARNING("[UI] Image selection discarded because the target component no longer exists.");
                        return;
                    }
                    auto settings = component->settings();
                    settings.path = paths.front().lexically_normal();
                    component->setSettings(settings);
                });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(value.path.empty());
        if (ImGui::Button("Clear"))
        {
            value.path.clear();
            changed = true;
        }
        ImGui::EndDisabled();
        changed |= ImGui::ColorEdit4("Tint", &value.color.x);
        if (changed) setSettings(value);
    }
    void UITextComponent::onImGui()
    {
        auto value = m_settings;
        bool changed = editString("Text UTF-8", value.text);
        changed |= editString("Font Family", value.font);
        changed |= ImGui::DragFloat("Font Size", &value.size, 0.5f, 1, 512);
        changed |= ImGui::ColorEdit4("Text Color", &value.color.x);
        int alignment = value.alignment;
        changed |= ImGui::Combo("Alignment", &alignment, "Left\0Center\0Right\0");
        value.alignment = static_cast<std::uint8_t>(alignment);
        if (changed) setSettings(value);
    }
    void UIButtonComponent::onImGui()
    {
        auto value = m_settings;
        bool changed = ImGui::Checkbox("Interactable", &value.interactable);
        changed |= editString("Click Event", value.event);
        changed |= ImGui::ColorEdit4("Normal", &value.normal.x);
        changed |= ImGui::ColorEdit4("Hovered", &value.hovered.x);
        changed |= ImGui::ColorEdit4("Pressed", &value.pressed.x);
        if (changed) setSettings(value);
    }
}
