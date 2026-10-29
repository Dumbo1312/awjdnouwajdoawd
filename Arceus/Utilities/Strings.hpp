#pragma once
#include <engine/qengine.hpp>

static std::wstring RandomWString(std::size_t length)
{
	const qengine::qtype_enc::qe_wstring charset(QSTR(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"));
	std::random_device rd;
	std::mt19937 generator(rd());
	std::uniform_int_distribution<std::size_t> distribution(0, charset.get().size() - 1);

	std::wstring random_wstring;
	random_wstring.reserve(length);

	for (std::size_t i = 0; i < length; ++i)
		random_wstring += charset.get()[distribution(generator)];

	return random_wstring;
}

namespace SDetector
{
	using namespace qengine::qtype_enc;

	inline const qe_string ErrorD3D12Context(QSTR("Could not create the DirectX12 context"));
	inline const qe_string ErrorStructuredBuffer(QSTR("Could not create Structured Buffer"));
	inline const qe_string ErrorHeapDescriptor(QSTR("Could not create Heap and Descriptors"));
	inline const qe_string ErrorRootSignature(QSTR("Could not create Root Signature"));
	inline const qe_string ErrorComputePipeline(QSTR("Could not create Compute Pipeline"));

	inline const qe_string ExecutionProvider(QSTR("DML"));
	inline const qe_string OptTrue(QSTR("true"));
	inline const qe_string OptFalse(QSTR("false"));
	inline const qe_string Opt0(QSTR("0"));
	inline const qe_string Opt1(QSTR("1"));
	inline const qe_string OptDisableCPUFallback(QSTR("session.disable_cpu_ep_fallback"));
	inline const qe_string OptEnableCPUMemArena(QSTR("session.enable_cpu_mem_arena"));
	inline const qe_string OptEnableGraphCapture(QSTR("execution_provider.dml.enable_graph_capture"));
	inline const qe_string OptEnableDynamicGraphFusion(QSTR("execution_provider.dml.enable_dynamic_graph_fusion"));
	inline const qe_string OptDisableMetaCommands(QSTR("execution_provider.dml.disable_metacommands"));
	inline const qe_string OptPerformancePreference(QSTR("execution_provider.dml.performance_preference"));
	inline const qe_string OptHighPerformance(QSTR("high_performance"));
	inline const qe_string OptDisableGPUMemArena(QSTR("execution_provider.dml.disable_memory_arena"));
	inline const qe_string OptEnableCPUSyncSpinning(QSTR("execution_provider.dml.enable_cpu_sync_spinning"));

	inline const qe_string ShaderEntryPoint(QSTR("CSMain"));
	inline const qe_string ShaderModel(QSTR("cs_5_0"));

	inline const qe_string ShaderPreprocessing(QSTR(R"(
	cbuffer PreprocessingParams : register(b0)
	{
		uint network_size;
	};

	Texture2D<float4> InputTexture : register(t0);
	RWStructuredBuffer<float> PlanarOutput : register(u0);

	[numthreads(16, 16, 1)]
	void CSMain(uint3 id : SV_DispatchThreadID)
	{
		if (id.x >= network_size || id.y >= network_size)
		{
			return;
		}

		float4 bgra_color = InputTexture.Load(int3(id.xy, 0));
		float b = bgra_color.x;
		float g = bgra_color.y;
		float r = bgra_color.z;

		uint plane_size = network_size * network_size;
		uint pixel_offset = id.y * network_size + id.x;

		PlanarOutput[plane_size * 0 + pixel_offset] = b;
		PlanarOutput[plane_size * 1 + pixel_offset] = g;
		PlanarOutput[plane_size * 2 + pixel_offset] = r;
	}
	)"));

	inline const qe_wstring LogCat(QSTR(L"DETECTOR"));
	inline const qe_wstring LogOptimizations(QSTR(L"Added optimizations"));
	inline const qe_wstring LogDirectMLProvider(QSTR(L"Appended DirectML execution provider"));
	inline const qe_wstring LogModelRead(QSTR(L"Read the model"));
	inline const qe_wstring LogSessionCreated(QSTR(L"Created session"));
	inline const qe_wstring LogIONodeDetails(QSTR(L"Defined input/output node details"));
	inline const qe_wstring LogD3D12Device(QSTR(L"Created D3D12 device"));
	inline const qe_wstring LogCmdQueueAllocList(QSTR(L"Creted command queue, allocator and list"));
	inline const qe_wstring LogDirectMLDevice(QSTR(L"Created DirectML device"));
	inline const qe_wstring LogD3D12Buffer(QSTR(L"Created D3D12 buffer"));
	inline const qe_wstring LogDescriptorsHeap(QSTR(L"Created descriptors and heap"));
	inline const qe_wstring LogFenceCreated(QSTR(L"Created fence"));
	inline const qe_wstring LogRootSignature(QSTR(L"Created root signature"));
	inline const qe_wstring LogShaderCompiled(QSTR(L"Compiled shader"));
	inline const qe_wstring LogComputePipeline(QSTR(L"Created compute pipeline"));
	inline const qe_wstring LogOutputBinded(QSTR(L"Binded output"));
	inline const qe_wstring LogInputBinded(QSTR(L"Binded input"));
}

namespace SScreenCapture
{
	using namespace qengine::qtype_enc;

	inline const qe_string DDDevCreation(QSTR("[DesktopDuplication] Failed device creation"));
	inline const qe_string DDDxgiDeviceFailed(QSTR("[DesktopDuplication] Failed dxgi device"));
	inline const qe_string DDDxgiAdapterFailed(QSTR("[DesktopDuplication] Failed dxgi adapter"));
	inline const qe_string DDOutputFailed(QSTR("[DesktopDuplication] Failed output"));
	inline const qe_string DDDescriptionFailed(QSTR("[DesktopDuplication] Failed description"));
	inline const qe_string DDDesktopDuplicationFailed(QSTR("[DesktopDuplication] Failed desktop duplication"));
	inline const qe_string DDFrameReleaseFailed(QSTR("[DesktopDuplication] Failed frame release"));
	inline const qe_string DDFailedToAcquireFrame(QSTR("[DesktopDuplication] Failed to acquire frame"));
	inline const qe_string DDQueryInterfaceFail(QSTR("[DesktopDuplication] QueryInterface fail"));
	inline const qe_string DDSharedTexture(QSTR("[DesktopDuplication] Failed to create shared texture"));
	inline const qe_string DDSharedInterface(QSTR("Failed to query shared resource interface"));
	inline const qe_string DDSharedHandle(QSTR("Failed to get shared handle"));
	inline const qe_string DDRecreationLimit(QSTR("Could not recreate DesktopDuplication context"));

	inline const qe_wstring LogCat(QSTR(L"SCREENCAPTURE"));
	inline const qe_wstring LogD3D11Device(QSTR(L"Created D3D11 device"));
	inline const qe_wstring LogDXGIDevice(QSTR(L"Got DXGI device"));
	inline const qe_wstring LogDXGIAdapter(QSTR(L"Got DXGI adapter"));
	inline const qe_wstring LogPrimaryDisplay(QSTR(L"Got primary display"));
	inline const qe_wstring LogDisplayInfo(QSTR(L"Got display information"));
	inline const qe_wstring LogDuplicationInterface(QSTR(L"Created duplication interface"));
	inline const qe_wstring LogCaptureArea(QSTR(L"Calculated capture area"));
	inline const qe_wstring LogSharedTexture(QSTR(L"Created shared texture"));
	inline const qe_wstring LogSharedHandle(QSTR(L"Got shared handle"));
	inline const qe_wstring LogRecreation(QSTR(L"Recreated DD with a resolution of: "));
	inline const qe_wstring LogCreation(QSTR(L"Created DD with a resolution of: "));
}

namespace SMenu
{
	using namespace qengine::qtype_enc;

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
	inline const qe_string Version(QSTR("v" STRINGIFY(VERSION)));

	inline const qe_string HipFireTab(QSTR("HIP FIRE"));
	inline const qe_string AdsTab(QSTR("ADS"));
	inline const qe_string ScopedTab(QSTR("SCOPED"));
	inline const qe_string Strength(QSTR("Strength"));
	inline const qe_string FOV(QSTR("FOV"));

	inline const qe_string AITab(QSTR("AI"));
	inline const qe_string ConfidenceThreshold(QSTR("Confidence Threshold"));
	inline const qe_string BrutalMode(QSTR("Brutal Mode"));
	inline const qe_string AimHeight(QSTR("Aim Height"));
	inline const qe_string CBBChest(QSTR("Chest"));
	inline const qe_string CBBNeck(QSTR("Neck"));
	inline const qe_string CBBHead(QSTR("Head"));

	inline const qe_string TriggerTab(QSTR("TRIGGER"));
	inline const qe_string Trigger(QSTR("Trigger"));
	inline const qe_string ProximityControl(QSTR("Proximity Control"));

	inline const qe_string KeybindsTab(QSTR("KEYBINDS"));
	inline const qe_string HoldAim(QSTR("Hold Aim"));
	inline const qe_string ToggleAim(QSTR("Toggle Aim"));
	inline const qe_string HideMenu(QSTR("Hide Menu"));
	inline const qe_string AdsBind(QSTR("ADS Bind"));

	inline const qe_string SensitivityTab(QSTR("SENSITIVITY"));
	inline const qe_string XAndY(QSTR("X and Y"));
	inline const qe_string AdsSens(QSTR("ADS"));
	inline const qe_string ScopedSens(QSTR("Scoped"));

	inline const qe_string SubscriptionTab(QSTR("SUBSCRIPTION"));
	inline const qe_string ExpiresOn(QSTR("Expires in %s"));

	inline const qe_string InformationTab(QSTR("INFORMATION"));
	inline const qe_string Screenshot(QSTR("Screenshot %dms"));
	inline const qe_string Inference(QSTR("Inference %dms"));
	inline const qe_string Processing(QSTR("Processing %dms"));
	inline const qe_string Resolution(QSTR("Resolution %dx%d"));

	inline const qe_string StartMenu(QSTR("START MENU"));
	inline const qe_string ModelChoice(QSTR("Model"));

	inline const qe_string Discord(QSTR("https://discord.gg/FntYdwbm8m"));

	inline const qe_wstring LogCat(QSTR(L"MENU"));
	inline const qe_wstring LogMenuThread(QSTR(L"Started menu thread"));
	inline const qe_wstring LogWindowClass(QSTR(L"Registered window class"));
	inline const qe_wstring LogWindowCreated(QSTR(L"Created window"));
	inline const qe_wstring LogD3DDevice(QSTR(L"Created D3D device"));
	inline const qe_wstring LogImGuiContext(QSTR(L"Created ImGui context"));
	inline const qe_wstring LogFontsLoaded(QSTR(L"Loaded fonts"));
	inline const qe_wstring LogImGuiBackends(QSTR(L"Started ImGui backends"));
	inline const qe_wstring LogLogoLoaded(QSTR(L"Loaded logo"));
}

namespace SGame
{
	using namespace qengine::qtype_enc;

	inline const qe_string  ModelDot			(QSTR(".dot"));
	inline const qe_string  ErrorModelGet		(QSTR("Could not get model"));
	inline const qe_string  ErrorModelInv		(QSTR("Invalid model"));

#ifdef FORTNITE
	inline const qe_string  Version				(QSTR("Fortnite"));
	inline const qe_wstring UserFile			(QSTR(L"fort.usr"));
	inline const qe_wstring ACName				(QSTR(L"EasyAntiCheat_EOS.exe"));

	inline const qe_string  ModelNameFast		(QSTR("Fast"));
	inline const qe_string  ModelNameAccurate	(QSTR("Accurate"));
	inline const qe_string  ModelNameCasual		(QSTR("Casual"));

	inline const qe_string  DownloadModels		(QSTR("Models/Fortnite/"));
	inline const qe_string  ModelFileFast		(QSTR("fort.fast"));
	inline const qe_string  ModelFileAccurate	(QSTR("fort.accu"));
	inline const qe_string  ModelFileCasual		(QSTR("fort.casu"));

#elif defined(VALORANT)
	inline const qe_string  Version				(QSTR("Valorant"));
	inline const qe_wstring UserFile			(QSTR(L"valorant.usr"));
	inline const qe_wstring ACName				(QSTR(L"vgc.exe"));

	inline const qe_string  ModelNameRedOut		(QSTR("Red Outlines"));

	inline const qe_string  DownloadModels		(QSTR("Models/Valorant/"));
	inline const qe_string  ModelFileRedOut		(QSTR("val_redout.mdl"));

#elif defined(MARVELRIVALS)
	inline const qe_string  Version				(QSTR("MarvelRivals"));
	inline const qe_wstring UserFile			(QSTR(L"marvel.usr"));
	inline const qe_wstring ACName				(QSTR(L""));

	inline const qe_string  DownloadModels		(QSTR("Models/MarvelRivals/"));

#else
	inline const qe_string  Version				(QSTR("Default"));
	inline const qe_wstring UserFile			(QSTR(L"default.usr"));
	inline const qe_wstring ACName				(QSTR(L""));

	inline const qe_string  ModelNameFast		(QSTR("Fast"));
	inline const qe_string  ModelNameAccurate	(QSTR("Accurate"));
	inline const qe_string  ModelNameCasual		(QSTR("Casual"));
	inline const qe_string  ModelNameRedOut		(QSTR("Default"));

	inline const qe_string  DownloadModels		(QSTR("Models/Default/"));
	inline const qe_string  ModelFileFast		(QSTR("default.fast"));
	inline const qe_string  ModelFileAccurate	(QSTR("default.accu"));
	inline const qe_string  ModelFileCasual		(QSTR("default.casu"));
	inline const qe_string  ModelFileRedOut		(QSTR("default.mdl"));
#endif

}

namespace SUtility
{
	using namespace qengine::qtype_enc;

	inline const qe_wstring LogFile(QSTR(L"seny.log"));
	inline const qe_wstring LogProgress(QSTR(L"PROGRESS"));
	inline const qe_wstring LogWarning(QSTR(L"WARNING"));

	inline const qe_wstring LogCatUser(QSTR(L"USER"));
	inline const qe_wstring LogUserSettingsFail(QSTR(L"Could not load user settings"));
	inline const qe_wstring LogUserProfileCreated(QSTR(L"Created user profile"));
	inline const qe_wstring LogUserSettingsUsed(QSTR(L"Utilized user settings"));
	inline const qe_wstring LogUserDependencies(QSTR(L"Downloaded required dependencies"));

	inline const qe_string DownloadDependencies(QSTR("Dependencies/"));
	inline const qe_string OnnxRuntime(QSTR("onnxruntime.dll"));
	inline const qe_string DirectML(QSTR("DirectML.dll"));
}

namespace SSecurity
{
	using namespace qengine::qtype_enc;
	
	inline const qe_string NamedMutex(QSTR("Global\\{E0B2EE49-C686-4438-8657-AC8B8E50A2FF}"));
	inline const qe_string AlreadyOpenErr(QSTR("Seny is already open"));

	inline const qe_string EncPass(QSTR("system"));
	inline const qe_string DecErr(QSTR("Could not decrypt the buffer"));

	inline const qe_wstring CloseAC(QSTR(L"For safety close "));
	inline const qe_wstring ProcEAC(QSTR(L"EasyAntiCheat_EOS.exe"));
	inline const qe_wstring ProcVanguard(QSTR(L"vgc.exe"));
	inline const qe_wstring ProcVanguardTray(QSTR(L"vgtray.exe"));
	inline const qe_wstring ProcMarRivLauncher(QSTR(L"MarvelRivals_Launcher.exe"));

	inline const qe_wstring LogCat(QSTR(L"SECURITY"));
	inline const qe_wstring LogManipulatedSections(QSTR(L"Manipulated executable"));
	inline const qe_wstring LogAnticheatCheck(QSTR(L"Checked for anticheats"));
}

namespace SAuth
{
	using namespace qengine::qtype_enc;

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
	inline const qe_string License(QSTR(STRINGIFY(LICENSE)));

	inline qe_string Subscription;

	inline const qe_wstring LogCat(QSTR(L"AUTH"));
	inline const qe_wstring LogUserAuthenticated(QSTR(L"User authenticated"));
}

namespace SIOManagement
{
	using namespace qengine::qtype_enc;

	inline const qe_wstring LogCat(QSTR(L"IO"));

	inline qe_wstring CmdInstall(QSTR(L"\" install -f"));
	inline qe_wstring CmdAttach(QSTR(L"\" attach -r 127.0.0.1 -b 1"));

	inline qe_wstring ClientPath(QSTR(L"USBS"));
	inline qe_wstring VBoxMonPath(QSTR(L"drivers"));
	inline qe_wstring VBoxPath(QSTR(L"VirtualBox"));

	inline const qe_string DownloadUSBClient(QSTR("USBClient/"));
	inline const qe_string DownloadUSBServer(QSTR("USBServer/"));

	inline const qe_string VhciInf(QSTR("usbip_vhci_ude.inf"));
	inline const qe_string VhciCat(QSTR("usbip_vhci_ude.cat"));
	inline const qe_string VhciSys(QSTR("usbip_vhci_ude.sys"));
	inline const qe_string Attacher(QSTR("attacher.exe"));
	inline const qe_string Usbip(QSTR("usbip.exe"));
	inline const qe_string VboxInf(QSTR("VBoxUSB.inf"));
	inline const qe_string VboxCat(QSTR("VBoxUSB.cat"));
	inline const qe_string VboxSys(QSTR("VBoxUSB.sys"));
	inline const qe_string VboxMon(QSTR("VBoxUSBMon.sys"));

	inline const qe_string VMException(QSTR("Virtual Mouse: "));
	inline const qe_string SetUpErr1(QSTR("8"));
	inline const qe_string SetUpErr2(QSTR("9"));
	inline const qe_string ErrClient(QSTR("10"));

	inline const qe_wstring LogFiles(QSTR(L"Downloaded all required files"));
	inline const qe_wstring LogSetUp(QSTR(L"Finished required set up"));
	inline const qe_wstring LogInstall(QSTR(L"Installed Virtual Mouse"));
	inline const qe_wstring LogStart(QSTR(L"Started Virtual Mouse"));


	inline const qe_string DownloadInception(QSTR("Inception/"));
	inline const qe_string WinDivDll(QSTR("WinDivert.dll"));
	inline const qe_string WinDivSys(QSTR("WinDivert64.sys"));

	inline const qe_string INCException(QSTR("Inception: "));

	inline const qe_wstring ILogStart(QSTR(L"Started Inception"));
}