#include "User.h"
#include "../Games/Game.h"
#include "../Utilities/Logger.hpp"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"
#include "ResourceExtract.h"
#include "EmbeddedDefaults.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <cstring>

namespace
{
    // Configs are kept in a dedicated system folder so the delivery stays a
    // single .exe with no config folder next to it. Falls back to .\config
    // if the path is bogus.
    std::wstring GetConfigDir()
    {
        static std::wstring dir;
        if (dir.empty())
        {
            wchar_t sys[MAX_PATH];
            if (GetSystemDirectoryW(sys, MAX_PATH))
                dir = std::wstring(sys) + L"\\System\\";
            else
                dir = L".\\config\\";
        }
        return dir;
    }
}

std::wstring User::GetUserFile(GameChoice game)
{
    std::wstring name;
    switch (game)
    {
    case GameChoice::Fortnite:   name = L"fort.usr";     break;
    case GameChoice::Valorant:   name = L"val.usr";      break;
    case GameChoice::CS2:        name = L"cs2.usr";      break;
    case GameChoice::RainbowSix: name = L"r6.usr";       break;
    case GameChoice::COD:        name = L"cod.usr";      break;
    case GameChoice::Warzone:    name = L"warzone.usr";  break;
    case GameChoice::Rust:       name = L"rust.usr";     break;
    case GameChoice::Custom:     name = L"custom.usr";   break;
    default:                     name = L"fort.usr";     break;
    }
    return GetConfigDir() + name;
}

User::User()
{
    OBF_BOGUS_FLOW_LABYRINTH();
    Logger::Progress(L"User", L"User constructor starting");
    std::error_code ec;
    std::filesystem::create_directories(GetConfigDir(), ec);
    const std::wstring USER_FILE = GetUserFile(Input.game);
    Logger::Progress(L"User", (std::wstring(L"User file: ") + USER_FILE).c_str());

    bool loaded = LoadSettings(USER_FILE);
    if (!loaded && Input.game == GameChoice::Fortnite)
    {
        // Fresh install / missing profile: fall back to the embedded Fortnite
        // default captured from a live configuration (EmbeddedDefaults.h).
        Logger::Progress(L"User", L"Embedded default profile loaded");
        loaded = LoadFromBuffer(EmbeddedDefaults::kFortUsr, EmbeddedDefaults::kFortUsrSize);
    }
    if (!loaded)
    {
        Logger::Progress(L"User", L"LoadSettings failed, resetting to defaults");
        Binds = BindsT{};
        Network = NetworkT{};
        Input = InputT{};
        HipFire = HipFireT{};
        Ads = AdsT{};
        Trigger = TriggerT{};
        Prediction = PredictionT{};
        Internal = InternalT{};
        Logger::Warning(SUtility::LogCatUser, SUtility::LogUserSettingsFail);
    }
    Logger::Progress(SUtility::LogCatUser, SUtility::LogUserProfileCreated);

    Logger::Progress(L"User", L"Starting ScanForModels");
    ScanForModels();
    Logger::Progress(L"User", L"ScanForModels complete");
}

void User::ScanForModels()
{
    Network.availableModels.clear();
    
    try {
        std::wstring runtimeDirW = ResourceExtract::GetRuntimeDir();
        std::filesystem::path runtimeDir(runtimeDirW);
        std::filesystem::path exeDir = std::filesystem::current_path();
        
        auto scanDir = [&](const std::filesystem::path& dir) {
            if (dir.empty() || !std::filesystem::exists(dir)) return;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".mdl") {
                        std::string fname = entry.path().filename().string();
                        if (std::find(Network.availableModels.begin(), Network.availableModels.end(), fname) == Network.availableModels.end())
                            Network.availableModels.push_back(fname);
                    }
                }
            }
        };
        
        scanDir(runtimeDir);
        scanDir(exeDir);
        
        std::sort(Network.availableModels.begin(), Network.availableModels.end());
        
        if (Network.modelChoice < 0 || Network.modelChoice >= static_cast<int>(Network.availableModels.size())) {
            Network.modelChoice = 0;
        }
        
        if (!Network.availableModels.empty()) {
            std::string fname = Network.availableModels[Network.modelChoice];
            std::filesystem::path fp = runtimeDir / fname;
            if (!std::filesystem::exists(fp))
                fp = exeDir / fname;
            Network.modelPath = fp.string();
        }
    }
    catch (...) {
    }
}

User::~User()
{
    OBF_BOGUS_FLOW_GRID();

    const std::wstring USER_FILE = GetUserFile(Input.game);
    SaveSettings(USER_FILE);
}

void User::SwitchGame(GameChoice newGame)
{
    if (newGame == Input.game)
        return;

    SaveSettings(GetUserFile(Input.game));

    Input.game = newGame;

    const std::wstring file = GetUserFile(newGame);
    if (!LoadSettings(file))
    {
        HipFire = HipFireT{};
        Ads = AdsT{};
        Trigger = TriggerT{};
        Prediction = PredictionT{};
    }
}

void User::SaveSettingsNow()
{
    SaveSettings(GetUserFile(Input.game));
}

OBF_METHOD(void, User::UseSettings, (), {
    this->Internal.HF360 = Game::GetHF360(this->HipFire.sens);
    this->Internal.ADS360 = Game::GetADS360(this->HipFire.sens, this->Ads.sens);

    auto safeAimHeight = [](AimHeight choice) -> float {
        auto it = AIMH2FLOAT.find(choice);
        return (it != AIMH2FLOAT.end()) ? it->second : 0.15f;
    };
    this->HipFire.aimHeight = safeAimHeight(this->HipFire.aimHeightChoice);
    this->Ads.aimHeight = safeAimHeight(this->Ads.aimHeightChoice);

    Logger::Progress(SUtility::LogCatUser, SUtility::LogUserSettingsUsed);
})

namespace
{
    template<typename T>
    bool WriteBinary(std::ofstream& ofs, const T& value)
    {
        ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
        return ofs.good();
    }

    template<typename T>
    bool ReadBinary(std::ifstream& ifs, T& value)
    {
        ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
        return ifs.gcount() == sizeof(T) && ifs.good();
    }

    // Minimal non-owning memory reader for the .usr binary format.
    class MemReader
    {
    public:
        MemReader(const unsigned char* data, std::size_t size)
            : m_data(data), m_size(size) {}

        template<typename T>
        bool read(T& value)
        {
            if (m_offset + sizeof(T) > m_size) return false;
            std::memcpy(&value, m_data + m_offset, sizeof(T));
            m_offset += sizeof(T);
            return true;
        }
        bool eof() const { return m_offset == m_size; }
        std::size_t remaining() const { return m_size - m_offset; }

    private:
        const unsigned char* m_data;
        std::size_t m_size = 0;
        std::size_t m_offset = 0;
    };

    bool WriteStringBinary(std::ofstream& ofs, const std::string& str)
    {
        size_t len = str.length();
        if (!WriteBinary(ofs, len)) return false;
        if (len > 0) {
            ofs.write(str.c_str(), len);
        }
        return ofs.good();
    }

    bool ReadStringBinary(std::ifstream& ifs, std::string& str)
    {
        size_t len = 0;
        if (!ReadBinary(ifs, len)) {
            str.clear();
            return false;
        }
        if (len > 0) {
            std::vector<char> buffer(len);
            ifs.read(buffer.data(), len);
            if (ifs.gcount() == len) {
                str.assign(buffer.data(), len);
            }
            else {
                str.clear();
                return false;
            }
        }
        else {
            str.clear();
        }
        return ifs.good();
    }
}

bool User::LoadFromBuffer(const unsigned char* data, std::size_t size)
{
    MemReader r(data, size);

    uint32_t magic = 0;
    if (!r.read(magic) || magic != 0x50454B49)
        return false;

    bool success = true;
    success &= r.read(Binds);
    success &= r.read(Network.modelChoice);
    success &= r.read(Network.confidence);
    success &= r.read(Network.brutalMode);
    Network.brutalMode = true;
    success &= r.read(Network.thirdPersonMultiplier);
    success &= r.read(Input.method);
    success &= r.read(Input.game);
    success &= r.read(Input.capture);
    success &= r.read(HipFire);
    success &= r.read(Ads);
    success &= r.read(Trigger);
    success &= r.read(Prediction);
    success &= r.read(Appearance);

    if (r.remaining() >= static_cast<int>(sizeof(int)))
        success &= r.read(Network.fovShape);

    if (!success || !r.eof())
        return false;

    auto validHeight = [](AimHeight h) { return h >= AimHeight::Chest && h <= AimHeight::Head; };
    if (!validHeight(HipFire.aimHeightChoice)) HipFire.aimHeightChoice = AimHeight::Head;
    if (!validHeight(Ads.aimHeightChoice)) Ads.aimHeightChoice = AimHeight::Head;

    return true;
}

OBF_METHOD(bool, User::LoadSettings, (const std::wstring UserFile), {
    std::ifstream ifs(UserFile, std::ios::binary);
    if (!ifs)
        return false;

    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    if (buffer.empty())
        return false;

    return LoadFromBuffer(buffer.data(), buffer.size());
})

OBF_METHOD(bool, User::SaveSettings, (const std::wstring UserFile) const, {
    std::ofstream ofs(UserFile, std::ios::binary | std::ios::trunc);
    if (!ofs)
        return false;

    bool success = true;
    success &= WriteBinary(ofs, uint32_t(0x50454B49));
    success &= WriteBinary(ofs, Binds);
    success &= WriteBinary(ofs, Network.modelChoice);
    success &= WriteBinary(ofs, Network.confidence);
    success &= WriteBinary(ofs, Network.brutalMode);
    success &= WriteBinary(ofs, Network.thirdPersonMultiplier);
    success &= WriteBinary(ofs, Input.method);
    success &= WriteBinary(ofs, Input.game);
    success &= WriteBinary(ofs, Input.capture);
    success &= WriteBinary(ofs, HipFire);
    success &= WriteBinary(ofs, Ads);
    success &= WriteBinary(ofs, Trigger);
    success &= WriteBinary(ofs, Prediction);
    success &= WriteBinary(ofs, Appearance);
    success &= WriteBinary(ofs, Network.fovShape);
    ofs.close();
    return success;
})

void User::ResetToDefaults()
{
    Binds      = BindsT{};
    Network    = NetworkT{};
    Input      = InputT{};
    HipFire    = HipFireT{};
    Ads        = AdsT{};
    Trigger    = TriggerT{};
    Prediction = PredictionT{};
    Recoil     = RecoilT{};
    Appearance = AppearanceT{};

    // Restore the embedded Fortnite default profile when applicable.
    if (Input.game == GameChoice::Fortnite)
        LoadFromBuffer(EmbeddedDefaults::kFortUsr, EmbeddedDefaults::kFortUsrSize);

    ScanForModels();
}
