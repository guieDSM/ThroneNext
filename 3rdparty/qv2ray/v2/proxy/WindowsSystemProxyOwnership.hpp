#pragma once

#include <QString>

namespace Qv2ray::components::proxy {

#ifdef Q_OS_WIN
    bool ApplyOwnedSystemProxy(const QString &proxyServer);
    bool HasOwnedSystemProxyJournal();
    bool RecoverLegacySystemProxy(const QString &expectedProxyServer);
    bool RestoreOwnedSystemProxy();
    bool RecoverOwnedSystemProxy();
#endif

} // namespace Qv2ray::components::proxy
