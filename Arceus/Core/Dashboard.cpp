#include "Dashboard.h"

#include <cstdio>
#include <cstring>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "../ImGui/imgui.h"
#include "../ImGui/VKNames.h"

// ============================================================================
//  Global configuration instance
// ============================================================================
Config g_Config;

// ============================================================================
//  Color palette
//  ---------------------------------------------------------------------------
//  Deep charcoal backgrounds, subtle slate borders and a neon-indigo accent.
// ============================================================================
namespace UI
{
    const ImVec4 kBackground   = ImVec4(13.f / 255.f, 17.f / 255.f, 23.f / 255.f, 1.f);   // #0D1117
    const ImVec4 kSurface      = ImVec4(18.f / 255.f, 22.f / 255.f, 31.f / 255.f, 1.f);   // #12161F
    const ImVec4 kBorder       = ImVec4(31.f / 255.f, 41.f / 255.f, 55.f / 255.f, 1.f);   // #1F2937
    const ImVec4 kAccent       = ImVec4(99.f / 255.f, 102.f / 255.f, 241.f / 255.f, 1.f); // #6366F1
    const ImVec4 kAccentHover  = ImVec4(129.f / 255.f, 140.f / 255.f, 248.f / 255.f, 1.f); // #818CF8
    const ImVec4 kAccentActive = ImVec4(79.f / 255.f, 70.f / 255.f, 229.f / 255.f, 1.f);  // #4F46E5
    const ImVec4 kText         = ImVec4(229.f / 255.f, 231.f / 255.f, 235.f / 255.f, 1.f); // #E5E7EB
    const ImVec4 kTextMuted    = ImVec4(156.f / 255.f, 163.f / 255.f, 175.f / 255.f, 1.f); // #9CA3AF
    const ImVec4 kSuccess      = ImVec4(34.f / 255.f, 197.f / 255.f, 94.f / 255.f, 1.f);  // #22C55E
    const ImVec4 kWarning      = ImVec4(245.f / 255.f, 158.f / 255.f, 11.f / 255.f, 1.f); // #F59E0B
    const ImVec4 kDanger       = ImVec4(239.f / 255.f, 68.f / 255.f, 68.f / 255.f, 1.f);  // #EF4444

    inline ImU32 U32(const ImVec4& c)
    {
        return ImGui::ColorConvertFloat4ToU32(c);
    }
}

// AARRGGBB <-> ImVec4
static ImVec4 U32ToVec4(std::uint32_t c)
{
    return ImVec4(
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >> 8)  & 0xFF) / 255.0f,
        ( c        & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
}

static std::uint32_t Vec4ToU32(const ImVec4& c)
{
    auto b = [](float f) { return (std::uint32_t)(f * 255.0f + 0.5f) & 0xFF; };
    return (b(c.w) << 24) | (b(c.x) << 16) | (b(c.y) << 8) | b(c.z);
}

// ============================================================================
//  Helpers
// ============================================================================

static const char* KeyName(int vk)
{
    auto it = VKNames.find(vk);
    if (it != VKNames.end() && it->second[0] != '\0')
        return it->second;

    static char fallback[24];
    std::snprintf(fallback, sizeof(fallback), "Key 0x%02X", vk);
    return fallback;
}

static const char* AimTargetName(AimTarget t)
{
    switch (t)
    {
    case AimTarget::Head:   return "Head";
    case AimTarget::Neck:   return "Neck";
    case AimTarget::Chest:  return "Chest";
    case AimTarget::Pelvis: return "Pelvis";
    default:                return "Head";
    }
}

static const char* StatusLabel(AppStatus s)
{
    switch (s)
    {
    case AppStatus::Active:     return "Active";
    case AppStatus::Undetected: return "Undetected";
    case AppStatus::Detected:   return "Detected";
    case AppStatus::Warning:    return "Warning";
    default:                    return "Unknown";
    }
}

static ImVec4 StatusColor(AppStatus s)
{
    switch (s)
    {
    case AppStatus::Active:
    case AppStatus::Undetected: return UI::kSuccess;
    case AppStatus::Detected:   return UI::kDanger;
    case AppStatus::Warning:    return UI::kWarning;
    default:                    return UI::kTextMuted;
    }
}

// ----------------------------------------------------------------------------
//  Keybind capture widget. While "capturing" the next key or mouse button
//  pressed is written into `key`. Escape cancels.
// ----------------------------------------------------------------------------
static int s_captureId       = -1;   // ImGuiID of the active capture widget
static int s_captureArmFrame = -1;   // ignore input until the frame after click

static bool KeybindWidget(const char* id, int* key)
{
    ImGui::PushID(id);

    const int widgetId = ImGui::GetID("##keybind");
    const bool capturing = (s_captureId == widgetId);
    const bool armed = (ImGui::GetFrameCount() >= s_captureArmFrame);

    if (ImGui::Button(capturing ? "Listening..." : KeyName(*key), ImVec2(112, 0)))
    {
        if (capturing)
        {
            s_captureId = -1;
        }
        else
        {
            s_captureId = widgetId;
            s_captureArmFrame = ImGui::GetFrameCount() + 1;
        }
    }

    bool changed = false;
    if (capturing && armed)
    {
        ImGuiIO& io = ImGui::GetIO();

        int pressed = 0;
        for (int vk = 0x07; vk <= 0xFF; ++vk)
        {
            if (io.KeysDown[vk])
            {
                pressed = vk;
                break;
            }
        }
        if (!pressed)
        {
            static const int mouseVk[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
            for (int m = 0; m < 5; ++m)
            {
                if (ImGui::IsMouseClicked(m))
                {
                    pressed = mouseVk[m];
                    break;
                }
            }
        }

        if (pressed)
        {
            if (pressed != VK_ESCAPE)
            {
                *key = pressed;
                changed = true;
            }
            s_captureId = -1;
        }
    }

    ImGui::PopID();
    return changed;
}

// ----------------------------------------------------------------------------
//  Styled switch (master toggles).
// ----------------------------------------------------------------------------
static bool ToggleSwitch(const char* id, const char* label, bool* value)
{
    ImGui::PushID(id);
    bool changed = false;

    const float width = 42.0f, height = 22.0f;
    ImGui::InvisibleButton("##switch", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        *value = !*value;
        changed = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetItemRectMin();

    const ImU32 track = *value ? UI::U32(UI::kAccent) : UI::U32(ImVec4(0.06f, 0.08f, 0.12f, 1.0f));
    dl->AddRectFilled(p, p + ImVec2(width, height), track, height * 0.5f);
    if (hovered)
        dl->AddRect(p, p + ImVec2(width, height), UI::U32(UI::kBorder), height * 0.5f, 0, 1.0f);

    const float knob = height - 4.0f;
    const float knobX = *value ? p.x + width - knob - 2.0f : p.x + 2.0f;
    dl->AddCircleFilled(ImVec2(knobX + knob * 0.5f, p.y + height * 0.5f), knob * 0.5f, UI::U32(UI::kText), 16);

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::PopID();
    return changed;
}

// ----------------------------------------------------------------------------
//  Sidebar navigation item.
// ----------------------------------------------------------------------------
static bool SidebarItem(const char* label, bool selected)
{
    const float itemHeight = 40.0f;
    ImGui::InvisibleButton(label, ImVec2(ImGui::GetContentRegionAvail().x, itemHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();

    if (selected)
        dl->AddRectFilled(pMin, pMax, UI::U32(ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.16f)), 6.0f);
    else if (hovered)
        dl->AddRectFilled(pMin, pMax, UI::U32(ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.07f)), 6.0f);

    if (selected)
        dl->AddRectFilled(
            ImVec2(pMin.x, pMin.y + 8.0f),
            ImVec2(pMin.x + 3.0f, pMax.y - 8.0f),
            UI::U32(UI::kAccent), 2.0f);

    const ImU32 textColor = selected ? UI::U32(UI::kText) : UI::U32(UI::kTextMuted);
    const float textY = pMin.y + (itemHeight - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(pMin.x + 18.0f, textY), textColor, label);

    return clicked;
}

// ----------------------------------------------------------------------------
//  Status pill (rounded badge with a colored dot).
// ----------------------------------------------------------------------------
static void StatusBadge(AppStatus status)
{
    const char* label = StatusLabel(status);
    const ImVec4& dot = StatusColor(status);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float textW = ImGui::CalcTextSize(label).x;
    const float padX = 10.0f;
    const float dotR = 4.0f;
    const float height = 26.0f;
    const float width = textW + padX * 2.0f + dotR * 2.0f + 6.0f;

    dl->AddRectFilled(pos, pos + ImVec2(width, height), UI::U32(ImVec4(dot.x, dot.y, dot.z, 0.12f)), height * 0.5f);
    dl->AddRect(pos, pos + ImVec2(width, height), UI::U32(ImVec4(dot.x, dot.y, dot.z, 0.45f)), height * 0.5f, 0, 1.0f);
    dl->AddCircleFilled(ImVec2(pos.x + padX + dotR, pos.y + height * 0.5f), dotR, UI::U32(dot), 12);
    dl->AddText(ImVec2(pos.x + padX + dotR * 2.0f + 6.0f, pos.y + (height - ImGui::GetFontSize()) * 0.5f), UI::U32(UI::kText), label);

    ImGui::Dummy(ImVec2(width, height));
}

// ----------------------------------------------------------------------------
//  Card container with custom styled header. Always paired with EndCard().
// ----------------------------------------------------------------------------
static void BeginCard(const char* id, const char* title, const char* subtitle, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, UI::kSurface);
    ImGui::PushStyleColor(ImGuiCol_Border, UI::kBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

    ImGui::BeginChild(id, size, true);

    const ImVec2 cursor = ImGui::GetCursorPos();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(4, 16), UI::U32(UI::kAccent), 2.0f);

    ImGui::SetCursorPos(ImVec2(cursor.x + 12, cursor.y));
    ImGui::TextUnformatted(title);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, UI::kTextMuted);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
}

static void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ----------------------------------------------------------------------------
//  Slider rows with a dynamic readout rendered on the right side.
// ----------------------------------------------------------------------------
static bool SliderFloatRow(const char* label, float* value, float vMin, float vMax, const char* fmt, const char* suffix)
{
    char readout[64];
    std::snprintf(readout, sizeof(readout), fmt, *value);
    if (suffix && *suffix)
        std::snprintf(readout + std::strlen(readout), sizeof(readout) - std::strlen(readout), "%s", suffix);

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    const float readoutW = ImGui::CalcTextSize(readout).x + 12.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (availW > readoutW + 40.0f)
        ImGui::SetNextItemWidth(availW - readoutW);

    const bool changed = ImGui::SliderFloat("##value", value, vMin, vMax, fmt, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::TextColored(UI::kAccent, "%s", readout);

    ImGui::PopID();
    return changed;
}

static bool SliderIntRow(const char* label, int* value, int vMin, int vMax, const char* suffix)
{
    char readout[64];
    std::snprintf(readout, sizeof(readout), "%d%s", *value, suffix ? suffix : "");

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    const float readoutW = ImGui::CalcTextSize(readout).x + 12.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (availW > readoutW + 40.0f)
        ImGui::SetNextItemWidth(availW - readoutW);

    const bool changed = ImGui::SliderInt("##value", value, vMin, vMax, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::TextColored(UI::kAccent, "%s", readout);

    ImGui::PopID();
    return changed;
}

// ----------------------------------------------------------------------------
//  Page header with accent underline.
// ----------------------------------------------------------------------------
static void RenderPageHeader(const char* title, const char* subtitle)
{
    ImGui::TextUnformatted(title);
    ImGui::PushStyleColor(ImGuiCol_Text, UI::kTextMuted);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 8));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + availW, p.y + 1.0f), UI::U32(UI::kAccent), 0.5f);
    ImGui::Dummy(ImVec2(0, 14));
}

// ============================================================================
//  Tab contents
// ============================================================================

static void RenderAimbotTab(Config& cfg)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float colW = (availW - spacing) / 2.0f;

    BeginCard("##AimTargeting", "Targeting", "Target selection & key", ImVec2(colW, 0));
    {
        ToggleSwitch("##AimMaster", "Enable Aimbot", &cfg.aimbotEnabled);
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::TextUnformatted("Target Key");
        ImGui::SameLine();
        KeybindWidget("##AimKey", &cfg.aimbotKeybind);
        ImGui::Dummy(ImVec2(0, 2));

        ImGui::TextUnformatted("Target Selection");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        int target = static_cast<int>(cfg.aimTarget);
        if (ImGui::Combo("##AimTarget", &target, "Head\0Neck\0Chest\0Pelvis\0\0"))
            cfg.aimTarget = static_cast<AimTarget>(target);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##AimPrecision", "Precision", "FOV, smoothness & distance", ImVec2(colW, 0));
    {
        SliderFloatRow("Aim FOV", &cfg.aimFovDegrees, 1.0f, 360.0f, "%.1f", " deg");
        ImGui::Checkbox("Draw FOV Circle", &cfg.drawFovCircle);
        ImGui::Dummy(ImVec2(0, 2));

        SliderFloatRow("Smoothness", &cfg.aimSmoothness, 1.0f, 20.0f, "%.1f", "");
        ImGui::Dummy(ImVec2(0, 2));

        SliderIntRow("Max Distance", &cfg.aimMaxDistanceM, 10, 1000, " m");
    }
    EndCard();

    BeginCard("##AimChecks", "Checks & Behavior", "Line-of-sight, downed targets & RCS", ImVec2(availW, 0));
    {
        if (ImGui::Checkbox("Visibility Check", &cfg.visibilityCheck))
        {
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only engage targets that have a clear line of sight.");

        ImGui::SameLine(0, 24.0f);
        if (ImGui::Checkbox("Ignore Knocked", &cfg.ignoreKnocked))
        {
        }

        ImGui::SameLine(0, 24.0f);
        ToggleSwitch("##Rcs", "Recoil Control (RCS)", &cfg.recoilControl);

        if (cfg.recoilControl)
        {
            ImGui::Dummy(ImVec2(0, 6));
            SliderFloatRow("Recoil Strength", &cfg.recoilStrength, 0.1f, 10.0f, "%.1f", "");
            ImGui::TextUnformatted("Recoil Key");
            ImGui::SameLine();
            KeybindWidget("##RcsKey", &cfg.recoilKeybind);
        }
    }
    EndCard();
}

static void RenderTriggerbotTab(Config& cfg)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float colW = (availW - spacing) / 2.0f;

    BeginCard("##TbMain", "Triggerbot", "Automatic firing", ImVec2(colW, 0));
    {
        ToggleSwitch("##TbMaster", "Enable Triggerbot", &cfg.triggerbotEnabled);
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::TextUnformatted("Trigger Key");
        ImGui::SameLine();
        KeybindWidget("##TbKey", &cfg.triggerbotKeybind);
        ImGui::Dummy(ImVec2(0, 2));

        SliderFloatRow("Delay (ms)", &cfg.triggerbotDelayMs, 0.0f, 250.0f, "%.0f", "");
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##TbFilter", "Filtering", "Who triggers", ImVec2(colW, 0));
    {
        ImGui::Checkbox("Hold key to fire", &cfg.triggerbotHoldMode);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only fire while the trigger key is held down.");

        ImGui::Checkbox("Ignore Knocked", &cfg.triggerbotIgnoreKnocked);
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::PushStyleColor(ImGuiCol_Text, UI::kTextMuted);
        ImGui::TextWrapped("The triggerbot fires automatically when a valid target "
                           "enters the aim FOV while the trigger key is active.");
        ImGui::PopStyleColor();
    }
    EndCard();
}

static void RenderVisualsTab(Config& cfg)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float colW = (availW - spacing) / 2.0f;

    BeginCard("##Esp", "ESP", "World overlays", ImVec2(colW, 0));
    {
        ToggleSwitch("##EspMaster", "Enable Visuals", &cfg.visualsEnabled);
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::Checkbox("Boxes", &cfg.drawEspBoxes);
        ImGui::Checkbox("Health Bars", &cfg.drawHealthBars);
        ImGui::Checkbox("Player Names", &cfg.drawPlayerNames);
        ImGui::Checkbox("Snaplines", &cfg.drawSnaplines);

        SliderFloatRow("Box Thickness", &cfg.espBoxThickness, 0.5f, 4.0f, "%.1f", "px");

        ImGui::TextUnformatted("Box Color");
        ImGui::SameLine();
        ImVec4 boxColor = U32ToVec4(cfg.espBoxColor);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::ColorEdit4("##BoxColor", &boxColor.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip))
            cfg.espBoxColor = Vec4ToU32(boxColor);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##Overlay", "Overlay", "On-screen FOV indicator", ImVec2(colW, 0));
    {
        ImGui::Checkbox("Draw FOV Overlay", &cfg.drawFovOverlay);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the aim FOV circle on the game screen.");

        ImGui::TextUnformatted("Overlay Color");
        ImGui::SameLine();
        ImVec4 overlayColor = U32ToVec4(cfg.fovOverlayColor);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::ColorEdit4("##FovColor", &overlayColor.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip))
            cfg.fovOverlayColor = Vec4ToU32(overlayColor);

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleColor(ImGuiCol_Text, UI::kTextMuted);
        ImGui::TextWrapped("ESP boxes and health bars are drawn on top of the "
                           "game window using a transparent overlay.");
        ImGui::PopStyleColor();
    }
    EndCard();
}

static void RenderSettingsTab(Config& cfg)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float colW = (availW - spacing) / 2.0f;

    BeginCard("##SettingsGeneral", "General", "Application preferences", ImVec2(colW, 0));
    {
        SliderFloatRow("UI Scale", &cfg.uiScale, 0.8f, 1.6f, "%.2f", "x");
        ImGui::Checkbox("Show FPS", &cfg.showFps);
        ImGui::Checkbox("VSync", &cfg.vsync);
        ImGui::Checkbox("Launch on startup", &cfg.launchOnStartup);

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Menu Key");
        ImGui::SameLine();
        KeybindWidget("##MenuKey", &cfg.menuKey);
    }
    EndCard();

    ImGui::SameLine();

    BeginCard("##SettingsApp", "Application", "Status & actions", ImVec2(colW, 0));
    {
        ImGui::TextUnformatted("Status");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        int status = static_cast<int>(cfg.status);
        if (ImGui::Combo("##Status", &status, "Active\0Undetected\0Detected\0Warning\0\0"))
            cfg.status = static_cast<AppStatus>(status);
        ImGui::Dummy(ImVec2(0, 6));

        StatusBadge(cfg.status);

        ImGui::Dummy(ImVec2(0, 14));
        if (ImGui::Button("Reset to defaults", ImVec2(-1.0f, 0)))
            cfg = Config{};
    }
    EndCard();
}

// ============================================================================
//  Theme
// ============================================================================

void SetupCustomTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);

    // --- Colors ---
    s.Colors[ImGuiCol_WindowBg]            = UI::kBackground;
    s.Colors[ImGuiCol_ChildBg]             = UI::kBackground;
    s.Colors[ImGuiCol_PopupBg]             = UI::kSurface;
    s.Colors[ImGuiCol_Border]              = UI::kBorder;
    s.Colors[ImGuiCol_BorderShadow]        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    s.Colors[ImGuiCol_Text]                = UI::kText;
    s.Colors[ImGuiCol_TextDisabled]        = UI::kTextMuted;
    s.Colors[ImGuiCol_TextSelectedBg]      = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.25f);

    s.Colors[ImGuiCol_FrameBg]             = ImVec4(0.06f, 0.08f, 0.12f, 1.0f);
    s.Colors[ImGuiCol_FrameBgHovered]      = UI::kBorder;
    s.Colors[ImGuiCol_FrameBgActive]       = ImVec4(0.12f, 0.15f, 0.22f, 1.0f);

    s.Colors[ImGuiCol_TitleBg]             = UI::kSurface;
    s.Colors[ImGuiCol_TitleBgActive]       = UI::kSurface;
    s.Colors[ImGuiCol_TitleBgCollapsed]    = UI::kSurface;
    s.Colors[ImGuiCol_MenuBarBg]           = UI::kSurface;

    s.Colors[ImGuiCol_CheckMark]           = UI::kAccent;
    s.Colors[ImGuiCol_SliderGrab]          = UI::kAccent;
    s.Colors[ImGuiCol_SliderGrabActive]    = UI::kAccentHover;

    s.Colors[ImGuiCol_Button]              = ImVec4(0.08f, 0.10f, 0.15f, 1.0f);
    s.Colors[ImGuiCol_ButtonHovered]       = UI::kAccent;
    s.Colors[ImGuiCol_ButtonActive]        = UI::kAccentActive;

    s.Colors[ImGuiCol_Header]              = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.14f);
    s.Colors[ImGuiCol_HeaderHovered]       = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.22f);
    s.Colors[ImGuiCol_HeaderActive]        = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.30f);

    s.Colors[ImGuiCol_Separator]           = UI::kBorder;
    s.Colors[ImGuiCol_SeparatorHovered]    = UI::kAccent;
    s.Colors[ImGuiCol_SeparatorActive]     = UI::kAccent;

    s.Colors[ImGuiCol_Tab]                 = ImVec4(0.06f, 0.08f, 0.12f, 1.0f);
    s.Colors[ImGuiCol_TabHovered]          = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.18f);
    s.Colors[ImGuiCol_TabActive]           = UI::kAccent;
    s.Colors[ImGuiCol_TabUnfocused]        = ImVec4(0.06f, 0.08f, 0.12f, 1.0f);
    s.Colors[ImGuiCol_TabUnfocusedActive]  = UI::kAccentActive;

    s.Colors[ImGuiCol_PlotLines]           = UI::kAccent;
    s.Colors[ImGuiCol_PlotHistogram]       = UI::kAccent;

    s.Colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    s.Colors[ImGuiCol_ScrollbarGrab]       = UI::kBorder;
    s.Colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.5f);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = UI::kAccent;

    s.Colors[ImGuiCol_ResizeGrip]          = ImVec4(UI::kAccent.x, UI::kAccent.y, UI::kAccent.z, 0.25f);
    s.Colors[ImGuiCol_ResizeGripHovered]   = UI::kAccent;
    s.Colors[ImGuiCol_ResizeGripActive]    = UI::kAccentActive;

    s.Colors[ImGuiCol_WindowShadow]        = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);

    // --- Rounded corners ---
    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    // --- Spacing & padding ---
    s.WindowPadding      = ImVec2(14, 14);
    s.FramePadding       = ImVec2(8, 5);
    s.ItemSpacing        = ImVec2(10, 8);
    s.ItemInnerSpacing   = ImVec2(6, 6);
    s.CellPadding        = ImVec2(8, 6);

    // --- Borders ---
    s.WindowBorderSize   = 1.0f;
    s.ChildBorderSize    = 1.0f;
    s.PopupBorderSize    = 1.0f;
    s.FrameBorderSize    = 0.0f;
    s.TabBorderSize      = 0.0f;

    // --- Misc ---
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
}

// ============================================================================
//  Dashboard
// ============================================================================

void RenderDashboardUI(Config& cfg, bool& running)
{
    static int activeTab = 0;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    constexpr ImGuiWindowFlags winFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin("##Dashboard", &running, winFlags))
    {
        constexpr float kSidebarWidth = 210.0f;
        const ImVec2 windowAvail = ImGui::GetContentRegionAvail();

        // ---------------- Sidebar ----------------
        ImGui::PushStyleColor(ImGuiCol_ChildBg, UI::kSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, UI::kBorder);
        ImGui::BeginChild("##Sidebar", ImVec2(kSidebarWidth, windowAvail.y), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 16));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

            ImGui::TextColored(UI::kAccent, "PEKI");
            ImGui::TextColored(UI::kTextMuted, "MASTER");
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            static const char* const kTabs[] = { "Aimbot", "Triggerbot", "Visuals", "Settings" };
            for (int i = 0; i < 4; ++i)
            {
                if (SidebarItem(kTabs[i], activeTab == i))
                    activeTab = i;
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 112.0f);

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextColored(UI::kTextMuted, "STATUS");
            ImGui::Dummy(ImVec2(0, 6));

            StatusBadge(cfg.status);
            if (cfg.status == AppStatus::Active || cfg.status == AppStatus::Undetected)
                StatusBadge(AppStatus::Undetected);

            ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // ---------------- Main content ----------------
        ImGui::BeginChild("##Content", ImVec2(windowAvail.x - kSidebarWidth, windowAvail.y), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 12));

            static const char* const kTitles[] = { "Aimbot", "Triggerbot", "Visuals", "Settings" };
            static const char* const kSubtitles[] =
            {
                "Configure target acquisition, FOV and smoothing behavior.",
                "Configure automatic firing when a valid target is in range.",
                "ESP and on-screen overlay options.",
                "General application preferences."
            };
            RenderPageHeader(kTitles[activeTab], kSubtitles[activeTab]);

            switch (activeTab)
            {
            case 0: RenderAimbotTab(cfg);     break;
            case 1: RenderTriggerbotTab(cfg); break;
            case 2: RenderVisualsTab(cfg);    break;
            case 3: RenderSettingsTab(cfg);   break;
            default:                          break;
            }

            ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();
    }

    ImGui::PopStyleVar(2);
    ImGui::End();
}
