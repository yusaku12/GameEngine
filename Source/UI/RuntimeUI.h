#pragma once

#include "Components\UI\RuntimeUIComponents.h"

namespace Engine
{
	class Scene;
	class GameObject;

	/*
	* @brief UIの矩形を表す構造体
	*/
	struct UIRect
	{
		Vector2 min = Vector2::Zero; //!< 左下の座標
		Vector2 max = Vector2::Zero; //!< 右上の座標

		/*
		* @brief 指定した点が矩形内に含まれるかどうかを判定する
		* @param point 判定する点
		*/
		bool contains(const Vector2& point) const noexcept
		{
			return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
		}
	};

	/*
	* @brief 描画情報を表す構造体
	*/
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

	class RuntimeUI final
	{
	public:

		using ClickHandler = std::function<void(GameObject&)>;

		static RuntimeUI& instance() noexcept;

		bool registerClickEvent(const std::string& name, ClickHandler handler);

		void unregisterClickEvent(const std::string& name);

		void setSurface(const Vector2& screenOrigin, const Vector2& screenSize, bool focused) noexcept;

		void setRenderSize(const Vector2& size) noexcept;

		void processInput(bool gameplayRunning);

		void capture(const Scene* active, const Scene& persistent);

		std::span<const UIDraw> draws() const noexcept { return { m_draws.data(), m_drawCount }; }

		void clear() noexcept;

		void shutdown() noexcept;

	private:

		void visit(const GameObject& object, const UIRect& parent, const UIRect& clip,
			const Vector2& origin, float scale, std::size_t depth);

		UIDraw& append();

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
	};
}
