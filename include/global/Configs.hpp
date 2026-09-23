#pragma once

#include "Const.hpp"
#include "Utils.hpp"
#include "include/database/DatabaseManager.h"

// Carried over from srslist.h, which used to be included here.
#include <array>
#include <string_view>
#include <utility>

// Switch core support

namespace Configs {
    void initDB(const std::string& dbPath);

    QString FindCoreRealPath();

    bool IsAdmin(bool forceRenew=false);

    bool isSetuidSet(const std::string& path);

    QString GetBasePath();
} // namespace Configs

#define ROUTES_PREFIX_NAME QString("route_profiles")
#define ROUTES_PREFIX QString(ROUTES_PREFIX_NAME + "/")
