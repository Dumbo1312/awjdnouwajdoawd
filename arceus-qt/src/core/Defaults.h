#pragma once
#include <QVector>
#include "Preset.h"
#include "Provider.h"

namespace arceus {

class Settings;

namespace Defaults {
    QVector<Provider> providers();
    QVector<Preset> presets(const Settings *s);
}

}
