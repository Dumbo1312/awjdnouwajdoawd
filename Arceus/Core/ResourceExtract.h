#pragma once
#include <string>

namespace ResourceExtract
{
    void ForceExtractNow();
    bool ExtractAll();
    const std::wstring& GetRuntimeDir();
    bool ExtractEmbeddedModel();
    void CleanupFiles();
    void CleanupDir();
}
