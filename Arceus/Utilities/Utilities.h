#pragma once
#include <Windows.h>
#include <string>
#include <filesystem>
#include <chrono>
#include "../Core/Target.h"

#include <array>
#include <cstring>

class TargetVector
{
private:
    static constexpr size_t CAPACITY = 64;
    std::array<Target, CAPACITY> m_data;
    size_t m_size = 0;

public:
    void clear() { m_size = 0; }

    void push_back(const Target& val)
    {
        if (m_size < CAPACITY)
            m_data[m_size++] = val;
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        if (m_size < CAPACITY)
            m_data[m_size++] = Target(std::forward<Args>(args)...);
    }

    void insert(Target* pos, const Target* first, const Target* last)
    {
        size_t count = static_cast<size_t>(last - first);
        if (count == 0) return;

        if (m_size + count > CAPACITY)
            count = CAPACITY - m_size;

        std::memcpy(&m_data[m_size], first, count * sizeof(Target));

        m_size += count;
    }

    Target* begin() { return m_data.data(); }
    Target* end() { return m_data.data() + m_size; }
    const Target* begin() const { return m_data.data(); }
    const Target* end() const { return m_data.data() + m_size; }

    bool empty() const { return m_size == 0; }
    size_t size() const { return m_size; }
    static constexpr size_t capacity() { return CAPACITY; }

    Target& operator[](size_t i) { return m_data[i]; }
    const Target& operator[](size_t i) const { return m_data[i]; }

    Target* data() { return m_data.data(); }
    const Target* data() const { return m_data.data(); }
};

namespace Utility 
{
	std::filesystem::path GetExecutableDirectory();
	bool FileExists(const std::filesystem::path& dir, const std::string& filename);
	bool LoadFileToVector(const std::wstring& path, std::vector<unsigned char>& outData);
	void SaveFileToDisk(std::filesystem::path path, const std::string fileName, const std::vector<unsigned char>& bytes);

	bool DecryptBuffer(const void* encryptedBuffer, size_t encryptedSize, std::vector<BYTE>& decryptedBuffer);

	float ComputeIoU(const Target& a, const Target& b);
	void PerformNMS(TargetVector& candidates, const float nms_threshold);
}

namespace Security
{
	void SetSecurity();

	void CheckSecurity();
}

namespace LicenseServer
{
	void Authenticate();

	void CheckHeartBeat();

	std::vector<unsigned char> DownloadFileToMemory(std::string fileName);

	bool License(std::string key);
	std::string GetMessage();
	std::string GetSubscription();
	
	bool TryAutoLogin(std::string& username, std::string& password, std::string& key);
	void SaveCredentials(bool save, const std::string& username, const std::string& password, const std::string& key);
	void ClearSavedCredentials();
}

namespace Timer
{
	void Start();

	void Measure(int& milliseconds);
}