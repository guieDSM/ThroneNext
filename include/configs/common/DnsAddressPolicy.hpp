#pragma once
#include <QString>
#include <QUrl>

namespace Configs {
    // The legacy migration wrote a UDP resolver IP as a TLS endpoint. Yandex
    // DoT requires its service hostname for TLS SNI and certificate validation.
    inline QString normalizeLegacyDnsAddress(const QString &address) {
        QUrl url(address);
        if (url.scheme() != "tls" || !url.userInfo().isEmpty()
            || url.hasQuery() || url.hasFragment()
            || (!url.path().isEmpty() && url.path() != "/")) return address;
        if (url.host() != "77.88.8.8" && url.host() != "77.88.8.1") return address;
        url.setHost("common.dot.dns.yandex.net");
        return url.toString();
    }
}
