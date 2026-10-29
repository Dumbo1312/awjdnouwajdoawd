#include "imgui.h"

namespace c
{
	inline ImVec4 accent = ImColor(191, 0, 255);
	inline ImVec4 separator = ImColor(30, 20, 40, 150); 

	namespace bg
	{
		inline ImVec4 background = ImColor(10, 10, 10, 180);
		inline ImVec2 size = ImVec2(1050, 570);
		inline float rounding = 12.f;
	}

	namespace child
	{
		inline ImVec4 background = ImColor(15, 10, 20, 200);
		inline ImVec4 cap = ImColor(25, 15, 35, 220);
		inline float rounding = 12.f;
	}

	namespace page
	{
		inline ImVec4 background_active = ImColor(40, 20, 60, 200);
		inline ImVec4 background = ImColor(20, 15, 25, 150);

		inline ImVec4 text_hov = ImColor(200, 150, 255);
		inline ImVec4 text = ImColor(160, 120, 200);

		inline float rounding = 8.f;
	}

	namespace elements
	{
		inline ImVec4 background_hovered = ImColor(50, 25, 80, 220);
		inline ImVec4 background = ImColor(25, 15, 35, 180);
		inline float rounding = 6.f;
	}

	namespace checkbox
	{
		inline ImVec4 mark = ImColor(191, 0, 255, 255);
	}

	namespace text
	{
		inline ImVec4 text_active = ImColor(255, 255, 255);
		inline ImVec4 text_hov = ImColor(220, 180, 255);
		inline ImVec4 text = ImColor(180, 150, 220);
	}
}
