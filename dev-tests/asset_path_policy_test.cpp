#include "include/global/AssetPathPolicy.hpp"

#include <cassert>

int main() {
    const QString portableRoot = "D:/ThroneNext/data";
    const QString defaultRoot = "C:/Users/test/AppData/Local/Throne";
    const QString appDir = "D:/ThroneNext/app";

    // A custom -appdata root wins over the platform default and application dir.
    assert(Configs::AssetBasePath(portableRoot, true, defaultRoot, appDir) == portableRoot);
    // Packaged and legacy callers without a selected root retain their fallback.
    assert(Configs::AssetBasePath({}, true, defaultRoot, appDir) == defaultRoot);
    assert(Configs::AssetBasePath({}, false, defaultRoot, appDir) == appDir);
}
