#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/BoundedDownload.hpp"

#include <QNetworkProxy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QSaveFile>
#include <QApplication>
#include <QMap>
#include <QStringList>



#include "include/global/Configs.hpp"
#include "include/ui/mainwindow.h"
#include "include/global/DeviceDetailsHelper.hpp"

namespace Configs_network {

    namespace {
        bool shouldUseLocalProxy(bool explicitlyRequested = false) {
            const auto& settings = *Configs::dataManager->settingsRepo;
            // Selecting System Proxy is a desired launch mode, not proof that
            // the loopback listener already exists.  Subscription updates must
            // stay usable before the first profile starts.
            return settings.net_use_proxy || explicitlyRequested
                || (settings.spmode_system_proxy && settings.started_id >= 0);
        }
    }

    HTTPResponse NetworkRequestHelper::HttpGet(const QString &url, bool sendHwid, bool useProxy,
                                                bool allowInsecureTls, bool forceDirect) {
        if (!isHttpUrl(QUrl(url))) return HTTPResponse{QObject::tr("Only HTTP and HTTPS URLs are supported.")};
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        accessManager.setTransferTimeout(10000);
        request.setUrl(url);
        if (forceDirect) {
            // A timed-out subscription may need to bypass the loopback HTTP
            // proxy, even while Windows system proxy mode is enabled.
            accessManager.setProxy(QNetworkProxy::NoProxy);
        } else if (shouldUseLocalProxy(useProxy)) {
            if (Configs::dataManager->settingsRepo->started_id < 0) {
                return HTTPResponse{QObject::tr("Request with proxy but no profile started.")};
            }
            QNetworkProxy p;
            p.setType(QNetworkProxy::HttpProxy);
            const auto address = Configs::dataManager->settingsRepo->inbound_address;
            p.setHostName(address == "::" || address == "0.0.0.0" ? "127.0.0.1" : address);
            p.setPort(Configs::dataManager->settingsRepo->inbound_socks_port);
            if (Configs::dataManager->settingsRepo->inbound_auth) {
                p.setUser(Configs::dataManager->settingsRepo->inbound_user);
                p.setPassword(Configs::dataManager->settingsRepo->inbound_pass);
            }
            accessManager.setProxy(p);
        }
        // Set attribute
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, Configs::dataManager->settingsRepo->GetUserAgent());
        if (allowInsecureTls && Configs::dataManager->settingsRepo->net_insecure) {
            QSslConfiguration c;
            c.setPeerVerifyMode(QSslSocket::PeerVerifyMode::VerifyNone);
            request.setSslConfiguration(c);
        }
        //Attach HWID and device info headers if enabled in settings
        if (sendHwid) {
            auto details = GetDeviceDetails();

            // Parse custom parameters if provided
            QMap<QString, QString> customParams;
            if (!Configs::dataManager->settingsRepo->sub_custom_hwid_params.isEmpty()) {
                QStringList pairs = Configs::dataManager->settingsRepo->sub_custom_hwid_params.split(',');
                for (const QString &pair : pairs) {
                    QString trimmed = pair.trimmed();
                    int eqPos = trimmed.indexOf('=');
                    if (eqPos > 0) {
                        QString key = trimmed.left(eqPos).trimmed();
                        QString value = trimmed.mid(eqPos + 1).trimmed();
                        // Validate: key must be one of the allowed parameters, value must not contain newlines
                        if (!key.isEmpty() && !value.isEmpty() &&
                            !value.contains('\n') && !value.contains('\r') &&
                            value.length() < 1000) { // Reasonable length limit
                            QString lowerKey = key.toLower();
                            // Only accept known parameter keys
                            if (lowerKey == "hwid" || lowerKey == "os" ||
                                lowerKey == "osversion" || lowerKey == "model") {
                                customParams[lowerKey] = value;
                            }
                        }
                    }
                }
            }

            // Use custom values if provided, otherwise use default values
            QString hwid = customParams.contains("hwid") ? customParams["hwid"] : details.hwid;
            QString os = customParams.contains("os") ? customParams["os"] : details.os;
            QString osVersion = customParams.contains("osversion") ? customParams["osversion"] : details.osVersion;
            QString model = customParams.contains("model") ? customParams["model"] : details.model;

            if (!hwid.isEmpty()) request.setRawHeader("x-hwid", hwid.toUtf8());
            if (!os.isEmpty()) request.setRawHeader("x-device-os", os.toUtf8());
            if (!osVersion.isEmpty()) request.setRawHeader("x-ver-os", osVersion.toUtf8());
            if (!model.isEmpty()) request.setRawHeader("x-device-model", model.toUtf8());
        }
        //
        auto _reply = accessManager.get(request);
        connect(_reply, &QNetworkReply::sslErrors, _reply, [allowInsecureTls](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err: errors) {
                error_str << err.errorString();
            }
            MW_show_log(QString("SSL Errors: %1 %2").arg(error_str.join(","), allowInsecureTls && Configs::dataManager->settingsRepo->net_insecure ? "(Ignored)" : ""));
        });
        const auto download = readBoundedReply(_reply, 32 * 1024 * 1024, 30000);
        auto result = HTTPResponse{download.error, download.data, _reply->rawHeaderPairs(), _reply->error()};
        _reply->deleteLater();
        return result;
    }

    QString NetworkRequestHelper::GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name) {
        for (const auto &p: header) {
            if (QString(p.first).toLower() == name.toLower()) return p.second;
        }
        return "";
    }

    QString NetworkRequestHelper::DownloadAsset(const QString &url, const QString &fileName) {
        // Assets include executable updates. The subscription TLS override must
        // never authorize an unverified binary download.
        if (!isHttpUrl(QUrl(url)) || QUrl(url).scheme() != "https")
            return QObject::tr("Asset downloads require HTTPS.");
        if (fileName.isEmpty() || fileName == "." || fileName == ".."
            || fileName.contains('/') || fileName.contains('\\') || fileName.contains(':'))
            return QObject::tr("Invalid asset file name.");
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        accessManager.setTransferTimeout(15000);
        request.setUrl(url);
        if (shouldUseLocalProxy()) {
            if (Configs::dataManager->settingsRepo->started_id < 0) {
                return QObject::tr("Request with proxy but no profile started.");
            }
            QNetworkProxy p;
            p.setType(QNetworkProxy::HttpProxy);
            const auto address = Configs::dataManager->settingsRepo->inbound_address;
            p.setHostName(address == "::" || address == "0.0.0.0" ? "127.0.0.1" : address);
            p.setPort(Configs::dataManager->settingsRepo->inbound_socks_port);
            if (Configs::dataManager->settingsRepo->inbound_auth) {
                p.setUser(Configs::dataManager->settingsRepo->inbound_user);
                p.setPassword(Configs::dataManager->settingsRepo->inbound_pass);
            }
            accessManager.setProxy(p);
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto _reply = accessManager.get(request);
        connect(_reply, &QNetworkReply::sslErrors, _reply, [](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err: errors) {
                error_str << err.errorString();
            }
            MW_show_log(QString("Asset TLS verification failed: %1").arg(error_str.join(",")));
        });
        connect(_reply, &QNetworkReply::downloadProgress, _reply, [&](qint64 bytesReceived, qint64 bytesTotal)
        {
            runOnUiThread([=]{
                GetMainWindow()->setDownloadReport(DownloadProgressReport{fileName, bytesReceived, bytesTotal}, true);
                GetMainWindow()->UpdateDataView();
            });
        });
        const auto download = readBoundedReply(_reply, 256 * 1024 * 1024, 300000);
        runOnUiThread([=]
        {
            GetMainWindow()->setDownloadReport({}, false);
            GetMainWindow()->UpdateDataView(true);
        });
        _reply->deleteLater();
        if (!download.error.isEmpty()) return download.error;
        const auto &body = download.data;
        if (body.isEmpty()) {
            return QObject::tr("Download failed: the server returned an empty response.");
        }

        const auto filePath = Configs::GetBasePath() + "/" + fileName;
        QSaveFile tmp(filePath);
        tmp.setDirectWriteFallback(false);
        if (!tmp.open(QIODevice::WriteOnly)) {
            return QObject::tr("Could not open file.");
        }
        if (tmp.write(body) != body.size()) {
            tmp.cancelWriting();
            return QObject::tr("Could not write file.");
        }
        if (!tmp.commit()) {
            return QObject::tr("Could not save downloaded file.");
        }
        return "";
    }

} // namespace Configs_network
