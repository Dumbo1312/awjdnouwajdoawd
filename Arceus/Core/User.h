#pragma once
#include <windows.h>
#include <string>
#include <array>
#include <vector>
#include <unordered_map>

enum class AppState { Login, StartMenu, Loading, Running};
enum class AimHeight { Chest, Neck, Head };
enum class InputMethod { Default, Win32, GHub, Razer, RPBox, Makcu };
enum class GameChoice { Fortnite, Valorant, CS2, RainbowSix, COD, Warzone, Rust, Custom };
enum class CaptureMethod { DesktopDuplication, WinRT, CaptureCard };

const std::unordered_map<AimHeight, float> AIMH2FLOAT
{
	{AimHeight::Chest,  0.35f},
	{AimHeight::Neck,   0.25f},
	{AimHeight::Head,   0.15f}
};

class User
{
public:
	struct BindsT {
		int aim = 0x00;
		int toggleAim = 0x00;
		int ads = 0x00;
		int toggleMenu = VK_F10;
	} Binds;

	struct NetworkT {
		int modelChoice = 0;
		std::string modelPath;
		std::vector<std::string> availableModels;
		float confidence = 0.4f;
		int size = -1;
		bool brutalMode = true;
		float thirdPersonMultiplier = 3.0f;
	} Network;

	struct InputT {
		InputMethod method = InputMethod::Default;
		GameChoice game = GameChoice::Fortnite;
		CaptureMethod capture = CaptureMethod::DesktopDuplication;

		// Capture Card
		int captureCardDeviceIndex = 0;

		// Custom Game
		std::string customGameName = "MyGame-Win64-Shipping";
	} Input;

	struct HipFireT {
		float strength = 0.4f;
		int fov = 200;
		float sens = 5.0f;
		int deadzone = 3;
		AimHeight aimHeightChoice = AimHeight::Head;
		float aimHeight = 0.15f;
	} HipFire;

	struct AdsT {
		float strength = 0.3f;
		int fov = 100;
		float sens = 50.0f;
		int deadzone = 3;
		AimHeight aimHeightChoice = AimHeight::Head;
		float aimHeight = 0.15f;
	} Ads;

	struct TriggerT {
		bool enabled = false;
		int proximity = 0;
		int bind = 0x00;
	} Trigger;

	struct PredictionT {
		bool enabled = false;
		float strength = 1.5f;
	} Prediction;

	struct RecoilT {
		bool enabled = false;
		float strength = 1.0f;
		int bind = VK_LBUTTON;
	} Recoil;

	struct AppearanceT {
		char accentHex[8] = "FFFFFF";
	} Appearance;

	struct InternalT { 
		bool toggledAim = false;
		int HF360 = 0;
		int ADS360 = 0;
		std::array<int, 4> Exclusion = {};
		std::array<int, 4> ExclusionT = {};
		float recoilAccum = 0.0f;
		int screenshotMs = 0;
		int inferenceMs = 0;
		int processingMs = 0;
		int ResolutionW = 0;
		int ResolutionH = 0;
		AppState appState = AppState::Loading;
	} Internal;

	User();
	~User();

	void UseSettings();
	void ScanForModels();
	void SwitchGame(GameChoice newGame);
	void SaveSettingsNow();
	void ResetToDefaults();
private:
	static std::wstring GetUserFile(GameChoice game);
	bool LoadSettings(const std::wstring UserFile);
	bool LoadFromBuffer(const unsigned char* data, std::size_t size);
	bool SaveSettings(const std::wstring UserFile) const;
};
