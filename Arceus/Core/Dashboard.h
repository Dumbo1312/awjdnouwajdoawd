#pragma once
#include <Windows.h>
#include <cstdint>

// ============================================================================
//  Dashboard.h
//  ---------------------------------------------------------------------------
//  Blurred.gg-style dark configuration dashboard built with Dear ImGui (1.90)
//  on top of Direct3D 11.
//
//  Public surface:
//    * Config                      - single source of truth for all UI state.
//    * SetupCustomTheme()          - apply the modern dark palette once after
//                                    ImGui::CreateContext().
//    * RenderDashboardUI(Config&)  - draw the full dashboard each frame.
//
//  The Config struct is intentionally free of any ImGui dependency so it can
//  be included directly by back-end feature code (aimbot, triggerbot, ESP...).
// ============================================================================

// ----------------------------------------------------------------------------
//  Target selection for the aimbot (combo box items).
// ----------------------------------------------------------------------------
enum class AimTarget : int
{
    Head = 0,
    Neck,
    Chest,
    Pelvis,
    Count
};

// ----------------------------------------------------------------------------
//  App status indicator rendered at the bottom of the sidebar.
// ----------------------------------------------------------------------------
enum class AppStatus : int
{
    Active = 0,      // green badge, e.g. "ACTIVE"
    Undetected,      // green badge, e.g. "UNDETECTED"
    Detected,        // red badge
    Warning          // amber badge
};

// ----------------------------------------------------------------------------
//  Config
//  ---------------------------------------------------------------------------
//  Holds every piece of UI state the dashboard exposes. A global instance
//  `g_Config` is provided for quick access, but back-ends may also keep their
//  own copy and pass it by reference to RenderDashboardUI().
// ----------------------------------------------------------------------------
struct Config
{
    // ---- Aimbot ------------------------------------------------------------
    bool        aimbotEnabled      = true;                 // Master toggle
    int         aimbotKeybind      = VK_RBUTTON;           // Target keybind
    AimTarget   aimTarget          = AimTarget::Head;      // Head / Neck / Chest / Pelvis
    float       aimFovDegrees      = 30.0f;                // 1.0 .. 360.0 degrees
    bool        drawFovCircle      = true;                 // "Draw FOV Circle"
    float       aimSmoothness      = 5.0f;                 // 1.0 .. 20.0 (step 0.1)
    int         aimMaxDistanceM    = 500;                  // 10 .. 1000 meters
    bool        visibilityCheck    = true;                 // Line-of-sight check
    bool        ignoreKnocked      = true;                 // Skip knocked/downed targets
    bool        recoilControl      = false;                // Recoil Control System (RCS)
    float       recoilStrength     = 1.0f;                 // Vertical compensation strength
    int         recoilKeybind      = VK_LBUTTON;           // Key while which RCS is active

    // ---- Triggerbot --------------------------------------------------------
    bool        triggerbotEnabled  = false;
    int         triggerbotKeybind  = VK_CONTROL;
    float       triggerbotDelayMs  = 10.0f;                // 0 .. 250 ms
    bool        triggerbotHoldMode = false;                // Require key held
    bool        triggerbotIgnoreKnocked = true;

    // ---- Visuals -----------------------------------------------------------
    bool        visualsEnabled     = true;
    bool        drawEspBoxes       = true;
    bool        drawHealthBars     = true;
    bool        drawPlayerNames    = true;
    bool        drawSnaplines      = false;
    bool        drawFovOverlay     = true;                 // In-game FOV circle overlay
    float       espBoxThickness    = 1.5f;                 // 0.5 .. 4.0
    std::uint32_t espBoxColor      = 0xFF00D4FF;           // AARRGGBB
    std::uint32_t fovOverlayColor  = 0xFFFFFFFF;           // AARRGGBB

    // ---- Settings ----------------------------------------------------------
    bool        showFps            = true;
    bool        vsync              = true;
    float       uiScale            = 1.0f;                 // 0.8 .. 1.6
    int         menuKey            = VK_F10;               // Toggle menu visibility
    bool        launchOnStartup    = false;
    AppStatus   status             = AppStatus::Active;
};

// Global instance so back-end features can read the current configuration.
extern Config g_Config;

// ----------------------------------------------------------------------------
//  Rendering API
// ----------------------------------------------------------------------------

// Applies the modern dark color palette (rounded corners, subtle borders and
// the indigo accent). Call ONCE after ImGui::CreateContext().
void SetupCustomTheme();

// Draws the complete dashboard: left navigation sidebar + right card layout.
// Call every frame inside the main render loop. Returns while the window is
// open; pass `running` if you want the close button to terminate the app.
void RenderDashboardUI(Config& cfg, bool& running);
