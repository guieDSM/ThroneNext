#include "include/database/entities/QuickRoutePolicy.hpp"
#include "include/database/entities/SimpleRouteInputPolicy.hpp"

#include <QFile>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace Configs::QuickRoute {
    namespace {
        struct SuffixRules {
            QSet<QString> exact;
            QSet<QString> wildcard;
            QSet<QString> exception;
        };

        const SuffixRules& suffixRules() {
            static const SuffixRules rules = [] {
                SuffixRules parsed;
                QFile file(QStringLiteral(":/data/public_suffix_list.dat"));
                if (!file.open(QIODevice::ReadOnly)) return parsed;
                while (!file.atEnd()) {
                    const QString line = QString::fromUtf8(file.readLine()).trimmed();
                    if (line.isEmpty() || line.startsWith(QStringLiteral("//"))) continue;
                    QString value = QString::fromLatin1(QUrl::toAce(line.startsWith('!') ? line.mid(1) :
                        line.startsWith(QStringLiteral("*.")) ? line.mid(2) : line)).toLower();
                    if (value.isEmpty()) continue;
                    if (line.startsWith('!')) parsed.exception.insert(value);
                    else if (line.startsWith(QStringLiteral("*."))) parsed.wildcard.insert(value);
                    else parsed.exact.insert(value);
                }
                return parsed;
            }();
            return rules;
        }

        QJsonArray strings(const QJsonObject& record, const char* key) {
            QJsonArray result;
            QSet<QString> seen;
            for (const auto& item : record.value(QLatin1String(key)).toArray()) {
                const QString value = item.toString().trimmed();
                if (value.isEmpty() || seen.contains(value)) continue;
                seen.insert(value);
                result.append(value);
            }
            return result;
        }

        QString ruleName(const QJsonObject& record) {
            return QStringLiteral("Quick route [%1] %2")
                .arg(record.value("id").toString(), record.value("display").toString());
        }

        bool matchesPlainDirectRule(const RouteRule& existing, const QJsonObject& rule) {
            if (existing.action != QStringLiteral("route") || existing.outboundID != directID ||
                !existing.ip_version.isEmpty() || !existing.network.isEmpty() || !existing.protocol.isEmpty() ||
                !existing.inbound.isEmpty() || !existing.domain.isEmpty() || !existing.domain_keyword.isEmpty() ||
                !existing.domain_regex.isEmpty() || !existing.source_ip_cidr.isEmpty() ||
                existing.source_ip_is_private || existing.ip_is_private || !existing.source_port.isEmpty() ||
                !existing.source_port_range.isEmpty() || !existing.port.isEmpty() ||
                !existing.port_range.isEmpty() || !existing.process_name.isEmpty() ||
                !existing.process_path_regex.isEmpty() || !existing.wifi_ssid.isEmpty() ||
                !existing.wifi_bssid.isEmpty() || !existing.rule_set.isEmpty() || existing.invert ||
                !existing.override_address.isEmpty() || !existing.override_port.isEmpty()) return false;
            return QJsonArray::fromStringList(existing.domain_suffix) == rule.value("domain_suffix").toArray() &&
                   QJsonArray::fromStringList(existing.ip_cidr) == rule.value("ip_cidr").toArray() &&
                   QJsonArray::fromStringList(existing.process_path) == rule.value("process_path").toArray();
        }
    }

    QString registrableDomain(const QString& host) {
        QHostAddress address;
        if (address.setAddress(host)) return {};
        const QString normalized = QString::fromLatin1(QUrl::toAce(host)).toLower();
        const QStringList labels = normalized.split('.', Qt::SkipEmptyParts);
        if (labels.size() < 2) return {};
        const auto& rules = suffixRules();
        int suffixLabels = 1; // PSL default rule is "*".
        bool exception = false;
        for (int i = 0; i < labels.size(); ++i) {
            const QString candidate = labels.mid(i).join('.');
            const int count = labels.size() - i;
            if (rules.exception.contains(candidate) && count > suffixLabels) {
                suffixLabels = count;
                exception = true;
            }
            if (!exception && rules.exact.contains(candidate) && count > suffixLabels)
                suffixLabels = count;
            if (!exception && i > 0 && rules.wildcard.contains(candidate) && count + 1 > suffixLabels)
                suffixLabels = count + 1;
        }
        if (exception) --suffixLabels;
        return labels.size() > suffixLabels ? labels.mid(labels.size() - suffixLabels - 1).join('.') : QString{};
    }

    Target parseSiteOrIp(const QString& input) {
        const QString text = input.trimmed();
        if (text.isEmpty()) return {.error = QStringLiteral("Введите ссылку, домен или IP")};

        // Exact IPs and CIDRs are handled before QUrl, which treats colon in IPv6 as a scheme.
        const auto subnet = QHostAddress::parseSubnet(text);
        if (subnet.second >= 0 && !text.contains("://")) {
            const QString canonical = subnet.first.toString() + '/' + QString::number(subnet.second);
            return {QStringLiteral("ip"), canonical, text, {}};
        }
        QHostAddress address;
        if (address.setAddress(text)) {
            const int bits = address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128;
            return {QStringLiteral("ip"), address.toString() + '/' + QString::number(bits), text, {}};
        }

        const QUrl url = QUrl::fromUserInput(text);
        if (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
            return {.error = QStringLiteral("Нужна ссылка http(s), домен, IP или подсеть")};
        QString error;
        if (address.setAddress(url.host())) {
            const int bits = address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128;
            return {QStringLiteral("ip"), address.toString() + '/' + QString::number(bits), url.host(), {}};
        }
        const QString host = SimpleRouteInput::normalizeDomain(url.host(), true, &error);
        if (host.isEmpty()) return {.error = error};
        const QString root = registrableDomain(host);
        if (root.isEmpty()) return {.error = QStringLiteral("Не удалось определить домен сайта")};
        return {QStringLiteral("site"), root, host, {}};
    }

    QJsonObject directRule(const QJsonObject& record) {
        QJsonObject rule{{"action", "route"}, {"outbound", directID}};
        const QString kind = record.value("kind").toString();
        if (kind == "site") rule["domain_suffix"] = strings(record, "domains");
        else if (kind == "ip") rule["ip_cidr"] = strings(record, "ips");
        else if (kind == "app") rule["process_path"] = strings(record, "paths");
        return rule;
    }

    QJsonObject withoutCoveredSelectors(const RouteProfile& profile, const QJsonObject& record,
                                        QStringList* covered) {
        const QString kind = record.value(QStringLiteral("kind")).toString();
        const QString recordKey = kind == QStringLiteral("site") ? QStringLiteral("domains")
            : kind == QStringLiteral("app") ? QStringLiteral("paths")
            : kind == QStringLiteral("ip") ? QStringLiteral("ips") : QString{};
        const QString selectorKey = kind == QStringLiteral("site") ? QStringLiteral("domain_suffix")
            : kind == QStringLiteral("app") ? QStringLiteral("process_path")
            : kind == QStringLiteral("ip") ? QStringLiteral("ip_cidr") : QString{};
        if (recordKey.isEmpty()) return record;
        QJsonArray rules;
        if (profile.isRaw) rules = QJsonDocument::fromJson(profile.rawRoute.toUtf8()).object()
                                        .value(QStringLiteral("rules")).toArray();
        else for (const auto& rule : profile.Rules) {
            if (rule->action != QStringLiteral("route") ||
                !rule->ip_version.isEmpty() || !rule->network.isEmpty() || !rule->protocol.isEmpty() ||
                !rule->inbound.isEmpty() || !rule->source_ip_cidr.isEmpty() ||
                rule->source_ip_is_private || rule->ip_is_private ||
                !rule->source_port.isEmpty() || !rule->source_port_range.isEmpty() ||
                !rule->port.isEmpty() || !rule->port_range.isEmpty() ||
                !rule->wifi_ssid.isEmpty() || !rule->wifi_bssid.isEmpty() ||
                !rule->rule_set.isEmpty() || rule->invert ||
                !rule->override_address.isEmpty() || !rule->override_port.isEmpty()) continue;
            const QList<QString>* selectors = nullptr;
            if (kind == QStringLiteral("site") && rule->domain.isEmpty() &&
                rule->domain_keyword.isEmpty() && rule->domain_regex.isEmpty() &&
                rule->ip_cidr.isEmpty() && rule->process_name.isEmpty() &&
                rule->process_path.isEmpty() && rule->process_path_regex.isEmpty())
                selectors = &rule->domain_suffix;
            else if (kind == QStringLiteral("app") && rule->domain.isEmpty() &&
                rule->domain_suffix.isEmpty() && rule->domain_keyword.isEmpty() &&
                rule->domain_regex.isEmpty() && rule->ip_cidr.isEmpty() &&
                rule->process_name.isEmpty() && rule->process_path_regex.isEmpty())
                selectors = &rule->process_path;
            else if (kind == QStringLiteral("ip") && rule->domain.isEmpty() &&
                rule->domain_suffix.isEmpty() && rule->domain_keyword.isEmpty() &&
                rule->domain_regex.isEmpty() && rule->process_name.isEmpty() &&
                rule->process_path.isEmpty() && rule->process_path_regex.isEmpty())
                selectors = &rule->ip_cidr;
            if (!selectors || selectors->isEmpty()) continue;
            QJsonArray values;
            for (const auto& value : *selectors) values.append(value);
            rules.append(QJsonObject{{QStringLiteral("action"), QStringLiteral("route")},
                                     {QStringLiteral("outbound"), rule->outboundID},
                                     {selectorKey, values}});
        }

        auto covers = [&](QString selector, QString value) {
            if (kind == QStringLiteral("site")) {
                selector = selector.toLower();
                value = value.toLower();
                const bool subdomainsOnly = selector.startsWith('.');
                if (subdomainsOnly) selector.removeFirst();
                return (!subdomainsOnly && selector == value)
                    || value.endsWith(QChar('.') + selector);
            }
            if (kind == QStringLiteral("app"))
                return QDir::fromNativeSeparators(selector).compare(
                    QDir::fromNativeSeparators(value), Qt::CaseInsensitive) == 0;
            const auto candidate = QHostAddress::parseSubnet(value);
            const auto existing = QHostAddress::parseSubnet(selector);
            return candidate.second >= 0 && existing.second >= 0
                && existing.second <= candidate.second
                && candidate.first.isInSubnet(existing);
        };
        auto isCovered = [&](const QString& value) {
            for (const auto& item : rules) {
                const QJsonObject rule = item.toObject();
                // Another selector family or routing condition makes this rule
                // conditional; it cannot prove that every request is covered.
                bool unconditional = true;
                for (auto it = rule.begin(); it != rule.end(); ++it)
                    if (it.key() != QStringLiteral("action") && it.key() != QStringLiteral("outbound")
                        && it.key() != selectorKey) unconditional = false;
                if (!unconditional || rule.value(QStringLiteral("action")).toString() != QStringLiteral("route"))
                    continue;
                const QJsonValue selectors = rule.value(selectorKey);
                const QJsonArray values = selectors.isArray() ? selectors.toArray()
                    : (selectors.isString() ? QJsonArray{selectors} : QJsonArray{});
                bool matches = false;
                for (const auto& selector : values)
                    matches |= covers(selector.toString(), value);
                if (!matches) continue;
                // Respect rule order: a preceding proxy rule can be overridden
                // by the new exception even if a later direct rule exists.
                const QJsonValue outbound = rule.value(QStringLiteral("outbound"));
                return outbound.toString() == QStringLiteral("direct")
                    || (outbound.isDouble() && outbound.toInt() == directID);
            }
            return false;
        };

        QJsonArray kept;
        QSet<QString> seen;
        for (const auto& item : record.value(recordKey).toArray()) {
            QString value = item.toString().trimmed();
            if (kind == QStringLiteral("site")) {
                const auto parsed = parseSiteOrIp(value);
                if (parsed.kind == QStringLiteral("site")) value = parsed.value;
                else value = value.toLower();
            }
            const QString key = value.toCaseFolded();
            if (value.isEmpty() || seen.contains(key)) continue;
            seen.insert(key);
            bool coveredByNew = false;
            if (kind == QStringLiteral("site"))
                for (const auto& earlier : kept)
                    coveredByNew |= covers(earlier.toString(), value);
            if (coveredByNew || isCovered(value)) {
                if (covered) covered->append(value);
            } else kept.append(value);
        }
        if (kept.isEmpty()) return {};
        QJsonObject result = record;
        result.insert(recordKey, kept);
        return result;
    }

    SiteDiagnostics analyzeConsoleText(const QString& log, const QString& mainDomain) {
        SiteDiagnostics result;
        const QRegularExpression urls(QStringLiteral(R"(https?://[^\s"'<>]+)"));
        const QRegularExpression httpStatus(QStringLiteral(R"(\b([45][0-9][0-9])\b)"));
        QSet<QString> seen;
        for (const QString& line : log.split('\n')) {
            if (line.contains(QStringLiteral("ERR_BLOCKED_BY_CLIENT"), Qt::CaseInsensitive)) {
                ++result.browserBlocked;
                continue;
            }
            if (line.contains(QStringLiteral(".live is not a function"))) ++result.scriptErrors;
            const auto status = httpStatus.match(line);
            const bool failedHttp = status.hasMatch();
            const bool failedNetwork = line.contains(QStringLiteral("ERR_TIMED_OUT"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("ERR_CONNECTION"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("ERR_NAME_NOT_RESOLVED"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("ERR_ADDRESS_UNREACHABLE"), Qt::CaseInsensitive);
            if (!failedHttp && !failedNetwork) continue;
            if (failedHttp) ++result.httpErrors;
            auto matches = urls.globalMatch(line);
            while (matches.hasNext()) {
                QString url = matches.next().captured();
                while (!url.isEmpty() && QStringLiteral(".,);]").contains(url.back())) url.chop(1);
                const auto parsed = parseSiteOrIp(url);
                if (parsed.kind != QStringLiteral("site") || parsed.value == mainDomain ||
                    seen.contains(parsed.value)) continue;
                seen.insert(parsed.value);
                result.candidates.append({parsed.value, url, failedHttp
                    ? QStringLiteral("HTTP %1").arg(status.captured(1))
                    : QStringLiteral("Ошибка соединения")});
            }
        }
        return result;
    }

    SiteDiagnostics analyzeHar(const QByteArray& data, const QString& mainDomain, QString* error) {
        SiteDiagnostics result;
        if (data.size() > 32 * 1024 * 1024) {
            if (error) *error = QStringLiteral("HAR больше 32 МБ");
            return result;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        const QJsonArray entries = document.object().value(QStringLiteral("log")).toObject()
                                       .value(QStringLiteral("entries")).toArray();
        if (!document.isObject() || !document.object().value(QStringLiteral("log")).isObject() ||
            !document.object().value(QStringLiteral("log")).toObject()
                 .value(QStringLiteral("entries")).isArray()) {
            if (error) *error = QStringLiteral("Файл не содержит список запросов HAR");
            return result;
        }
        QSet<QString> seen;
        const QRegularExpression errorPage(QStringLiteral(
            R"(<(?:title|h1)(?:\s+[^>]*)?>\s*(?:403 Forbidden|404 Not Found|502 Bad Gateway|503 Service Unavailable)\s*</(?:title|h1)>)"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto& item : entries) {
            const QJsonObject entry = item.toObject();
            const QJsonObject request = entry.value(QStringLiteral("request")).toObject();
            const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
            const QUrl url(request.value(QStringLiteral("url")).toString());
            if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                                   url.scheme() != QStringLiteral("https"))) continue;
            const QString networkError = entry.value(QStringLiteral("_error")).toString(
                response.value(QStringLiteral("_error")).toString());
            if (networkError.contains(QStringLiteral("ERR_BLOCKED_BY_CLIENT"), Qt::CaseInsensitive)) {
                ++result.browserBlocked;
                continue;
            }
            const int status = response.value(QStringLiteral("status")).toInt();
            const bool failedHttp = status >= 400;
            if (failedHttp) ++result.httpErrors;
            const QJsonObject content = response.value(QStringLiteral("content")).toObject();
            const QString kind = entry.value(QStringLiteral("_resourceType")).toString(
                entry.value(QStringLiteral("_resource_type")).toString());
            bool htmlError = false;
            const QString body = content.value(QStringLiteral("text")).toString();
            if (kind.compare(QStringLiteral("document"), Qt::CaseInsensitive) == 0 &&
                content.value(QStringLiteral("mimeType")).toString().startsWith(
                    QStringLiteral("text/html"), Qt::CaseInsensitive) && body.size() <= 12 * 1024) {
                const QByteArray bytes = content.value(QStringLiteral("encoding")).toString() ==
                    QStringLiteral("base64") ? QByteArray::fromBase64(body.toLatin1()) : body.toUtf8();
                if (bytes.size() <= 8 * 1024)
                    htmlError = errorPage.match(QString::fromUtf8(bytes)).hasMatch();
            }
            if (htmlError) ++result.htmlErrorPages;
            const bool failedNetwork = status == 0 && !networkError.isEmpty();
            if (!failedHttp && !htmlError && !failedNetwork) continue;
            const Target target = parseSiteOrIp(url.host());
            if (target.kind != QStringLiteral("site") || target.value == mainDomain ||
                seen.contains(target.value)) continue;
            seen.insert(target.value);
            const QString issue = htmlError && !failedHttp
                ? QStringLiteral("HTML-страница ошибки при HTTP %1").arg(status)
                : failedHttp ? QStringLiteral("HTTP %1").arg(status)
                             : QStringLiteral("Ошибка соединения");
            // HAR URLs may contain signed media paths and credentials. Retain only
            // the domain, which is sufficient for a route suggestion.
            result.candidates.append({target.value, {}, issue});
        }
        return result;
    }

    bool insertRule(RouteProfile& profile, const QJsonObject& record, QString* error) {
        const auto rule = directRule(record);
        if (rule.size() <= 2 || (rule.value("domain_suffix").toArray().isEmpty() &&
                                 rule.value("ip_cidr").toArray().isEmpty() &&
                                 rule.value("process_path").toArray().isEmpty())) {
            if (error) *error = QStringLiteral("Пустое исключение");
            return false;
        }
        if (profile.isRaw) {
            const auto parsed = QJsonDocument::fromJson(profile.rawRoute.toUtf8());
            if (!parsed.isObject() || !parsed.object().value("rules").isArray()) {
                if (error) *error = QStringLiteral("Профиль содержит некорректные raw-правила");
                return false;
            }
            QJsonObject root = parsed.object();
            QJsonArray rules = root.value("rules").toArray();
            if (rules.contains(rule)) {
                if (error) *error = QStringLiteral("Такое прямое правило уже есть в профиле маршрутизации");
                return false;
            }
            rules.prepend(rule);
            root["rules"] = rules;
            profile.rawRoute = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
        } else {
            const QString name = ruleName(record);
            for (const auto& existing : profile.Rules) {
                if (existing->name == name) {
                    if (error) *error = QStringLiteral("Управляемое правило с таким именем уже есть в профиле");
                    return false;
                }
                if (matchesPlainDirectRule(*existing, rule)) {
                    if (error) *error = QStringLiteral("Такое прямое правило уже есть в профиле маршрутизации");
                    return false;
                }
            }
            auto managed = std::make_shared<RouteRule>();
            managed->name = name;
            managed->type = custom;
            managed->outboundID = directID;
            for (const auto& value : rule.value("domain_suffix").toArray()) managed->domain_suffix.append(value.toString());
            for (const auto& value : rule.value("ip_cidr").toArray()) managed->ip_cidr.append(value.toString());
            for (const auto& value : rule.value("process_path").toArray()) managed->process_path.append(value.toString());
            profile.Rules.prepend(managed);
        }
        return true;
    }

    bool removeRule(RouteProfile& profile, const QJsonObject& record, QString* error) {
        if (profile.isRaw) {
            const auto parsed = QJsonDocument::fromJson(profile.rawRoute.toUtf8());
            if (!parsed.isObject()) {
                if (error) *error = QStringLiteral("Raw-профиль повреждён");
                return false;
            }
            QJsonObject root = parsed.object();
            QJsonArray rules = root.value("rules").toArray();
            const auto target = directRule(record);
            bool removed = false;
            for (qsizetype i = 0; i < rules.size(); ++i) {
                if (rules.at(i).toObject() == target) { rules.removeAt(i); removed = true; break; }
            }
            if (!removed) {
                if (error) *error = QStringLiteral("Правило изменено вручную; автоматическое удаление отменено");
                return false;
            }
            root["rules"] = rules;
            profile.rawRoute = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
        } else {
            const QString name = ruleName(record);
            bool removed = false;
            for (qsizetype i = 0; i < profile.Rules.size(); ++i) {
                if (profile.Rules.at(i)->name == name) { profile.Rules.removeAt(i); removed = true; break; }
            }
            if (!removed) {
                if (error) *error = QStringLiteral("Управляемое правило не найдено");
                return false;
            }
        }
        return true;
    }

    QList<QJsonObject> readRecords(const QString& json) {
        QList<QJsonObject> result;
        for (const auto& value : QJsonDocument::fromJson(json.toUtf8()).array())
            if (value.isObject()) result.append(value.toObject());
        return result;
    }

    QString writeRecords(const QList<QJsonObject>& records) {
        QJsonArray values;
        for (const auto& record : records) values.append(record);
        return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
    }
}
