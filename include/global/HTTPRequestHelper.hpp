#pragma once

#include <QObject>
#include <QNetworkReply>
#include <functional>

namespace Configs_network {
    struct HTTPResponse {
        QString error;
        QByteArray data;
        QList<QPair<QByteArray, QByteArray>> header;
        QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    };

    struct DownloadProgressReport
    {
        QString fileName;
        qint64 downloadedSize;
        qint64 totalSize;
    };

    class NetworkRequestHelper : QObject {
        Q_OBJECT

        explicit NetworkRequestHelper(QObject *parent) : QObject(parent){};

        ~NetworkRequestHelper() override = default;
        ;

    public:
        static HTTPResponse HttpGet(const QString &url, bool sendHwid = false, bool useProxy = false,
                                    bool allowInsecureTls = true, bool forceDirect = false);

        static QString GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name);

        static QString DownloadAsset(const QString &url, const QString &fileName);
    };
} // namespace Configs_network

using namespace Configs_network;
