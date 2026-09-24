#pragma once

#include "include/database/entities/RouteProfile.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QByteArray>

namespace Configs::QuickRoute {
    // Each managed exception owns exactly one direct rule in every selected profile.
    // Records live in SettingsRepo so profile backups carry their management metadata.
    struct Target {
        QString kind; // site, ip, or app
        QString value;
        QString display;
        QString error;
    };

    struct SiteErrorCandidate {
        QString domain;
        QString url;
        QString issue;
    };
    struct SiteDiagnostics {
        QList<SiteErrorCandidate> candidates;
        int browserBlocked = 0;
        int httpErrors = 0;
        int scriptErrors = 0;
        int htmlErrorPages = 0;
    };
    struct BrowserCaptureCandidate {
        QString domain;
        QString reason; // html_error, http_error, network_error, or observed_host
    };

    Target parseSiteOrIp(const QString& input);
    QString registrableDomain(const QString& host);
    QJsonObject directRule(const QJsonObject& record);
    // Removes selectors already covered by unconditional direct rules in this
    // profile. An empty returned record means there is nothing new to add.
    QJsonObject withoutCoveredSelectors(const RouteProfile& profile, const QJsonObject& record,
                                        QStringList* covered = nullptr);
    SiteDiagnostics analyzeConsoleText(const QString& log, const QString& mainDomain);
    SiteDiagnostics analyzeHar(const QByteArray& data, const QString& mainDomain,
                               QString* error = nullptr);
    QList<BrowserCaptureCandidate> readBrowserCaptureCandidates(const QJsonObject& capture,
                                                                 const QString& mainDomain);
    bool insertRule(RouteProfile& profile, const QJsonObject& record, QString* error = nullptr);
    bool removeRule(RouteProfile& profile, const QJsonObject& record, QString* error = nullptr);
    QList<QJsonObject> readRecords(const QString& json);
    QString writeRecords(const QList<QJsonObject>& records);
}
