#include "custom_widgets.hpp"
#include "VKNames.h"
#include <cmath>

namespace custom
{
    struct key_state
    {
        ImVec4 background, text;
        bool active = false;
        bool hovered = false;
        float alpha = 0.f;
    };

    bool Keybind(const char* label, int* key)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        ImGuiIO& io = g.IO;
        const ImGuiStyle& style = g.Style;

        const ImGuiID id = window->GetID(label);
        const float width = (GetContentRegionMax().x - style.WindowPadding.x);

        const ImRect rect(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width, 32));

        ItemSize(ImRect(rect.Min, rect.Max));
        if (!ImGui::ItemAdd(rect, id)) return false;

        char buf_display[64] = "None";

        bool value_changed = false;

        std::string active_key = VKNames.at(*key);

        if (*key != 0 && g.ActiveId != id)
            strcpy_s(buf_display, active_key.c_str());
        else if (g.ActiveId == id)
            strcpy_s(buf_display, "...");

        const ImVec2 label_size = CalcTextSize(buf_display, NULL, true);

        ImRect clickable(ImVec2(rect.Max.x - 20 - label_size.x, rect.Min.y), rect.Max);
        bool hovered = ItemHoverable(clickable, id, NULL);

        static std::map<ImGuiID, key_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, key_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.text = ImLerp(it_anim->second.text, g.ActiveId == id ? c::text::text_active : hovered ? c::text::text_hov : c::text::text, ImGui::GetIO().DeltaTime * 6.f);

        window->DrawList->AddRectFilled(clickable.Min, clickable.Max, GetColorU32(c::elements::background), c::elements::rounding);

        window->DrawList->AddText(rect.Min + ImVec2(0, (32 - CalcTextSize(label).y) / 2), GetColorU32(it_anim->second.text), label);

        PushStyleColor(ImGuiCol_Text, c::text::text);
        PushFont(font::Icons);
        RenderTextClipped(clickable.Min - ImVec2(30, 0), clickable.Max, "g", NULL, NULL, ImVec2(0.0f, 0.5f));
        PopFont();
        PopStyleColor();

        PushStyleColor(ImGuiCol_Text, c::text::text_active);
        RenderTextClipped(clickable.Min, clickable.Max, buf_display, NULL, &label_size, ImVec2(0.5f, 0.5f));
        PopStyleColor();

        if (hovered && io.MouseClicked[0])
        {
            if (g.ActiveId != id)
            {
                memset(io.MouseDown, 0, sizeof(io.MouseDown));
                memset(io.KeysDown, 0, sizeof(io.KeysDown));
                value_changed = true;
                *key = 0x00;
            }
            ImGui::SetActiveID(id, window);
            ImGui::FocusWindow(window);
        }
        else if (io.MouseClicked[0])
        {
            if (g.ActiveId == id)
                ImGui::ClearActiveID();
        }

        if (g.ActiveId == id)
        {
            for (auto i = 0; i < 5; i++)
            {
                if (io.MouseDown[i])
                {
                    switch (i)
                    {
                    case 0: *key = 0x01; break;
                    case 1: *key = 0x02; break;
                    case 2: *key = 0x04; break;
                    case 3: *key = 0x05; break;
                    case 4: *key = 0x06; break;
                    }
                    value_changed = true;
                    ImGui::ClearActiveID();
                    break;
                }
            }

            for (auto i = 0x07; i <= 0xFF; i++)
            {
                if (io.KeysDown[i])
                {
                    *key = i;
                    value_changed = true;
                    ImGui::ClearActiveID();
                }
            }
        }

        return value_changed;
    }

    // Animation state for Child caption line
    struct child_anim_state
    {
        float line_alpha = 0.f;
    };
    static std::map<ImGuiID, child_anim_state> child_anim;

    // Ease in-out function
    static float EaseInOut(float t)
    {
        return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    }

    bool ChildEx(const char* name, ImGuiID id, const ImVec2& size_arg, bool cap, ImGuiWindowFlags flags)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* parent_window = g.CurrentWindow;

        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ChildWindow;
        flags |= (parent_window->Flags & ImGuiWindowFlags_NoMove);

        const ImVec2 content_avail = GetContentRegionAvail();
        ImVec2 size = ImTrunc(size_arg);
        const int auto_fit_axises = ((size.x == 0.0f) ? (1 << ImGuiAxis_X) : 0x00) | ((size.y == 0.0f) ? (1 << ImGuiAxis_Y) : 0x00);
        if (size.x <= 0.0f) size.x = ImMax(content_avail.x + size.x, 4.0f);
        if (size.y <= 0.0f) size.y = ImMax(content_avail.y + size.y, 4.0f);

        SetNextWindowPos(ImVec2(parent_window->DC.CursorPos + ImVec2(0.0f, cap ? 45.0f : 0.0f)));
        SetNextWindowSize(size - ImVec2(0.0f, cap ? 45.0f : 0.0f));

        GetWindowDrawList()->AddRectFilled(parent_window->DC.CursorPos + ImVec2(0.0f, cap ? 45.0f : 0.0f), parent_window->DC.CursorPos + size_arg, GetColorU32(c::child::background), c::child::rounding, cap ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll);

        if (cap) {
            // Check hover state for the ENTIRE child area (not just caption)
            ImRect full_rect(parent_window->DC.CursorPos, parent_window->DC.CursorPos + size_arg);
            bool is_hovered = full_rect.Contains(g.IO.MousePos);

            // Get or create animation state
            auto& anim = child_anim[id];
            
            // Animate alpha with easing
            float target_alpha = is_hovered ? 1.0f : 0.0f;
            float speed = 4.0f * g.IO.DeltaTime;
            
            if (anim.line_alpha < target_alpha)
                anim.line_alpha = ImMin(anim.line_alpha + speed, target_alpha);
            else if (anim.line_alpha > target_alpha)
                anim.line_alpha = ImMax(anim.line_alpha - speed, target_alpha);
            
            // Apply easing to the alpha
            float eased_alpha = EaseInOut(anim.line_alpha);

            // Draw the accent line with animated alpha
            GetWindowDrawList()->AddRectFilledMultiColor(parent_window->DC.CursorPos + ImVec2((size_arg.x / 2), 44), parent_window->DC.CursorPos + ImVec2((size_arg.x - 50), 45), GetColorU32(c::accent, eased_alpha), GetColorU32(c::accent, 0.f), GetColorU32(c::accent, 0.f), GetColorU32(c::accent, eased_alpha), c::child::rounding);
            GetWindowDrawList()->AddRectFilledMultiColor(parent_window->DC.CursorPos + ImVec2(50, 44), parent_window->DC.CursorPos + ImVec2(size_arg.x / 2, 45), GetColorU32(c::accent, 0.f), GetColorU32(c::accent, eased_alpha), GetColorU32(c::accent, eased_alpha), GetColorU32(c::accent, 0.f), c::child::rounding);

            GetWindowDrawList()->AddRectFilled(parent_window->DC.CursorPos, parent_window->DC.CursorPos + ImVec2(size_arg.x, 35), GetColorU32(c::child::cap), c::child::rounding, ImDrawFlags_RoundCornersTop);
            GetWindowDrawList()->AddText(parent_window->DC.CursorPos + ImVec2(size_arg.x - CalcTextSize(name).x, 35 - CalcTextSize(name).y) / 2, GetColorU32(c::text::text_active), name);
        }

        const char* temp_window_name;

        if (name) ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%s_%08X", parent_window->Name, name, id);

        else ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%08X", parent_window->Name, id);

        const float backup_border_size = g.Style.ChildBorderSize;

        bool ret = Begin(temp_window_name, NULL, flags | ImGuiWindowFlags_NoBackground);

        ImGuiWindow* child_window = g.CurrentWindow;
        child_window->ChildId = id;
        child_window->AutoFitChildAxises = (ImS8)auto_fit_axises;

        if (child_window->BeginCount == 1) parent_window->DC.CursorPos = child_window->Pos;

        const ImGuiID temp_id_for_activation = ImHashStr("##Child", 0, id);
        if (g.ActiveId == temp_id_for_activation) ClearActiveID();

        if (g.NavActivateId == id && !(flags & ImGuiWindowFlags_NavFlattened) && (child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY))
        {
            FocusWindow(child_window);
            NavInitWindow(child_window, false);
            SetActiveID(temp_id_for_activation, child_window);
            g.ActiveIdSource = g.NavInputSource;
        }
        return ret;
    }

    bool Child(const char* str_id, const ImVec2& size_arg, bool cap, ImGuiWindowFlags extra_flags)
    {
        ImGuiWindow* window = GetCurrentWindow();

        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 13));
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(13, 13));

        return ChildEx(str_id, window->GetID(str_id), size_arg, cap, extra_flags | ImGuiWindowFlags_AlwaysUseWindowPadding);
    }

    bool ChildID(ImGuiID id, const ImVec2& size_arg, bool cap, ImGuiWindowFlags extra_flags)
    {
        IM_ASSERT(id != 0);
        return ChildEx(NULL, id, size_arg, cap, extra_flags);
    }

    void EndChild()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        PopStyleVar(2);

        IM_ASSERT(g.WithinEndChild == false);
        IM_ASSERT(window->Flags & ImGuiWindowFlags_ChildWindow);

        g.WithinEndChild = true;
        if (window->BeginCount > 1)
        {
            End();
        }
        else
        {
            ImVec2 sz = window->Size;

            if (window->AutoFitChildAxises & (1 << ImGuiAxis_X)) sz.x = ImMax(4.0f, sz.x);
            if (window->AutoFitChildAxises & (1 << ImGuiAxis_Y)) sz.y = ImMax(4.0f, sz.y);

            End();

            ImGuiWindow* parent_window = g.CurrentWindow;
            ImRect bb(parent_window->DC.CursorPos, parent_window->DC.CursorPos + sz);
            ItemSize(sz);
            if ((window->DC.NavLayersActiveMask != 0 || window->DC.NavWindowHasScrollY) && !(window->Flags & ImGuiWindowFlags_NavFlattened))
            {
                ItemAdd(bb, window->ChildId);
            }
            else
            {
                ItemAdd(bb, 0);

                if (window->Flags & ImGuiWindowFlags_NavFlattened) parent_window->DC.NavLayersActiveMaskNext |= window->DC.NavLayersActiveMaskNext;
            }
            if (g.HoveredWindow == window) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;
        }
        g.WithinEndChild = false;
        g.LogLinePosY = -FLT_MAX;
    }


    void BeginGroup()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;

        g.GroupStack.resize(g.GroupStack.Size + 1);
        ImGuiGroupData& group_data = g.GroupStack.back();
        group_data.WindowID = window->ID;
        group_data.BackupCursorPos = window->DC.CursorPos;
        group_data.BackupCursorMaxPos = window->DC.CursorMaxPos;
        group_data.BackupIndent = window->DC.Indent;
        group_data.BackupGroupOffset = window->DC.GroupOffset;
        group_data.BackupCurrLineSize = window->DC.CurrLineSize;
        group_data.BackupCurrLineTextBaseOffset = window->DC.CurrLineTextBaseOffset;
        group_data.BackupActiveIdIsAlive = g.ActiveIdIsAlive;
        group_data.BackupHoveredIdIsAlive = g.HoveredId != 0;
        group_data.BackupActiveIdPreviousFrameIsAlive = g.ActiveIdPreviousFrameIsAlive;
        group_data.EmitItem = true;

        window->DC.GroupOffset.x = window->DC.CursorPos.x - window->Pos.x - window->DC.ColumnsOffset.x;
        window->DC.Indent = window->DC.GroupOffset;
        window->DC.CursorMaxPos = window->DC.CursorPos;
        window->DC.CurrLineSize = ImVec2(0.0f, 0.0f);
        if (g.LogEnabled) g.LogLinePosY = -FLT_MAX;
    }

    void EndGroup()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        IM_ASSERT(g.GroupStack.Size > 0);

        ImGuiGroupData& group_data = g.GroupStack.back();
        IM_ASSERT(group_data.WindowID == window->ID);

        if (window->DC.IsSetPos) ErrorCheckUsingSetCursorPosToExtendParentBoundaries();

        ImRect group_bb(group_data.BackupCursorPos, ImMax(window->DC.CursorMaxPos, group_data.BackupCursorPos));

        window->DC.CursorPos = group_data.BackupCursorPos;
        window->DC.CursorMaxPos = ImMax(group_data.BackupCursorMaxPos, window->DC.CursorMaxPos);
        window->DC.Indent = group_data.BackupIndent;
        window->DC.GroupOffset = group_data.BackupGroupOffset;
        window->DC.CurrLineSize = group_data.BackupCurrLineSize;
        window->DC.CurrLineTextBaseOffset = group_data.BackupCurrLineTextBaseOffset;
        if (g.LogEnabled) g.LogLinePosY = -FLT_MAX;

        if (!group_data.EmitItem)
        {
            g.GroupStack.pop_back();
            return;
        }

        window->DC.CurrLineTextBaseOffset = ImMax(window->DC.PrevLineTextBaseOffset, group_data.BackupCurrLineTextBaseOffset);
        ItemSize(group_bb.GetSize());
        ItemAdd(group_bb, 0, NULL, ImGuiItemFlags_NoTabStop);

        const bool group_contains_curr_active_id = (group_data.BackupActiveIdIsAlive != g.ActiveId) && (g.ActiveIdIsAlive == g.ActiveId) && g.ActiveId;
        const bool group_contains_prev_active_id = (group_data.BackupActiveIdPreviousFrameIsAlive == false) && (g.ActiveIdPreviousFrameIsAlive == true);
        if (group_contains_curr_active_id) g.LastItemData.ID = g.ActiveId;
        else if (group_contains_prev_active_id) g.LastItemData.ID = g.ActiveIdPreviousFrame;
        g.LastItemData.Rect = group_bb;

        const bool group_contains_curr_hovered_id = (group_data.BackupHoveredIdIsAlive == false) && g.HoveredId != 0;
        if (group_contains_curr_hovered_id) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;

        if (group_contains_curr_active_id && g.ActiveIdHasBeenEditedThisFrame) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Edited;

        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDeactivated;
        if (group_contains_prev_active_id && g.ActiveId != g.ActiveIdPreviousFrame) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Deactivated;

        g.GroupStack.pop_back();
    }

    void Separator_line()
    {
        GetWindowDrawList()->AddRectFilled(GetCursorScreenPos(), GetCursorScreenPos() + ImVec2(GetContentRegionMax().x - GetStyle().WindowPadding.x, 1), GetColorU32(c::separator));
        Spacing();
    }

    void SeparatorEx(ImGuiSeparatorFlags flags, float thickness)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return;

        ImGuiContext& g = *GImGui;
        IM_ASSERT(ImIsPowerOfTwo(flags & (ImGuiSeparatorFlags_Horizontal | ImGuiSeparatorFlags_Vertical)));
        IM_ASSERT(thickness > 0.0f);

        if (flags & ImGuiSeparatorFlags_Vertical)
        {
            float y1 = window->DC.CursorPos.y;
            float y2 = window->DC.CursorPos.y + window->DC.CurrLineSize.y;
            const ImRect bb(ImVec2(window->DC.CursorPos.x, y1 + (GetStyle().ItemSpacing.y / 2)), ImVec2(window->DC.CursorPos.x + thickness, y2 - (GetStyle().ItemSpacing.y / 2)));


            ItemSize(ImVec2(thickness, 0.0f));
            if (!ItemAdd(bb, 0)) return;

            window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColorU32(c::child::background));

            ImGui::SameLine();
        }
        else if (flags & ImGuiSeparatorFlags_Horizontal)
        {
            float x1 = window->Pos.x;
            float x2 = window->Pos.x + window->Size.x;

            if (g.GroupStack.Size > 0 && g.GroupStack.back().WindowID == window->ID) x1 += window->DC.Indent.x;

            if (ImGuiTable* table = g.CurrentTable)
            {
                x1 = table->Columns[table->CurrentColumn].MinX;
                x2 = table->Columns[table->CurrentColumn].MaxX;
            }

            ImGuiOldColumns* columns = (flags & ImGuiSeparatorFlags_SpanAllColumns) ? window->DC.CurrentColumns : NULL;
            if (columns) PushColumnsBackground();

            const float thickness_for_layout = (thickness == 1.0f) ? 0.0f : thickness;
            const ImRect bb(ImVec2(x1 + GetStyle().WindowPadding.x, window->DC.CursorPos.y), ImVec2(x2 - GetStyle().WindowPadding.x, window->DC.CursorPos.y + thickness));

            ItemSize(ImVec2(0.0f, thickness_for_layout));

            if (ItemAdd(bb, 0))
            {
                window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColorU32(c::separator));
            }
            if (columns)
            {
                PopColumnsBackground();
                columns->LineMinY = window->DC.CursorPos.y;
            }
        }
    }



    void Separator()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        if (window->SkipItems) return;

        ImGuiSeparatorFlags flags = (window->DC.LayoutType == ImGuiLayoutType_Horizontal) ? ImGuiSeparatorFlags_Vertical : ImGuiSeparatorFlags_Horizontal;
        flags |= ImGuiSeparatorFlags_SpanAllColumns;
        SeparatorEx(flags, 1.0f);
    }

    struct button_state
    {
        ImVec4 background, text;
    };

    bool Button(const char* label, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();

        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true), pos = window->DC.CursorPos;

        ImVec2 size = CalcItemSize(size_arg, label_size.x, label_size.y);

        const ImRect bb(pos, pos + size);

        ItemSize(size, 0.f);
        if (!ItemAdd(bb, id)) return false;

        bool hovered, held, pressed = ButtonBehavior(bb, id, &hovered, &held, NULL);

        static std::map<ImGuiID, button_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, button_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.background = ImLerp(it_anim->second.background, IsItemActive() || hovered ? c::page::background_active : c::page::background, g.IO.DeltaTime * 6.f);
        it_anim->second.text = ImLerp(it_anim->second.text, IsItemActive() || hovered ? c::accent : c::page::text, g.IO.DeltaTime * 6.f);

        GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(it_anim->second.background), c::page::rounding);

        PushClipRect(bb.Min, bb.Max, true);

        PushFont(font::Icons);
        GetWindowDrawList()->AddText(ImVec2(bb.Min.x + (size_arg.x - CalcTextSize(label).x) / 2, bb.Max.y - CalcTextSize(label).y - (size.y - CalcTextSize(label).y) / 2), GetColorU32(it_anim->second.text), label);
        PopFont();

        PopClipRect();


        return pressed;
    }


    struct page_state
    {
        ImVec4 background = {};
        ImVec4 text = {};
        float smooth_swap = 0.0f;
        float alpha_line = 0.0f;
        float line_size = 0.0f;
    };

    bool Page(bool selected, const char* icon, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();

        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(icon);
        const ImVec2 label_size = CalcTextSize(icon, NULL, true), pos = window->DC.CursorPos;

        ImVec2 size = CalcItemSize(size_arg, label_size.x, label_size.y);

        const ImRect bb(pos, pos + size);

        ItemSize(size, 0.f);
        if (!ItemAdd(bb, id)) return false;

        bool hovered, held, pressed = ButtonBehavior(bb, id, &hovered, &held, NULL);

        static std::map<ImGuiID, page_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, page_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.background = ImLerp(it_anim->second.background, selected || hovered ? c::page::background_active : c::page::background, g.IO.DeltaTime * 6.f);
        it_anim->second.text = ImLerp(it_anim->second.text, selected ? c::accent : hovered ? c::page::text_hov : c::page::text, g.IO.DeltaTime * 14.f);

        it_anim->second.alpha_line = ImLerp(it_anim->second.alpha_line, selected ? 1.f : 0.f, g.IO.DeltaTime * 6.f);
        it_anim->second.line_size = ImLerp(it_anim->second.line_size, selected ? (size_arg.x / 4) : (size_arg.x / 2), g.IO.DeltaTime * 6.f);

        it_anim->second.smooth_swap = ImLerp(it_anim->second.smooth_swap, selected ? 26.f : 0, g.IO.DeltaTime * 12.f);

        GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(it_anim->second.background), c::page::rounding);

        GetWindowDrawList()->AddRectFilled(bb.Min + ImVec2(+it_anim->second.line_size, (size_arg.y + 9)), bb.Max + ImVec2(-it_anim->second.line_size, 15), GetColorU32(c::accent, it_anim->second.alpha_line), c::page::rounding, ImDrawFlags_RoundCornersTop);

        PushClipRect(bb.Min, bb.Max, true);

        PushFont(font::Icons);
        GetWindowDrawList()->AddText(ImVec2(bb.Min.x + (size_arg.x - CalcTextSize(icon).x) / 2, bb.Max.y - CalcTextSize(icon).y - (size.y - CalcTextSize(icon).y) / 2 + it_anim->second.smooth_swap), GetColorU32(it_anim->second.text), icon);
        GetWindowDrawList()->AddText(ImVec2(bb.Min.x + (size_arg.x - CalcTextSize(icon).x) / 2, bb.Max.y - CalcTextSize(icon).y - (size.y - CalcTextSize(icon).y) / 2 - 25 + it_anim->second.smooth_swap), GetColorU32(it_anim->second.text), icon);
        PopFont();

        PopClipRect();


        return pressed;
    }

    struct check_state
    {
        ImVec4 background = {}, text = {};
        float alpha_mark = 0.0f;
    };

    bool Checkbox(const char* label, bool* v)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        const float square_sz = 20;
        const ImVec2 pos = window->DC.CursorPos;

        const float w = GetContentRegionMax().x - style.WindowPadding.x;

        const ImRect total_bb(pos + ImVec2(w - square_sz, 0), pos + ImVec2(w, 32));
        ItemSize(total_bb, 0.f);

        if (!ItemAdd(total_bb, id)) return false;

        bool hovered, held, pressed = ButtonBehavior(total_bb, id, &hovered, &held);

        static std::map<ImGuiID, check_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, check_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.background = ImLerp(it_anim->second.background, *v ? c::accent : hovered ? c::elements::background_hovered : c::elements::background, g.IO.DeltaTime * 6.f);
        it_anim->second.text = ImLerp(it_anim->second.text, *v ? c::text::text_active : hovered ? c::text::text_hov : c::text::text, g.IO.DeltaTime * 6.f);

        it_anim->second.alpha_mark = ImLerp(it_anim->second.alpha_mark, *v ? 1.f : 0.f, g.IO.DeltaTime * 6.f);

        if (IsItemClicked())
        {
            *v = !(*v);
            MarkItemEdited(id);
        }

        const ImRect check_bb(pos + ImVec2(w - square_sz, (32 - square_sz) / 2), pos + ImVec2(w, (32 + square_sz) / 2));

        GetWindowDrawList()->AddRectFilled(check_bb.Min, check_bb.Max, GetColorU32(it_anim->second.background), c::elements::rounding);

        RenderCheckMark(GetWindowDrawList(), check_bb.Min + ImVec2(square_sz / 2, square_sz / 2) / 2, GetColorU32(c::checkbox::mark, it_anim->second.alpha_mark), square_sz / 2);

        GetWindowDrawList()->AddText(pos + ImVec2(0, (32 - CalcTextSize(label).y) / 2), GetColorU32(it_anim->second.text), label);

        return pressed;
    }

    static float CalcMaxPopupHeightFromItemCount(int items_count)
    {
        ImGuiContext& g = *GImGui;
        if (items_count <= 0)
            return FLT_MAX;
        return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
    }

    int rotation_start_index;
    void ImRotateStart()
    {
        rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    }

    ImVec2 ImRotationCenter()
    {
        ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);

        const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
        for (int i = rotation_start_index; i < buf.Size; i++)
            l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);

        return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
    }

    void ImRotateEnd(float rad, ImVec2 center = ImRotationCenter())
    {
        float s = sin(rad), c = cos(rad);
        center = ImRotate(center, s, c) - center;

        auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
        for (int i = rotation_start_index; i < buf.Size; i++)
            buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
    }

    struct begin_state
    {
        ImVec4 background = {}, text = {}, outline = {};
        float open = 0.f;
        float alpha = 0.f;
        float combo_size = 0.f;
        float shadow_opticaly = 0.f;
        bool opened_combo = false;
        bool hovered = false;
        float arrow_roll = 0.f;
    };

    bool BeginCombo(const char* label, const char* preview_value, int val, bool multi, ImGuiComboFlags flags)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = GetCurrentWindow();

        g.NextWindowData.ClearFlags();
        if (window->SkipItems) return false;

        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        const ImVec2 label_size = CalcTextSize(label, NULL, true);
        const float w = ((GetContentRegionMax().x - style.WindowPadding.x));

        const ImRect bb(window->DC.CursorPos + ImVec2(w / 2, 0), window->DC.CursorPos + ImVec2(w, 32));
        const ImRect total_bb(bb.Min, bb.Max);
        ItemSize(bb, 0.f);

        if (!ItemAdd(total_bb, id, &bb)) return false;

        bool hovered, held, pressed = ButtonBehavior(bb, id, &hovered, &held);

        static std::map<ImGuiID, begin_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, begin_state() });
            it_anim = anim.find(id);
        }

        if (hovered && g.IO.MouseClicked[0] || it_anim->second.opened_combo && g.IO.MouseClicked[0] && !it_anim->second.hovered) it_anim->second.opened_combo = !it_anim->second.opened_combo;

        it_anim->second.arrow_roll = ImLerp(it_anim->second.arrow_roll, it_anim->second.opened_combo ? -1.f : 1.f, g.IO.DeltaTime * 6.f);
        it_anim->second.text = ImLerp(it_anim->second.text, it_anim->second.opened_combo ? c::text::text_active : hovered ? c::text::text_hov : c::text::text, g.IO.DeltaTime * 6.f);
        it_anim->second.background = ImLerp(it_anim->second.background, it_anim->second.opened_combo ? c::elements::background_hovered : c::elements::background, g.IO.DeltaTime * 6.f);
        it_anim->second.combo_size = ImLerp(it_anim->second.combo_size, it_anim->second.opened_combo ? (val * 30) + 15 : 0.f, g.IO.DeltaTime * 12.f);

        GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(it_anim->second.background), c::elements::rounding);

        GetWindowDrawList()->AddText(ImVec2(window->DC.CursorPos.x, bb.Min.y + (32 - CalcTextSize(label).y) / 2 + 1), GetColorU32(it_anim->second.text), label);

        PushClipRect(bb.Min, bb.Max, true);
        GetWindowDrawList()->AddText(ImVec2(bb.Min.x + ((32 - CalcTextSize(label).y) / 2), bb.Min.y + (32 - CalcTextSize(label).y) / 2), GetColorU32(c::text::text_active), preview_value);
        PopClipRect();

        GetWindowDrawList()->AddRectFilled(bb.Max - ImVec2(32, 32), bb.Max, GetColorU32(it_anim->second.background), c::elements::rounding);
        GetWindowDrawList()->AddRectFilledMultiColor(bb.Max - ImVec2(120, 32), bb.Max, GetColorU32(it_anim->second.background, 0.f), GetColorU32(it_anim->second.background, 1.f), GetColorU32(it_anim->second.background, 1.f), GetColorU32(it_anim->second.background, 0.f), c::elements::rounding);

        PushFont(font::Icons);
        ImRotateStart();
        GetWindowDrawList()->AddText(ImVec2(bb.Max.x - (32 + CalcTextSize("f").y) / 2, bb.Min.y + (32 - CalcTextSize("f").y) / 2), GetColorU32(c::text::text), "f");
        ImRotateEnd(1.57f * it_anim->second.arrow_roll);
        PopFont();

        if (!IsRectVisible(bb.Min, bb.Max + ImVec2(0, 2)))
        {
            it_anim->second.opened_combo = false;
            it_anim->second.combo_size = 0.f;
        }

        if (!it_anim->second.opened_combo && it_anim->second.combo_size < 2.f) return false;

        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 5));
        ImGui::SetNextWindowSize(ImVec2(bb.GetWidth(), it_anim->second.combo_size));

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse;

        PushStyleColor(ImGuiCol_WindowBg, c::elements::background);
        PushStyleVar(ImGuiStyleVar_WindowRounding, c::elements::rounding);
        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
        PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        bool ret = Begin(label, NULL, window_flags);

        PopStyleVar(3);
        PopStyleColor(1);

        it_anim->second.hovered = IsWindowHovered();

        if (multi && it_anim->second.hovered && g.IO.MouseClicked[0]) it_anim->second.opened_combo = false;

        return true;
    }

    void EndCombo()
    {
        End();
    }

    void MultiCombo(const char* label, bool variable[], const char* labels[], int count)
    {
        ImGuiContext& g = *GImGui;

        std::string preview = "None";

        for (auto i = 0, j = 0; i < count; i++)
        {
            if (variable[i])
            {
                if (j)
                    preview += (", ") + (std::string)labels[i];
                else
                    preview = labels[i];

                j++;
            }
        }

        if (BeginCombo(label, preview.c_str(), count))
        {
            for (auto i = 0; i < count; i++)
            {
                PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 15));
                PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
                custom::Selectable(labels[i], &variable[i], ImGuiSelectableFlags_DontClosePopups);
                PopStyleVar(2);
            }
            End();
        }

        preview = ("None");
    }

    bool BeginComboPreview()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImGuiComboPreviewData* preview_data = &g.ComboPreviewData;

        if (window->SkipItems || !(g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)) return false;

        IM_ASSERT(g.LastItemData.Rect.Min.x == preview_data->PreviewRect.Min.x && g.LastItemData.Rect.Min.y == preview_data->PreviewRect.Min.y);

        if (!window->ClipRect.Overlaps(preview_data->PreviewRect)) return false;

        preview_data->BackupCursorPos = window->DC.CursorPos;
        preview_data->BackupCursorMaxPos = window->DC.CursorMaxPos;
        preview_data->BackupCursorPosPrevLine = window->DC.CursorPosPrevLine;
        preview_data->BackupPrevLineTextBaseOffset = window->DC.PrevLineTextBaseOffset;
        preview_data->BackupLayout = window->DC.LayoutType;
        window->DC.CursorPos = preview_data->PreviewRect.Min + g.Style.FramePadding;
        window->DC.CursorMaxPos = window->DC.CursorPos;
        window->DC.LayoutType = ImGuiLayoutType_Horizontal;
        window->DC.IsSameLine = false;
        PushClipRect(preview_data->PreviewRect.Min, preview_data->PreviewRect.Max, true);

        return true;
    }

    void EndComboPreview()
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImGuiComboPreviewData* preview_data = &g.ComboPreviewData;

        ImDrawList* draw_list = window->DrawList;
        if (window->DC.CursorMaxPos.x < preview_data->PreviewRect.Max.x && window->DC.CursorMaxPos.y < preview_data->PreviewRect.Max.y)
            if (draw_list->CmdBuffer.Size > 1)
            {
                draw_list->_CmdHeader.ClipRect = draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ClipRect = draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 2].ClipRect;
                draw_list->_TryMergeDrawCmds();
            }
        PopClipRect();
        window->DC.CursorPos = preview_data->BackupCursorPos;
        window->DC.CursorMaxPos = ImMax(window->DC.CursorMaxPos, preview_data->BackupCursorMaxPos);
        window->DC.CursorPosPrevLine = preview_data->BackupCursorPosPrevLine;
        window->DC.PrevLineTextBaseOffset = preview_data->BackupPrevLineTextBaseOffset;
        window->DC.LayoutType = preview_data->BackupLayout;
        window->DC.IsSameLine = false;
        preview_data->PreviewRect = ImRect();
    }

    static const char* Items_ArrayGetter(void* data, int idx)
    {
        const char* const* items = (const char* const*)data;
        return items[idx];
    }

    static const char* Items_SingleStringGetter(void* data, int idx)
    {
        const char* items_separated_by_zeros = (const char*)data;
        int items_count = 0;
        const char* p = items_separated_by_zeros;
        while (*p)
        {
            if (idx == items_count)
                break;
            p += strlen(p) + 1;
            items_count++;
        }
        return *p ? p : NULL;
    }

    bool Combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items)
    {
        ImGuiContext& g = *GImGui;

        const char* preview_value = NULL;
        if (*current_item >= 0 && *current_item < items_count)
            preview_value = getter(user_data, *current_item);

        if (popup_max_height_in_items != -1 && !(g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint))
            SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, CalcMaxPopupHeightFromItemCount(popup_max_height_in_items)));

        if (!BeginCombo(label, preview_value, items_count, ImGuiComboFlags_None)) return false;

        bool value_changed = false;
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 15));
        for (int i = 0; i < items_count; i++)
        {
            const char* item_text = getter(user_data, i);
            if (item_text == NULL)
                item_text = "";

            PushID(i);
            const bool item_selected = (i == *current_item);
            if (custom::Selectable(item_text, item_selected) && *current_item != i)
            {
                value_changed = true;
                *current_item = i;
            }
            if (item_selected)
                SetItemDefaultFocus();
            PopID();
        }
        PopStyleVar();

        EndCombo();

        if (value_changed)
            MarkItemEdited(g.LastItemData.ID);

        return value_changed;
    }

    // Getter function that works with std::vector<std::string>
    static const char* StringVectorGetter(void* user_data, int idx)
    {
        auto* items = reinterpret_cast<std::vector<std::string>*>(user_data);
        if (idx < 0 || idx >= static_cast<int>(items->size()))
            return nullptr;
        return (*items)[idx].c_str();
    }

    bool Combo(const char* label, int* current_item, const std::vector<std::string>& items, int items_count, int height_in_items)
    {
        return Combo(label, current_item, StringVectorGetter,
            const_cast<void*>(reinterpret_cast<const void*>(&items)),
            items_count, height_in_items);
    }

    bool Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int height_in_items)
    {
        int items_count = 0;
        const char* p = items_separated_by_zeros;
        while (*p)
        {
            p += strlen(p) + 1;
            items_count++;
        }
        bool value_changed = Combo(label, current_item, Items_SingleStringGetter, (void*)items_separated_by_zeros, items_count, height_in_items);
        return value_changed;
    }



    struct select_state
    {
        ImVec4 text;
    };

    bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        ImGuiID id = window->GetID(label);
        ImVec2 label_size = CalcTextSize(label, NULL, true);
        ImVec2 size(size_arg.x != 0.0f ? size_arg.x : label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
        ImVec2 pos = window->DC.CursorPos;
        pos.y += window->DC.CurrLineTextBaseOffset;
        ItemSize(size, 0.0f);

        const bool span_all_columns = (flags & ImGuiSelectableFlags_SpanAllColumns) != 0;
        const float min_x = span_all_columns ? window->ParentWorkRect.Min.x : pos.x;
        const float max_x = span_all_columns ? window->ParentWorkRect.Max.x : window->WorkRect.Max.x;
        if (size_arg.x == 0.0f || (flags & ImGuiSelectableFlags_SpanAvailWidth)) size.x = ImMax(label_size.x, max_x - min_x);

        const ImVec2 text_min = pos;
        const ImVec2 text_max(min_x + size.x, pos.y + size.y);

        ImRect bb(min_x, pos.y, text_max.x, text_max.y);
        if ((flags & ImGuiSelectableFlags_NoPadWithHalfSpacing) == 0)
        {
            const float spacing_x = span_all_columns ? 0.0f : style.ItemSpacing.x;
            const float spacing_y = style.ItemSpacing.y;
            const float spacing_L = IM_TRUNC(spacing_x * 0.50f);
            const float spacing_U = IM_TRUNC(spacing_y * 0.50f);
            bb.Min.x -= spacing_L;
            bb.Min.y -= spacing_U;
            bb.Max.x += (spacing_x - spacing_L);
            bb.Max.y += (spacing_y - spacing_U);
        }

        const float backup_clip_rect_min_x = window->ClipRect.Min.x;
        const float backup_clip_rect_max_x = window->ClipRect.Max.x;
        if (span_all_columns)
        {
            window->ClipRect.Min.x = window->ParentWorkRect.Min.x;
            window->ClipRect.Max.x = window->ParentWorkRect.Max.x;
        }

        const bool disabled_item = (flags & ImGuiSelectableFlags_Disabled) != 0;
        const bool item_add = ItemAdd(bb, id, NULL, disabled_item ? ImGuiItemFlags_Disabled : ImGuiItemFlags_None);
        if (span_all_columns)
        {
            window->ClipRect.Min.x = backup_clip_rect_min_x;
            window->ClipRect.Max.x = backup_clip_rect_max_x;
        }

        if (!item_add) return false;

        const bool disabled_global = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        if (disabled_item && !disabled_global) BeginDisabled();

        if (span_all_columns && window->DC.CurrentColumns) PushColumnsBackground();
        else if (span_all_columns && g.CurrentTable) TablePushBackgroundChannel();

        ImGuiButtonFlags button_flags = 0;
        if (flags & ImGuiSelectableFlags_NoHoldingActiveID) { button_flags |= ImGuiButtonFlags_NoHoldingActiveId; }
        if (flags & ImGuiSelectableFlags_NoSetKeyOwner) { button_flags |= ImGuiButtonFlags_NoSetKeyOwner; }
        if (flags & ImGuiSelectableFlags_SelectOnClick) { button_flags |= ImGuiButtonFlags_PressedOnClick; }
        if (flags & ImGuiSelectableFlags_SelectOnRelease) { button_flags |= ImGuiButtonFlags_PressedOnRelease; }
        if (flags & ImGuiSelectableFlags_AllowDoubleClick) { button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick; }
        if ((flags & ImGuiSelectableFlags_AllowOverlap) || (g.LastItemData.InFlags & ImGuiItemFlags_AllowOverlap)) { button_flags |= ImGuiButtonFlags_AllowOverlap; }

        const bool was_selected = selected;
        bool hovered, held, pressed = ButtonBehavior(bb, id, &hovered, &held, button_flags);

        if ((flags & ImGuiSelectableFlags_SelectOnNav) && g.NavJustMovedToId != 0 && g.NavJustMovedToFocusScopeId == g.CurrentFocusScopeId)
            if (g.NavJustMovedToId == id)  selected = pressed = true;

        // Update NavId when clicking or when Hovering (this doesn't happen on most widgets), so navigation can be resumed with gamepad/keyboard
        if (pressed || (hovered && (flags & ImGuiSelectableFlags_SetNavIdOnHover)))
        {
            if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent)
            {
                SetNavID(id, window->DC.NavLayerCurrent, g.CurrentFocusScopeId, WindowRectAbsToRel(window, bb)); // (bb == NavRect)
                g.NavDisableHighlight = true;
            }
        }
        if (pressed) MarkItemEdited(id);

        if (selected != was_selected)  g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_ToggledSelection;


        if (g.NavId == id) RenderNavHighlight(bb, id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);

        if (span_all_columns && window->DC.CurrentColumns) PopColumnsBackground();
        else if (span_all_columns && g.CurrentTable) TablePopBackgroundChannel();

        static std::map<ImGuiID, select_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, select_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.text = ImLerp(it_anim->second.text, selected ? c::accent : c::text::text_active, g.IO.DeltaTime * 6.f);

        PushStyleColor(ImGuiCol_Text, GetColorU32(it_anim->second.text));
        RenderTextClipped(text_min, text_max, label, NULL, &label_size, style.SelectableTextAlign, &bb);
        PopStyleColor();

        if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(g.LastItemData.InFlags & ImGuiItemFlags_SelectableDontClosePopup)) CloseCurrentPopup();

        if (disabled_item && !disabled_global) EndDisabled();

        return pressed;
    }

    bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg)
    {
        if (Selectable(label, *p_selected, flags, size_arg))
        {
            *p_selected = !*p_selected;
            return true;
        }
        return false;
    }


    struct knob_state {
        float plus_float = 0.0f;
        int plus_int = 0;
        ImVec4 background = {}, circle = {}, text = {};
        float slow_anim = 0.0f;
        float circle_anim = 0.0f;
        float position = 0.0f;
    };


    bool KnobScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const void* step, const char* format, ImGuiSliderFlags flags)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const float w = GetContentRegionMax().x - style.WindowPadding.x;

        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        const ImRect frame_bb(window->DC.CursorPos + ImVec2(0, 0), window->DC.CursorPos + ImVec2(w, 32));

        const ImRect slider_bb(window->DC.CursorPos + ImVec2(w - 30, 0), window->DC.CursorPos + ImVec2(w, 100));

        const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? label_size.x : 0.0f, 0.0f));

        const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
        ItemSize(ImRect(total_bb.Min, total_bb.Max - ImVec2(0, 0)));

        if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0)) return false;

        if (format == NULL) format = DataTypeGetInfo(data_type)->PrintFmt;

        bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.InFlags), held, pressed = ButtonBehavior(frame_bb, id, &hovered, &held, NULL);

        // Fine control with scroll wheel
        bool scrolled = false;
        if (hovered && g.IO.MouseWheel != 0.0f)
        {
            switch (data_type)
            {
            case ImGuiDataType_S32:
            {
                int stepp = *(const int*)step;
                int* val = (int*)p_data;
                *val += (int)(g.IO.MouseWheel * stepp);
                scrolled = true;
                MarkItemEdited(id);
                break;
            }
            case ImGuiDataType_Float:
            {
                float stepp = *(const float*)step;
                float* val = (float*)p_data;
                *val += g.IO.MouseWheel * stepp;
                scrolled = true;
                MarkItemEdited(id);
                break;
            }
            }

            // Clamp value
            DataTypeClamp(data_type, p_data, p_min, p_max);
        }

        ImRect grab_bb;

        static std::map<ImGuiID, knob_state> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, knob_state() });
            it_anim = anim.find(id);
        }

        it_anim->second.circle_anim = ImLerp(it_anim->second.circle_anim, IsItemActive() ? 11.f : 10.f, g.IO.DeltaTime * 6.f);

        if ((flags & ImGuiSliderFlags_Integer) == 0) {
            it_anim->second.plus_float = ImLerp(it_anim->second.plus_float, *(float*)p_data <= *(float*)p_max && hovered && GetAsyncKeyState(VK_OEM_PLUS) & 0x01 ? *(float*)p_data += 0.05f : *(float*)p_data >= *(float*)p_min && hovered && GetAsyncKeyState(VK_OEM_MINUS) & 0x01 ? *(float*)p_data -= 0.05f : 0, g.IO.DeltaTime * 6.f);
            if (*(float*)p_data > *(float*)p_max) *(float*)p_data = *(float*)p_max;
            if (*(float*)p_data < *(float*)p_min) *(float*)p_data = *(float*)p_min;
        }
        else
        {
            it_anim->second.plus_int = ImLerp(it_anim->second.plus_int, *(int*)p_data <= *(int*)p_max && hovered && GetAsyncKeyState(VK_OEM_PLUS) & 0x01 ? *(int*)p_data += 1 : *(int*)p_data >= *(int*)p_min && hovered && GetAsyncKeyState(VK_OEM_MINUS) & 0x01 ? *(int*)p_data -= 1 : 0, g.IO.DeltaTime * 6.f);
            if (*(int*)p_data > *(int*)p_max) *(int*)p_data = *(int*)p_max;
            if (*(int*)p_data < *(int*)p_min) *(int*)p_data = *(int*)p_min;
        }

        it_anim->second.text = ImLerp(
            it_anim->second.text,
            hovered ? c::text::text_active : c::text::text,
            g.IO.DeltaTime * 6.f
        );

        const bool value_changed = DragBehavior(id, data_type, p_data, 0.f, p_min, p_max, format, NULL);


        if (value_changed) MarkItemEdited(id);

        char value_buf[64];
        const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);

        float radius = 10.f;
        float thickness = 3.f;

        it_anim->second.position = ImLerp(it_anim->second.position, *static_cast<float*>(p_data) / *reinterpret_cast<const float*>(p_max) * 6.25f, ImGui::GetIO().DeltaTime * 18.f);

        GetWindowDrawList()->PathClear();
        GetWindowDrawList()->PathArcTo(ImVec2(frame_bb.Max.x + radius - 22.f, frame_bb.Min.y + (32 / 2)), radius, 0.f, 2.f * IM_PI, 40);
        GetWindowDrawList()->PathStroke(GetColorU32(c::elements::background), 0, thickness);

        GetWindowDrawList()->PathClear();
        GetWindowDrawList()->PathArcTo(ImVec2(frame_bb.Max.x + radius - 22.f, frame_bb.Min.y + (32 / 2)), radius, IM_PI * 1.5f, IM_PI * 1.5f + it_anim->second.position, 40);
        GetWindowDrawList()->PathStroke(GetColorU32(c::accent), 0, thickness);

        GetWindowDrawList()->AddCircleFilled(ImVec2(frame_bb.Max.x + radius - 22.f + ImCos(IM_PI * 1.5f + it_anim->second.position) * radius, frame_bb.Min.y + (32 / 2) + ImSin(IM_PI * 1.5f + it_anim->second.position) * radius), 2.f, GetColorU32(c::text::text_active));

        GetWindowDrawList()->AddText(ImVec2(frame_bb.Max.x - (40 + CalcTextSize(value_buf).x), frame_bb.Min.y + (32 - CalcTextSize(value_buf).y) / 2), GetColorU32(c::text::text), value_buf);

        GetWindowDrawList()->AddText(ImVec2(frame_bb.Max.x - w, frame_bb.Min.y + (32 - CalcTextSize(value_buf).y) / 2), GetColorU32(it_anim->second.text), label);

        return value_changed;
    }

    bool KnobFloat(const char* label, float* v, float v_min, float v_max, float step, const char* format, ImGuiSliderFlags flags)
    {
        return KnobScalar(label, ImGuiDataType_Float, v, &v_min, &v_max, &step, format, flags);
    }

    bool KnobInt(const char* label, int* v, int v_min, int v_max, int step, const char* format, ImGuiSliderFlags flags)
    {
        return KnobScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, &step, format, flags | ImGuiSliderFlags_Integer);
    }






    struct slider_state {
        ImVec4 background = {};
        ImVec4 circle = {};
        ImVec4 text = {};
        float position = 0.0f;
        float slow = 0.0f;
    };

    bool SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const void* step, const char* format, ImGuiSliderFlags flags)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const float w = GetContentRegionMax().x - style.WindowPadding.x;
        const ImVec2 label_size = CalcTextSize(label, NULL, true);
        const ImRect frame_bb(window->DC.CursorPos + ImVec2(0, 0), window->DC.CursorPos + ImVec2(w, 50));
        const ImRect slider_bb(window->DC.CursorPos + ImVec2(0, 40), window->DC.CursorPos + ImVec2(w, 45));
        const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? label_size.x : 0.0f, 0.0f));
        const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
        ItemSize(ImRect(total_bb.Min, total_bb.Max - ImVec2(0, 18)));
        if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0)) return false;

        if (format == NULL) format = DataTypeGetInfo(data_type)->PrintFmt;
        bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.InFlags), held, pressed = ButtonBehavior(frame_bb, id, &hovered, &held, NULL);

        // Fine control with scroll wheel
        if (hovered && g.IO.MouseWheel != 0.0f)
        {
            switch (data_type)
            {
            case ImGuiDataType_S32: 
            {
                int stepp = *(const int*)step;
                int* val = (int*)p_data;
                *val += (int)(g.IO.MouseWheel * stepp);
                MarkItemEdited(id);
                break;
            }
            case ImGuiDataType_Float:
            {
                float stepp = *(const float*)step;
                float* val = (float*)p_data;
                *val += g.IO.MouseWheel * stepp;
                MarkItemEdited(id);
                break;
            }
            }

            // Clamp value
            DataTypeClamp(data_type, p_data, p_min, p_max);
        }

        ImRect grab_bb;
        static std::map<ImGuiID, slider_state> anim;
        auto it_anim = anim.find(id);
        if (it_anim == anim.end())
        {
            anim.insert({ id, slider_state() });
            it_anim = anim.find(id);
        }
        it_anim->second.text = ImLerp(
            it_anim->second.text,
            hovered ? c::text::text_active : c::text::text,
            g.IO.DeltaTime * 6.f
        );

        const bool value_changed = SliderBehavior(ImRect(frame_bb.Min - ImVec2(4, 0), frame_bb.Max + ImVec2(10, 0)), id, data_type, p_data, p_min, p_max, format, flags, &grab_bb);

        GetWindowDrawList()->AddRectFilled(slider_bb.Min, slider_bb.Max, GetColorU32(c::elements::background), 30, ImDrawFlags_RoundCornersTop);
        if (value_changed) MarkItemEdited(id);

        char value_buf[64];
        const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
        it_anim->second.slow = ImLerp(it_anim->second.slow, grab_bb.Min.x - (frame_bb.Min.x), g.IO.DeltaTime * 25.f);

        PushClipRect(slider_bb.Min + ImVec2(1, 0), slider_bb.Max, true);
        GetWindowDrawList()->AddRectFilled(slider_bb.Min + ImVec2(0, 0), ImVec2(it_anim->second.slow + (slider_bb.Min.x + 2), grab_bb.Max.y - 3), GetColorU32(c::accent), 30.f, ImDrawFlags_RoundCornersTop);
        PopClipRect();

        GetWindowDrawList()->AddText(ImVec2(frame_bb.Max.x - CalcTextSize(value_buf).x, frame_bb.Min.y + (32 - CalcTextSize(value_buf).y) / 2), GetColorU32(c::text::text), value_buf);
        GetWindowDrawList()->AddText(ImVec2(frame_bb.Max.x - w, frame_bb.Min.y + (32 - CalcTextSize(value_buf).y) / 2), GetColorU32(it_anim->second.text), label);

        return value_changed;
    }

    bool SliderFloat(const char* label, float* v, float v_min, float v_max, float step, const char* format, ImGuiSliderFlags flags)
    {
        return SliderScalar(label, ImGuiDataType_Float, v, &v_min, &v_max, &step, format, flags);
    }

    bool SliderInt(const char* label, int* v, int v_min, int v_max, int step, const char* format, ImGuiSliderFlags flags)
    {
        return SliderScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, &step, format, flags);
    }

    void Text(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        TextV(fmt, args);
        va_end(args);
    }

    void TextV(const char* fmt, va_list args)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        char buf[1024];
        ImFormatStringV(buf, IM_ARRAYSIZE(buf), fmt, args);

        const ImGuiID id = window->GetID(buf);
        const ImVec2 label_size = CalcTextSize(buf, NULL, true);
        const ImVec2 pos = window->DC.CursorPos;
        const float w = GetContentRegionMax().x - style.WindowPadding.x;
        const ImRect total_bb(pos, pos + ImVec2(w, 32));

        ItemSize(total_bb, 0.f);
        if (!ItemAdd(total_bb, id)) return;

        ImVec2 text_pos = pos;
        if (true)
        {
            text_pos.x += (w - label_size.x) * 0.5f;
        }
        text_pos.y += (32 - CalcTextSize(buf).y) / 2;

        GetWindowDrawList()->AddText(
            text_pos,
            GetColorU32(c::text::text_active),
            buf
        );
    }

    // https://github.com/ocornut/imgui/issues/1901#issue-335266223
    void Spinner(const char* label, float radius, int thickness, const ImU32& color)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return;

        // Render
        window->DrawList->PathClear();

        int num_segments = 30;
        int start = (int)abs(ImSin((float)g.Time * 1.8f) * (num_segments - 5));

        const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
        const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

        const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

        for (int i = 0; i < num_segments; i++)
        {
            const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + (float)g.Time * 8) * radius,
                centre.y + ImSin(a + (float)g.Time * 8) * radius));
        }

        window->DrawList->PathStroke(color, false, (float)thickness);
    }
}
