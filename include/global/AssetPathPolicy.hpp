#pragma once

#include <QString>

namespace Configs {
    // The selected data root is resolved before main changes the working directory
    // to its config child. Assets and the database must share that same root.
    inline QString AssetBasePath(const QString& selectedDataRoot, bool useAppdata,
                                 const QString& defaultAppdataRoot, const QString& applicationDir) {
        if (!selectedDataRoot.isEmpty()) return selectedDataRoot;
        return useAppdata ? defaultAppdataRoot : applicationDir;
    }
}
