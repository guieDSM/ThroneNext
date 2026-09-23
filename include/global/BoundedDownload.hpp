#pragma once

#include <QEventLoop>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace Configs_network {
    struct BoundedDownload {
        QByteArray data;
        QString error;
    };

    inline bool isHttpUrl(const QUrl &url) {
        return url.isValid() && !url.host().isEmpty()
            && (url.scheme() == "https" || url.scheme() == "http");
    }

    // The Qt transfer timeout measures inactivity only. A slow-drip response
    // needs a separate absolute deadline and a cap on the decoded body size.
    inline BoundedDownload readBoundedReply(QNetworkReply *reply, qsizetype maxBytes, int deadlineMs) {
        BoundedDownload result;
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        reply->setReadBufferSize(64 * 1024);
        const auto drain = [&] {
            if (!result.error.isEmpty()) return;
            const auto chunk = reply->read(maxBytes - result.data.size() + 1);
            if (chunk.size() > maxBytes - result.data.size()) {
                result.error = QObject::tr("Response exceeds the download size limit.");
                result.data.clear();
                reply->abort();
                loop.quit();
                return;
            }
            result.data += chunk;
        };
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, drain);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
            result.error = QObject::tr("Download deadline exceeded.");
            reply->abort();
            loop.quit();
        });
        deadline.start(deadlineMs);
        drain();
        if (!reply->isFinished() && result.error.isEmpty()) loop.exec();
        drain();
        if (result.error.isEmpty() && reply->error() != QNetworkReply::NoError)
            result.error = reply->errorString();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (result.error.isEmpty() && (status < 200 || status >= 300))
            result.error = QObject::tr("Server returned HTTP status %1.").arg(status);
        if (!result.error.isEmpty()) result.data.clear();
        return result;
    }
}
