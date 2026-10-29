#define IMGUI_DEFINE_MATH_OPERATORS
#pragma once
#include <string>
#include <wtypes.h>
#include <vector>
#include <cstdlib>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_settings.h"
#include "imstb_textedit.h"	



using namespace ImGui;

namespace font
{
	extern ImFont* Icons;
}

namespace custom 
{
	void				Text(const char* fmt, ...);
	void				TextV(const char* fmt, va_list args);

	bool				Page(bool selected, const char* icon, const ImVec2& size);

	bool				Button(const char* label, const ImVec2& size_arg);

	bool				ChildEx(const char* name, ImGuiID id, const ImVec2& size_arg, bool cap = false, ImGuiWindowFlags flags = 0);
	bool				Child(const char* str_id, const ImVec2& size = ImVec2(0, 0), bool cap = false, ImGuiWindowFlags flags = 0);
	bool				ChildID(ImGuiID id, const ImVec2& size = ImVec2(0, 0), bool cap = false, ImGuiWindowFlags flags = 0);
	void				EndChild();

	void				BeginGroup();
	void				EndGroup();

	bool				Checkbox(const char* label, bool* v);

	bool			    Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
	bool				Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));

	bool				BeginCombo(const char* label, const char* preview_value, int val = 0, bool multi = false, ImGuiComboFlags flags = 0);
	void				EndCombo();
	void				MultiCombo(const char* label, bool variable[], const char* labels[], int count);
	bool				Combo(const char* label, int* current_item, const std::vector<std::string>& items, int items_count, int popup_max_height_in_items = -1);
	bool				Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);
	bool				Combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items = -1);

	bool				KnobScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const void* step, const char* format, ImGuiSliderFlags flags = 0);
	bool				KnobFloat(const char* label, float* v, float v_min, float v_max, float step, const char* format, ImGuiSliderFlags flags = 0);
	bool				KnobInt(const char* label, int* v, int v_min, int v_max, int step, const char* format, ImGuiSliderFlags flags = 0);

	bool				SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const void* step, const char* format, ImGuiSliderFlags flags = 0);
	bool				SliderFloat(const char* label, float* v, float v_min, float v_max, float step, const char* format, ImGuiSliderFlags flags = 0);
	bool				SliderInt(const char* label, int* v, int v_min, int v_max, int step, const char* format, ImGuiSliderFlags flags = 0);

	bool				Keybind(const char* label, int* key);

	void				Separator_line();

	void				SeparatorEx(ImGuiSeparatorFlags flags, float thickness);
	void				Separator();

	// https://github.com/ocornut/imgui/issues/1901#issue-335266223
	void                Spinner(const char* label, float radius, int thickness, const ImU32& color);
}