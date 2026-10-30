#pragma once
#include "ApiClient.h"
#include "core/Provider.h"

namespace arceus {

class ApiRouter {
public:
    static ApiClient *make(const Provider &p, QObject *parent);
};

}
