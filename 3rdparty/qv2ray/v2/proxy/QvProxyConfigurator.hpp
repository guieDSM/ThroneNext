#pragma once
#include <QHostAddress>
#include <QObject>
#include <QString>
//
namespace Qv2ray::components::proxy {
    bool ClearSystemProxy();
    bool HasSystemProxyRecoveryJournal();
    bool RecoverLegacySystemProxy(int proxy_port, QString scheme);
    bool RecoverSystemProxy();
    bool SetSystemProxy(int http_port, int socks_port, QString scheme);
} // namespace Qv2ray::components::proxy

using namespace Qv2ray::components;
using namespace Qv2ray::components::proxy;
