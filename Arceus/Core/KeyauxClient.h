#pragma once
#include <string>
#include "ServerSecureClient.h"

namespace KeyauxClient
{
    bool Activate(const std::string& key);
    std::string GetLastMessage();
    std::string GetSavePath();
    std::string GetExpiry();
}
