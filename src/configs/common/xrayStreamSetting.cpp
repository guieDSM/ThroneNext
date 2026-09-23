#include "include/configs/common/xrayStreamSetting.h"
#include <QUrlQuery>
#include <QJsonArray>
#include <QJsonDocument>
#include "include/configs/common/XHTTPValidationPolicy.hpp"
#include "include/configs/common/utils.h"

namespace Configs {
    namespace {
        QString qs(const std::string& value) {
            return QString::fromStdString(value);
        }

        bool hasText(const std::string& value) {
            return !value.empty();
        }

        void addXmuxField(QJsonObject& xmux, const QString& key, const std::string& value) {
            if (hasText(value)) xmux[key] = qs(value);
        }

        void addXPaddingFields(QJsonObject& extra, bool obfsMode, const std::string& key,
                               const std::string& header, const std::string& placement,
                               const std::string& method) {
            if (obfsMode) extra["xPaddingObfsMode"] = true;
            if (hasText(key)) extra["xPaddingKey"] = qs(key);
            if (hasText(header)) extra["xPaddingHeader"] = qs(header);
            if (hasText(placement)) extra["xPaddingPlacement"] = qs(placement);
            if (hasText(method)) extra["xPaddingMethod"] = qs(method);
        }

        void addStringField(QJsonObject& object, const QString& key, const std::string& value) {
            if (hasText(value)) object[key] = qs(value);
        }

        void addNumericField(QJsonObject& object, const QString& key, const std::string& value) {
            if (!hasText(value)) return;
            bool ok = false;
            const auto parsed = qs(value).toLongLong(&ok);
            if (ok) object[key] = parsed;
        }

        QJsonObject buildHeadersObject(const std::map<std::string, std::string>& headers) {
            QJsonObject object;
            for (const auto& [key, value] : headers) object[qs(key)] = qs(value);
            return object;
        }

        template <typename XHTTPOptions>
        void addAdvancedXHTTPFields(QJsonObject& extra, const XHTTPOptions& settings) {
            if (auto headers = buildHeadersObject(settings.headers); !headers.isEmpty()) {
                extra["headers"] = headers;
            }
            addStringField(extra, "xPaddingBytes", settings.x_padding_bytes);
            addXPaddingFields(extra, settings.x_padding_obfs_mode, settings.x_padding_key,
                              settings.x_padding_header, settings.x_padding_placement,
                              settings.x_padding_method);
            addStringField(extra, "uplinkHTTPMethod", settings.uplink_http_method);
            addStringField(extra, "sessionIDPlacement", settings.session_id_placement);
            addStringField(extra, "sessionIDKey", settings.session_id_key);
            addStringField(extra, "sessionIDTable", settings.session_id_table);
            addStringField(extra, "sessionIDLength", settings.session_id_length);
            addStringField(extra, "seqPlacement", settings.seq_placement);
            addStringField(extra, "seqKey", settings.seq_key);
            addStringField(extra, "uplinkDataPlacement", settings.uplink_data_placement);
            addStringField(extra, "uplinkDataKey", settings.uplink_data_key);
            addStringField(extra, "uplinkChunkSize", settings.uplink_chunk_size);
            if (settings.no_grpc_header) extra["noGRPCHeader"] = true;
            if (settings.no_sse_header) extra["noSSEHeader"] = true;
            addStringField(extra, "scMaxEachPostBytes", settings.sc_max_each_post_bytes);
            addStringField(extra, "scMinPostsIntervalMs", settings.sc_min_posts_interval_ms);
            addNumericField(extra, "scMaxBufferedPosts", settings.sc_max_buffered_posts);
            addStringField(extra, "scStreamUpServerSecs", settings.sc_stream_up_server_secs);
            if (int(settings.server_max_header_bytes) != 0) {
                extra["serverMaxHeaderBytes"] = int(settings.server_max_header_bytes);
            }
        }

        QJsonObject buildXmuxObject(const clash::xhttpReuseSettings& reuse) {
            QJsonObject xmux;
            addXmuxField(xmux, "maxConcurrency", reuse.max_concurrency);
            addXmuxField(xmux, "maxConnections", reuse.max_connections);
            addXmuxField(xmux, "cMaxReuseTimes", reuse.c_max_reuse_times);
            addXmuxField(xmux, "hMaxRequestTimes", reuse.h_max_request_times);
            addXmuxField(xmux, "hMaxReusableSecs", reuse.h_max_reusable_secs);
            if (hasText(reuse.h_keep_alive_period)) {
                xmux["hKeepAlivePeriod"] = qs(reuse.h_keep_alive_period).toLongLong();
            }
            return xmux;
        }

        QJsonObject buildDownloadSettingsObject(const clash::xhttpDownloadSettings& settings) {
            QJsonObject object;
            if (hasText(settings.server)) object["address"] = qs(settings.server);
            object["port"] = int(settings.port);
            object["network"] = "xhttp";

            if (!settings.reality_opts.public_key.empty()) {
                object["security"] = "reality";
                QJsonObject reality;
                reality["show"] = false;
                if (hasText(settings.servername)) reality["serverName"] = qs(settings.servername);
                if (hasText(settings.client_fingerprint)) reality["fingerprint"] = qs(settings.client_fingerprint);
                reality["publicKey"] = qs(settings.reality_opts.public_key);
                if (hasText(settings.reality_opts.short_id)) reality["shortId"] = qs(settings.reality_opts.short_id);
                object["realitySettings"] = reality;
            } else if (settings.tls) {
                object["security"] = "tls";
                QJsonObject tls;
                if (hasText(settings.servername)) tls["serverName"] = qs(settings.servername);
                if (!settings.alpn.empty()) {
                    QJsonArray alpn;
                    for (const auto& item : settings.alpn) alpn.append(qs(item));
                    tls["alpn"] = alpn;
                }
                if (hasText(settings.client_fingerprint)) tls["fingerprint"] = qs(settings.client_fingerprint);
                object["tlsSettings"] = tls;
            }

            QJsonObject xhttp;
            if (hasText(settings.host)) xhttp["host"] = qs(settings.host);
            if (hasText(settings.path)) xhttp["path"] = qs(settings.path);
            xhttp["mode"] = hasText(settings.mode) ? qs(settings.mode) : "auto";

            QJsonObject extra;
            addAdvancedXHTTPFields(extra, settings);
            if (auto xmux = buildXmuxObject(settings.reuse_settings); !xmux.isEmpty()) extra["xmux"] = xmux;
            if (!extra.isEmpty()) xhttp["extra"] = extra;

            object["xhttpSettings"] = xhttp;
            return object;
        }

        const QStringList& knownXHTTPExtraKeys() {
            static const QStringList keys = {
                "headers",
                "xPaddingBytes",
                "xPaddingObfsMode",
                "xPaddingKey",
                "xPaddingHeader",
                "xPaddingPlacement",
                "xPaddingMethod",
                "uplinkHTTPMethod",
                "sessionPlacement",
                "sessionKey",
                "sessionIDPlacement",
                "sessionIDKey",
                "sessionIDTable",
                "sessionIDLength",
                "seqPlacement",
                "seqKey",
                "uplinkDataPlacement",
                "uplinkDataKey",
                "uplinkChunkSize",
                "noGRPCHeader",
                "noSSEHeader",
                "scMaxEachPostBytes",
                "scMinPostsIntervalMs",
                "scMaxBufferedPosts",
                "scStreamUpServerSecs",
                "serverMaxHeaderBytes",
                "xmux",
                "downloadSettings",
            };
            return keys;
        }

        const QStringList& knownXHTTPXmuxKeys() {
            static const QStringList keys = {
                "maxConcurrency",
                "maxConnections",
                "cMaxReuseTimes",
                "hMaxRequestTimes",
                "hMaxReusableSecs",
                "hKeepAlivePeriod",
            };
            return keys;
        }

        void exportString(QJsonObject& object, const QString& key, const QString& value) {
            if (value.isEmpty()) object.remove(key);
            else object[key] = value;
        }

        void exportBool(QJsonObject& object, const QString& key, bool value) {
            if (value) object[key] = true;
            else object.remove(key);
        }

        void exportLongLong(QJsonObject& object, const QString& key, long long value) {
            if (value != 0) object[key] = value;
            else object.remove(key);
        }

        void exportInt(QJsonObject& object, const QString& key, int value) {
            if (value != 0) object[key] = value;
            else object.remove(key);
        }

        void parseString(const QJsonObject& object, const QString& key, QString& target) {
            if (object.contains(key)) target = object[key].toString();
        }

        void parseVariantString(const QJsonObject& object, const QString& key, QString& target) {
            if (object.contains(key)) target = object[key].toVariant().toString();
        }

        void parseBool(const QJsonObject& object, const QString& key, bool& target) {
            if (object.contains(key)) target = object[key].toBool();
        }

        void parseLongLong(const QJsonObject& object, const QString& key, long long& target) {
            if (object.contains(key)) target = object[key].toVariant().toLongLong();
        }

        void parseInt(const QJsonObject& object, const QString& key, int& target) {
            if (object.contains(key)) target = object[key].toVariant().toInt();
        }

        bool queryAliasValue(const QUrlQuery& query, const QStringList& aliases, QString& value) {
            bool found = false;
            // Aliases are ordered from lowest to highest priority. This makes the
            // canonical Xray spelling win when a provider emits duplicate forms.
            for (const auto& alias : aliases) {
                if (!query.hasQueryItem(alias)) continue;
                value = query.queryItemValue(alias, QUrl::FullyDecoded);
                found = true;
            }
            return found;
        }

        void parseQueryStringAliases(const QUrlQuery& query, const QStringList& aliases, QString& target) {
            QString value;
            if (queryAliasValue(query, aliases, value)) target = value;
        }

        void parseQueryBoolAliases(const QUrlQuery& query, const QStringList& aliases, bool& target) {
            QString value;
            if (!queryAliasValue(query, aliases, value)) return;

            value = value.trimmed().toLower();
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                target = true;
            } else if (value == "0" || value == "false" || value == "no" || value == "off") {
                target = false;
            }
        }

        void parseQueryLongLongAliases(const QUrlQuery& query, const QStringList& aliases, long long& target) {
            QString value;
            if (!queryAliasValue(query, aliases, value)) return;

            bool ok = false;
            const auto parsed = value.toLongLong(&ok);
            if (ok) target = parsed;
        }

        void parseQueryIntAliases(const QUrlQuery& query, const QStringList& aliases, int& target) {
            QString value;
            if (!queryAliasValue(query, aliases, value)) return;

            bool ok = false;
            const auto parsed = value.toInt(&ok);
            if (ok) target = parsed;
        }

        void parseXHTTPXmuxObject(xrayXHTTP* config, const QJsonObject& obj) {
            for (const auto& key : obj.keys()) {
                if (!knownXHTTPXmuxKeys().contains(key)) {
                    config->rawXmux[key] = obj[key];
                }
            }
            parseVariantString(obj, "maxConcurrency", config->maxConcurrency);
            parseVariantString(obj, "maxConnections", config->maxConnections);
            parseVariantString(obj, "cMaxReuseTimes", config->cMaxReuseTimes);
            parseVariantString(obj, "hMaxRequestTimes", config->hMaxRequestTimes);
            parseVariantString(obj, "hMaxReusableSecs", config->hMaxReusableSecs);
            parseLongLong(obj, "hKeepAlivePeriod", config->hKeepAlivePeriod);
        }

        void parseXHTTPExtraObject(xrayXHTTP* config, const QJsonObject& obj) {
            for (const auto& key : obj.keys()) {
                if (!knownXHTTPExtraKeys().contains(key)) {
                    config->rawExtra[key] = obj[key];
                }
            }

            if (obj.contains("headers")) {
                if (obj["headers"].isObject()) {
                    config->headers = jsonObjectToQStringList(obj["headers"].toObject());
                } else if (obj["headers"].isArray()) {
                    config->headers = QJsonArray2QListString(obj["headers"].toArray());
                }
            }
            parseVariantString(obj, "xPaddingBytes", config->xPaddingBytes);
            parseBool(obj, "xPaddingObfsMode", config->xPaddingObfsMode);
            parseString(obj, "xPaddingKey", config->xPaddingKey);
            parseString(obj, "xPaddingHeader", config->xPaddingHeader);
            parseString(obj, "xPaddingPlacement", config->xPaddingPlacement);
            parseString(obj, "xPaddingMethod", config->xPaddingMethod);
            parseString(obj, "uplinkHTTPMethod", config->uplinkHTTPMethod);
            parseString(obj, "sessionPlacement", config->sessionIDPlacement); // legacy alias (pre-v26.6.22 Xray)
            parseString(obj, "sessionIDPlacement", config->sessionIDPlacement);
            parseString(obj, "sessionKey", config->sessionIDKey); // legacy alias (pre-v26.6.22 Xray)
            parseString(obj, "sessionIDKey", config->sessionIDKey);
            parseString(obj, "sessionIDTable", config->sessionIDTable);
            parseVariantString(obj, "sessionIDLength", config->sessionIDLength);
            parseString(obj, "seqPlacement", config->seqPlacement);
            parseString(obj, "seqKey", config->seqKey);
            parseString(obj, "uplinkDataPlacement", config->uplinkDataPlacement);
            parseString(obj, "uplinkDataKey", config->uplinkDataKey);
            parseVariantString(obj, "uplinkChunkSize", config->uplinkChunkSize);
            parseBool(obj, "noGRPCHeader", config->noGRPCHeader);
            parseBool(obj, "noSSEHeader", config->noSSEHeader);
            parseVariantString(obj, "scMaxEachPostBytes", config->scMaxEachPostBytes);
            parseVariantString(obj, "scMinPostsIntervalMs", config->scMinPostsIntervalMs);
            parseLongLong(obj, "scMaxBufferedPosts", config->scMaxBufferedPosts);
            parseVariantString(obj, "scStreamUpServerSecs", config->scStreamUpServerSecs);
            parseInt(obj, "serverMaxHeaderBytes", config->serverMaxHeaderBytes);
            if (obj.contains("downloadSettings")) {
                if (obj["downloadSettings"].isObject()) {
                    config->downloadSettings = QJsonObject2QString(obj["downloadSettings"].toObject(), true);
                } else if (obj["downloadSettings"].isString()) {
                    config->downloadSettings = obj["downloadSettings"].toString();
                }
            }
            if (auto xmuxObj = obj["xmux"].toObject(); !xmuxObj.isEmpty()) {
                parseXHTTPXmuxObject(config, xmuxObj);
            }
        }
    }

    bool xrayTLS::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        if (query.hasQueryItem("sni")) serverName = query.queryItemValue("sni");
        if (query.hasQueryItem("peer")) serverName = query.queryItemValue("peer");
        if (query.hasQueryItem("server_name")) serverName = query.queryItemValue("server_name");
        if (query.hasQueryItem("pcs")) pinnedPeerCertSha256 = query.queryItemValue("pcs", QUrl::FullyDecoded);
        if (query.hasQueryItem("vcn")) verifyPeerCertByName = query.queryItemValue("vcn", QUrl::FullyDecoded);
        if (query.hasQueryItem("alpn")) alpn = query.queryItemValue("alpn", QUrl::FullyDecoded).split(",");
        if (query.hasQueryItem("fp")) fingerprint = query.queryItemValue("fp");
        return true;
    }

    bool xrayTLS::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("serverName")) serverName = object["serverName"].toString();
        if (object.contains("pinnedPeerCertSha256")) pinnedPeerCertSha256 = object["pinnedPeerCertSha256"].toString();
        if (object.contains("verifyPeerCertByName")) verifyPeerCertByName = object["verifyPeerCertByName"].toString();
        if (object.contains("alpn")) alpn = QJsonArray2QListString(object["alpn"].toArray());
        if (object.contains("fingerprint")) fingerprint = object["fingerprint"].toString();
        return true;
    }

    bool xrayTLS::ParseFromClash(const clash::Proxies& object) {
        if (!object.servername.empty()) {
            serverName = QString::fromStdString(object.servername);
        } else if (!object.sni.empty()) {
            serverName = QString::fromStdString(object.sni);
        } else {
            serverName = QString::fromStdString(object.server);
        }
        for (const auto& s : object.alpn) {
            alpn.append(QString::fromStdString(s));
        }
        if (!object.client_fingerprint.empty()) fingerprint = QString::fromStdString(object.client_fingerprint);
        return true;
    }

    QString xrayTLS::ExportToLink() {
        QUrlQuery query;
        query.addQueryItem("sni", serverName);
        if (!pinnedPeerCertSha256.isEmpty()) query.addQueryItem("pcs", pinnedPeerCertSha256);
        if (!verifyPeerCertByName.isEmpty()) query.addQueryItem("vcn", verifyPeerCertByName);
        if (!alpn.isEmpty()) query.addQueryItem("alpn", alpn.join(","));
        if (!fingerprint.isEmpty()) query.addQueryItem("fp", fingerprint);
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayTLS::ExportToJson() {
        QJsonObject object;
        object["serverName"] = serverName;
        if (!pinnedPeerCertSha256.isEmpty()) object["pinnedPeerCertSha256"] = pinnedPeerCertSha256;
        if (!verifyPeerCertByName.isEmpty()) object["verifyPeerCertByName"] = verifyPeerCertByName;
        if (!alpn.isEmpty()) {
            object["alpn"] = QListStr2QJsonArray(alpn);
        }
        if (!fingerprint.isEmpty()) object["fingerprint"] = fingerprint;
        return object;
    }

    BuildResult xrayTLS::Build() {
        auto obj = ExportToJson();
        if (fingerprint.isEmpty() && !Configs::dataManager->settingsRepo->utlsFingerprint.isEmpty()) obj["fingerprint"] = Configs::dataManager->settingsRepo->utlsFingerprint;
        return {obj, ""};
    }

    bool xrayReality::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        if (query.hasQueryItem("sni")) serverName = query.queryItemValue("sni");
        if (query.hasQueryItem("peer")) serverName = query.queryItemValue("peer");
        if (query.hasQueryItem("pbk")) password = query.queryItemValue("pbk");
        if (query.hasQueryItem("fp")) fingerprint = query.queryItemValue("fp");
        if (query.hasQueryItem("sid")) shortId = query.queryItemValue("sid");
        if (query.hasQueryItem("spx")) spiderX = query.queryItemValue("spx", QUrl::FullyDecoded);
        return true;

    }

    bool xrayReality::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("serverName")) serverName = object["serverName"].toString();
        if (object.contains("fingerprint")) fingerprint = object["fingerprint"].toString();
        if (object.contains("password")) password = object["password"].toString();
        if (object.contains("shortId")) shortId = object["shortId"].toString();
        if (object.contains("spiderX")) spiderX = object["spiderX"].toString();
        return true;
    }

    bool xrayReality::ParseFromClash(const clash::Proxies& object) {
        if (!object.servername.empty()) {
            serverName = QString::fromStdString(object.servername);
        } else if (!object.sni.empty()) {
            serverName = QString::fromStdString(object.sni);
        } else {
            serverName = QString::fromStdString(object.server);
        }
        if (!object.client_fingerprint.empty()) fingerprint = QString::fromStdString(object.client_fingerprint);
        if (!object.reality_opts.public_key.empty()) password = QString::fromStdString(object.reality_opts.public_key);
        if (!object.reality_opts.short_id.empty()) shortId = QString::fromStdString(object.reality_opts.short_id);
        return true;
    }

    QString xrayReality::ExportToLink() {
        QUrlQuery query;
        query.addQueryItem("sni", serverName);
        if (!fingerprint.isEmpty()) query.addQueryItem("fp", fingerprint);
        if (!password.isEmpty()) query.addQueryItem("pbk", password);
        if (!shortId.isEmpty()) query.addQueryItem("sid", shortId);
        if (!spiderX.isEmpty()) query.addQueryItem("spx", spiderX);
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayReality::ExportToJson() {
        QJsonObject object;
        object["serverName"] = serverName;
        if (!fingerprint.isEmpty()) object["fingerprint"] = fingerprint;
        if (!password.isEmpty()) object["password"] = password;
        if (!shortId.isEmpty()) object["shortId"] = shortId;
        if (!spiderX.isEmpty()) object["spiderX"] = spiderX;
        return object;
    }

    BuildResult xrayReality::Build() {
        auto obj = ExportToJson();
        if (fingerprint.isEmpty() && !Configs::dataManager->settingsRepo->utlsFingerprint.isEmpty()) obj["fingerprint"] = Configs::dataManager->settingsRepo->utlsFingerprint;
        return {obj, ""};
    }

    bool xrayXHTTP::ParseExtraJson(QString str) {
        const auto parseObject = [](const QString& source, QJsonObject& object) {
            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(source.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
            object = document.object();
            return true;
        };

        QJsonObject object;
        // Preserve valid JSON byte-for-byte semantically. The legacy fallback is
        // attempted only for providers that emit Python-style dicts, so words
        // such as "True" inside a valid quoted string are never rewritten.
        if (!parseObject(str, object)) {
            str = str.replace('\'', '"').replace("True", "true").replace("False", "false");
            if (!parseObject(str, object)) return false;
        }
        parseXHTTPExtraObject(this, object);
        return true;
    }


    bool xrayXHTTP::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        if (query.hasQueryItem("host")) host = query.queryItemValue("host");
        if (query.hasQueryItem("path")) path = query.queryItemValue("path", QUrl::FullyDecoded);
        if (query.hasQueryItem("mode")) mode = query.queryItemValue("mode");
        // Parse the nested object first. Explicit query fields below deliberately
        // override it, which lets a provider patch one setting without rebuilding
        // the complete extra JSON object.
        if (query.hasQueryItem("extra")
            && !ParseExtraJson(query.queryItemValue("extra", QUrl::FullyDecoded))) {
            return false;
        }
        if (query.hasQueryItem("headers")) {
            auto raw = query.queryItemValue("headers", QUrl::FullyDecoded);
            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(raw.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                headers = jsonObjectToQStringList(document.object());
            } else if (error.error == QJsonParseError::NoError && document.isArray()) {
                headers = QJsonArray2QListString(document.array());
            } else {
                headers = raw.split("|");
                if (headers.length() % 2 != 0) {
                    MW_show_log("Failed to import invalid headers: " + raw);
                    headers.clear();
                }
            }
        }
        parseQueryStringAliases(query,
                                {"x-padding-bytes", "x_padding_bytes", "xPaddingBytes"},
                                xPaddingBytes);
        parseQueryBoolAliases(query,
                              {"x-padding-obfs-mode", "x_padding_obfs_mode", "xPaddingObfsMode"},
                              xPaddingObfsMode);
        parseQueryStringAliases(query,
                                {"x-padding-key", "x_padding_key", "xPaddingKey"},
                                xPaddingKey);
        parseQueryStringAliases(query,
                                {"x-padding-header", "x_padding_header", "xPaddingHeader"},
                                xPaddingHeader);
        parseQueryStringAliases(query,
                                {"x-padding-placement", "x_padding_placement", "xPaddingPlacement"},
                                xPaddingPlacement);
        parseQueryStringAliases(query,
                                {"x-padding-method", "x_padding_method", "xPaddingMethod"},
                                xPaddingMethod);
        parseQueryStringAliases(query,
                                {"uplink-http-method", "uplink_http_method", "uplinkHTTPMethod"},
                                uplinkHTTPMethod);
        parseQueryStringAliases(query,
                                {"session-placement", "session_placement", "sessionPlacement",
                                 "session-id-placement", "session_id_placement", "sessionIDPlacement"},
                                sessionIDPlacement);
        parseQueryStringAliases(query,
                                {"session-key", "session_key", "sessionKey",
                                 "session-id-key", "session_id_key", "sessionIDKey"},
                                sessionIDKey);
        parseQueryStringAliases(query,
                                {"session-id-table", "session_id_table", "sessionIDTable"},
                                sessionIDTable);
        parseQueryStringAliases(query,
                                {"session-id-length", "session_id_length", "sessionIDLength"},
                                sessionIDLength);
        parseQueryStringAliases(query,
                                {"seq-placement", "seq_placement", "seqPlacement"},
                                seqPlacement);
        parseQueryStringAliases(query,
                                {"seq-key", "seq_key", "seqKey"},
                                seqKey);
        parseQueryStringAliases(query,
                                {"uplink-data-placement", "uplink_data_placement", "uplinkDataPlacement"},
                                uplinkDataPlacement);
        parseQueryStringAliases(query,
                                {"uplink-data-key", "uplink_data_key", "uplinkDataKey"},
                                uplinkDataKey);
        parseQueryStringAliases(query,
                                {"uplink-chunk-size", "uplink_chunk_size", "uplinkChunkSize"},
                                uplinkChunkSize);
        parseQueryBoolAliases(query,
                              {"no-grpc-header", "no_grpc_header", "noGRPCHeader"},
                              noGRPCHeader);
        parseQueryBoolAliases(query,
                              {"no-sse-header", "no_sse_header", "noSSEHeader"},
                              noSSEHeader);
        parseQueryStringAliases(query,
                                {"sc-max-each-post-bytes", "sc_max_each_post_bytes", "scMaxEachPostBytes"},
                                scMaxEachPostBytes);
        parseQueryStringAliases(query,
                                {"sc-min-posts-interval-ms", "sc_min_posts_interval_ms", "scMinPostsIntervalMs"},
                                scMinPostsIntervalMs);
        parseQueryLongLongAliases(query,
                                  {"sc-max-buffered-posts", "sc_max_buffered_posts", "scMaxBufferedPosts"},
                                  scMaxBufferedPosts);
        parseQueryStringAliases(query,
                                {"sc-stream-up-server-secs", "sc_stream_up_server_secs", "scStreamUpServerSecs"},
                                scStreamUpServerSecs);
        parseQueryIntAliases(query,
                             {"server-max-header-bytes", "server_max_header_bytes", "serverMaxHeaderBytes"},
                             serverMaxHeaderBytes);
        parseQueryStringAliases(query,
                                {"download-settings", "download_settings", "downloadSettings"},
                                downloadSettings);

        parseQueryStringAliases(query,
                                {"max-concurrency", "max_concurrency", "maxConcurrency"},
                                maxConcurrency);
        parseQueryStringAliases(query,
                                {"max-connections", "max_connections", "maxConnections"},
                                maxConnections);
        parseQueryStringAliases(query,
                                {"max-reuse-times", "max_reuse_times", "c_max_reuse_times", "cMaxReuseTimes"},
                                cMaxReuseTimes);
        parseQueryStringAliases(query,
                                {"max-request-times", "max_request_times", "h_max_request_times", "hMaxRequestTimes"},
                                hMaxRequestTimes);
        parseQueryStringAliases(query,
                                {"max-reusable-secs", "max_reusable_secs", "h_max_reusable_secs", "hMaxReusableSecs"},
                                hMaxReusableSecs);
        parseQueryLongLongAliases(query,
                                  {"keep-alive-period", "keep_alive_period", "h_keep_alive_period", "hKeepAlivePeriod"},
                                  hKeepAlivePeriod);
        return true;
    }

    bool xrayXHTTP::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("host")) host = object["host"].toString();
        if (object.contains("path")) path = object["path"].toString();
        if (object.contains("mode")) mode = object["mode"].toString();
        QJsonObject topLevelExtra;
        for (const auto& key : object.keys()) {
            if (key == "host" || key == "path" || key == "mode" || key == "extra") continue;
            topLevelExtra[key] = object[key];
        }
        if (!topLevelExtra.isEmpty()) {
            parseXHTTPExtraObject(this, topLevelExtra);
        }
        if (auto exObj = object["extra"].toObject(); !exObj.isEmpty()) {
            parseXHTTPExtraObject(this, exObj);
        } else if (object["extra"].isString()) {
            if (!ParseExtraJson(object["extra"].toString())) return false;
        } else if (object.contains("extra") && !object["extra"].isUndefined()
                   && !object["extra"].isNull()) {
            return false;
        }
        return true;
    }

    bool xrayXHTTP::ParseFromClash(const clash::Proxies& object) {
        const auto& opts = object.xhttp_opts;
        host = qs(opts.host);
        path = qs(opts.path);
        mode = hasText(opts.mode) ? qs(opts.mode) : "auto";

        xPaddingObfsMode = opts.x_padding_obfs_mode;
        headers.clear();
        for (const auto& [key, value] : opts.headers) headers << qs(key) << qs(value);
        xPaddingBytes = qs(opts.x_padding_bytes);
        xPaddingKey = qs(opts.x_padding_key);
        xPaddingHeader = qs(opts.x_padding_header);
        xPaddingPlacement = qs(opts.x_padding_placement);
        xPaddingMethod = qs(opts.x_padding_method);
        uplinkHTTPMethod = qs(opts.uplink_http_method);
        sessionIDPlacement = qs(opts.session_id_placement);
        sessionIDKey = qs(opts.session_id_key);
        sessionIDTable = qs(opts.session_id_table);
        sessionIDLength = qs(opts.session_id_length);
        seqPlacement = qs(opts.seq_placement);
        seqKey = qs(opts.seq_key);
        uplinkDataPlacement = qs(opts.uplink_data_placement);
        uplinkDataKey = qs(opts.uplink_data_key);
        uplinkChunkSize = qs(opts.uplink_chunk_size);
        noGRPCHeader = opts.no_grpc_header;
        noSSEHeader = opts.no_sse_header;
        scMaxEachPostBytes = qs(opts.sc_max_each_post_bytes);
        scMinPostsIntervalMs = qs(opts.sc_min_posts_interval_ms);
        if (hasText(opts.sc_max_buffered_posts)) {
            bool ok = false;
            const auto value = qs(opts.sc_max_buffered_posts).toLongLong(&ok);
            if (ok) scMaxBufferedPosts = value;
        }
        scStreamUpServerSecs = qs(opts.sc_stream_up_server_secs);
        serverMaxHeaderBytes = int(opts.server_max_header_bytes);

        maxConcurrency = qs(opts.reuse_settings.max_concurrency);
        maxConnections = qs(opts.reuse_settings.max_connections);
        cMaxReuseTimes = qs(opts.reuse_settings.c_max_reuse_times);
        hMaxRequestTimes = qs(opts.reuse_settings.h_max_request_times);
        hMaxReusableSecs = qs(opts.reuse_settings.h_max_reusable_secs);
        if (hasText(opts.reuse_settings.h_keep_alive_period)) {
            hKeepAlivePeriod = qs(opts.reuse_settings.h_keep_alive_period).toLongLong();
        }

        if (opts.has_download_settings) {
            downloadSettings = QJsonObject2QString(buildDownloadSettingsObject(opts.download_settings), true);
        }

        return true;
    }

    QString xrayXHTTP::ExportToLink() {
        QUrlQuery query;
        if (!host.isEmpty()) query.addQueryItem("host", host);
        if (!path.isEmpty()) query.addQueryItem("path", path);
        if (!mode.isEmpty()) query.addQueryItem("mode", mode);
        auto jsonExport = ExportToJson();
        if (jsonExport.contains("extra") && jsonExport["extra"].isObject()) {
            auto exObj = jsonExport["extra"].toObject();
            if (!exObj.isEmpty()) {
                query.addQueryItem("extra", QJsonObject2QString(exObj, true));
            }
        }
        if (!headers.isEmpty()) query.addQueryItem("headers", headers.join("|"));
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayXHTTP::ExportToJson() {
        QJsonObject obj;
        if (!host.isEmpty()) obj["host"] = host;
        if (!path.isEmpty()) obj["path"] = path;
        if (!mode.isEmpty()) obj["mode"] = mode;

        QJsonObject extraObj = rawExtra;
        if (!headers.isEmpty()) extraObj["headers"] = qStringListToJsonObject(headers);
        else extraObj.remove("headers");
        exportString(extraObj, "xPaddingBytes", xPaddingBytes);
        exportBool(extraObj, "xPaddingObfsMode", xPaddingObfsMode);
        exportString(extraObj, "xPaddingKey", xPaddingKey);
        exportString(extraObj, "xPaddingHeader", xPaddingHeader);
        exportString(extraObj, "xPaddingPlacement", xPaddingPlacement);
        exportString(extraObj, "xPaddingMethod", xPaddingMethod);
        exportString(extraObj, "uplinkHTTPMethod", uplinkHTTPMethod);
        exportString(extraObj, "sessionIDPlacement", sessionIDPlacement);
        exportString(extraObj, "sessionIDKey", sessionIDKey);
        exportString(extraObj, "sessionIDTable", sessionIDTable);
        exportString(extraObj, "sessionIDLength", sessionIDLength);
        exportString(extraObj, "seqPlacement", seqPlacement);
        exportString(extraObj, "seqKey", seqKey);
        exportString(extraObj, "uplinkDataPlacement", uplinkDataPlacement);
        exportString(extraObj, "uplinkDataKey", uplinkDataKey);
        exportString(extraObj, "uplinkChunkSize", uplinkChunkSize);
        exportBool(extraObj, "noGRPCHeader", noGRPCHeader);
        exportBool(extraObj, "noSSEHeader", noSSEHeader);
        exportString(extraObj, "scMaxEachPostBytes", scMaxEachPostBytes);
        exportString(extraObj, "scMinPostsIntervalMs", scMinPostsIntervalMs);
        exportLongLong(extraObj, "scMaxBufferedPosts", scMaxBufferedPosts);
        exportString(extraObj, "scStreamUpServerSecs", scStreamUpServerSecs);
        exportInt(extraObj, "serverMaxHeaderBytes", serverMaxHeaderBytes);
        if (!downloadSettings.isEmpty()) {
            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(downloadSettings.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                extraObj["downloadSettings"] = document.object();
            } else {
                extraObj.remove("downloadSettings");
            }
        } else {
            extraObj.remove("downloadSettings");
        }
        QJsonObject xmuxObj = rawXmux;
        exportString(xmuxObj, "maxConcurrency", maxConcurrency);
        exportString(xmuxObj, "maxConnections", maxConnections);
        exportString(xmuxObj, "cMaxReuseTimes", cMaxReuseTimes);
        exportString(xmuxObj, "hMaxRequestTimes", hMaxRequestTimes);
        exportString(xmuxObj, "hMaxReusableSecs", hMaxReusableSecs);
        exportLongLong(xmuxObj, "hKeepAlivePeriod", hKeepAlivePeriod);
        if (!xmuxObj.isEmpty()) extraObj["xmux"] = xmuxObj;
        else extraObj.remove("xmux");
        if (!extraObj.isEmpty()) obj["extra"] = extraObj;
        return obj;
    }

    QString xrayXHTTP::Validate() const {
        xhttp_validation::Input input;
        input.mode = mode.toStdString();
        if (headers.size() % 2 != 0) return "XHTTP headers must contain name/value pairs.";
        for (qsizetype index = 0; index < headers.size(); index += 2) {
            input.headerNames.push_back(headers[index].toStdString());
        }
        input.xPaddingBytes = xPaddingBytes.toStdString();
        input.xPaddingPlacement = xPaddingPlacement.toStdString();
        input.xPaddingMethod = xPaddingMethod.toStdString();
        input.uplinkHTTPMethod = uplinkHTTPMethod.toStdString();
        input.sessionIDPlacement = sessionIDPlacement.toStdString();
        input.sessionIDTable = sessionIDTable.toStdString();
        input.sessionIDLength = sessionIDLength.toStdString();
        input.seqPlacement = seqPlacement.toStdString();
        input.uplinkDataPlacement = uplinkDataPlacement.toStdString();
        input.uplinkChunkSize = uplinkChunkSize.toStdString();
        input.scMaxEachPostBytes = scMaxEachPostBytes.toStdString();
        input.scMinPostsIntervalMs = scMinPostsIntervalMs.toStdString();
        input.scStreamUpServerSecs = scStreamUpServerSecs.toStdString();
        input.scMaxBufferedPosts = scMaxBufferedPosts;
        input.serverMaxHeaderBytes = serverMaxHeaderBytes;
        input.maxConcurrency = maxConcurrency.toStdString();
        input.maxConnections = maxConnections.toStdString();
        input.cMaxReuseTimes = cMaxReuseTimes.toStdString();
        input.hMaxRequestTimes = hMaxRequestTimes.toStdString();
        input.hMaxReusableSecs = hMaxReusableSecs.toStdString();
        input.downloadSettingsPresent = !downloadSettings.trimmed().isEmpty();
        if (input.downloadSettingsPresent) {
            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(downloadSettings.toUtf8(), &error);
            input.downloadSettingsValid = error.error == QJsonParseError::NoError && document.isObject();
        }

        const auto result = xhttp_validation::Validate(input);
        return result.ok ? QString() : QString::fromStdString(result.message);
    }

    BuildResult xrayXHTTP::Build() {
        if (const auto error = Validate(); !error.isEmpty()) return {{}, error};
        return {ExportToJson(), ""};
    }

    bool xrayWS::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());
        if (query.hasQueryItem("host")) host = query.queryItemValue("host");
        if (query.hasQueryItem("path")) path = query.queryItemValue("path", QUrl::FullyDecoded);
        if (query.hasQueryItem("ed")) ed = query.queryItemValue("ed").toInt();
        if (query.hasQueryItem("headers")) {
            auto raw = query.queryItemValue("headers", QUrl::FullyDecoded);
            headers = raw.split("|");
            if (headers.length()%2 != 0) {
                MW_show_log("Failed to import invalid headers: " + raw);
                headers.clear();
            }
        }
        if (query.hasQueryItem("heartbeat_period")) heartbeatPeriod = query.queryItemValue("heartbeat_period").toInt();
        return true;
    }

    bool xrayWS::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("path")) {
            path = object["path"].toString();
            if (path.contains("?ed=")) {
                auto spl = path.split("?ed=");
                path = spl[0];
                ed = spl[1].toInt();
            }
        }
        if (object.contains("host")) host = object["host"].toString();
        if (object.contains("headers") && object["headers"].isObject()) {
            auto headerObject = object["headers"].toObject();
            // Xray 26.x moved WebSocket Host out of headers.  Consume legacy
            // subscriptions but emit the modern independent host field.
            if (host.isEmpty()) {
                for (auto it = headerObject.begin(); it != headerObject.end(); ++it) {
                    if (it.key().compare("Host", Qt::CaseInsensitive) == 0) {
                        host = it.value().toString();
                        headerObject.erase(it);
                        break;
                    }
                }
            }
            headers = jsonObjectToQStringList(headerObject);
        }
        if (object.contains("heartbeatPeriod")) heartbeatPeriod = object["heartbeatPeriod"].toInt(0);
        return true;
    }

    bool xrayWS::ParseFromClash(const clash::Proxies& object) {
        if (!object.ws_opts.path.empty()) {
            path = QString::fromStdString(object.ws_opts.path);
            if (path.contains("?ed=")) {
                auto spl = path.split("?ed=");
                path = spl[0];
                ed = spl[1].toInt();
            }
        }
        if (!object.servername.empty()) {
            host = QString::fromStdString(object.servername);
        } else if (object.ws_opts.headers.contains("Host")) {
            host = QString::fromStdString(object.ws_opts.headers.at("Host"));
        }
        if (!object.ws_opts.headers.empty()) {
            for (const auto& [key, value] : object.ws_opts.headers) {
                if (QString::fromStdString(key).compare("Host", Qt::CaseInsensitive) == 0) continue;
                headers.append(QString::fromStdString(key) + "=" + QString::fromStdString(value));
            }
        }
        return true;
    }

    QString xrayWS::ExportToLink() {
        QUrlQuery query;
        if (!host.isEmpty()) query.addQueryItem("host", host);
        if (!path.isEmpty() && path != "/") query.addQueryItem("path", path);
        if (ed > 0) query.addQueryItem("ed", QString::number(ed));
        if (!headers.isEmpty()) query.addQueryItem("headers", headers.join("|"));
        if (heartbeatPeriod > 0) query.addQueryItem("heartbeat_period", QString::number(heartbeatPeriod));
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayWS::ExportToJson() {
        QJsonObject obj;
        if (!path.isEmpty()) {
            auto fullPath = path;
            if (ed > 0) fullPath += "?ed=" + QString::number(ed);
            obj["path"] = fullPath;
        }
        if (!host.isEmpty()) obj["host"] = host;
        if (!headers.isEmpty()) obj["headers"] = qStringListToJsonObject(headers);
        if (heartbeatPeriod > 0) obj["heartbeatPeriod"] = heartbeatPeriod;
        return obj;
    }

    BuildResult xrayWS::Build() {
        return {ExportToJson(), ""};
    }

    bool xrayHttpUpgrade::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());
        if (query.hasQueryItem("host")) host = query.queryItemValue("host");
        if (query.hasQueryItem("path")) path = query.queryItemValue("path", QUrl::FullyDecoded);
        if (query.hasQueryItem("ed")) ed = query.queryItemValue("ed").toInt();
        if (query.hasQueryItem("headers")) {
            auto raw = query.queryItemValue("headers", QUrl::FullyDecoded);
            headers = raw.split("|");
            if (headers.size() % 2 != 0) headers.clear();
        }
        return true;
    }

    bool xrayHttpUpgrade::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("path")) {
            path = object["path"].toString();
            if (path.contains("?ed=")) {
                auto spl = path.split("?ed=");
                path = spl[0];
                ed = spl[1].toInt();
            }
        }
        if (object.contains("host")) host = object["host"].toString();
        if (object.contains("headers") && object["headers"].isObject())
            headers = jsonObjectToQStringList(object["headers"].toObject());
        return true;
    }

    bool xrayHttpUpgrade::ParseFromClash(const clash::Proxies& object) {
        if (!object.ws_opts.path.empty()) {
            path = QString::fromStdString(object.ws_opts.path);
            if (path.contains("?ed=")) {
                auto spl = path.split("?ed=");
                path = spl[0];
                ed = spl[1].toInt();
            }
        }
        if (!object.servername.empty()) {
            host = QString::fromStdString(object.servername);
        } else if (object.ws_opts.headers.contains("Host")) {
            host = QString::fromStdString(object.ws_opts.headers.at("Host"));
        }
        if (!object.ws_opts.headers.empty()) {
            for (const auto& [key, value] : object.ws_opts.headers) {
                headers.append(QString::fromStdString(key) + "=" + QString::fromStdString(value));
            }
        }
        return true;
    }

    QString xrayHttpUpgrade::ExportToLink() {
        QUrlQuery query;
        if (!host.isEmpty()) query.addQueryItem("host", host);
        if (!path.isEmpty() && path != "/") query.addQueryItem("path", path);
        if (ed > 0) query.addQueryItem("ed", QString::number(ed));
        if (!headers.isEmpty()) query.addQueryItem("headers", headers.join("|"));
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayHttpUpgrade::ExportToJson() {
        QJsonObject obj;
        if (!path.isEmpty()) {
            auto fullPath = path;
            if (ed > 0) fullPath += "?ed=" + QString::number(ed);
            obj["path"] = fullPath;
        }
        if (!host.isEmpty()) obj["host"] = host;
        if (!headers.isEmpty()) obj["headers"] = qStringListToJsonObject(headers);
        return obj;
    }

    BuildResult xrayHttpUpgrade::Build() {
        return {ExportToJson(), ""};
    }

    bool xrayGRPC::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());
        if (query.hasQueryItem("authority")) authority = query.queryItemValue("authority", QUrl::FullyDecoded);
        if (query.hasQueryItem("serviceName")) serviceName = query.queryItemValue("serviceName", QUrl::FullyDecoded);
        if (query.hasQueryItem("mode") && query.queryItemValue("mode") == "multi") multiMode = true;
        return true;
    }

    bool xrayGRPC::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("authority")) authority = object["authority"].toString();
        if (object.contains("serviceName")) serviceName = object["serviceName"].toString();
        if (object.contains("multiMode")) multiMode = object["multiMode"].toBool();
        return true;
    }

    bool xrayGRPC::ParseFromClash(const clash::Proxies& object) {
        if (!object.grpc_opts.grpc_service_name.empty()) serviceName = QString::fromStdString(object.grpc_opts.grpc_service_name);
        return true;
    }

    QString xrayGRPC::ExportToLink() {
        QUrlQuery query;
        if (!authority.isEmpty()) query.addQueryItem("authority", authority);
        if (!serviceName.isEmpty()) query.addQueryItem("serviceName", serviceName);
        if (multiMode) query.addQueryItem("mode", "multi");
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayGRPC::ExportToJson() {
        QJsonObject obj;
        if (!authority.isEmpty()) obj["authority"] = authority;
        if (!serviceName.isEmpty()) obj["serviceName"] = serviceName;
        if (multiMode) obj["multiMode"] = multiMode;
        return obj;
    }

    BuildResult xrayGRPC::Build() {
        return {ExportToJson(), ""};
    }

    bool xrayStreamSetting::ParseFromLink(const QString &link) {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        if (query.hasQueryItem("type")) network = query.queryItemValue("type").replace("tcp", "raw");
        if (!Configs::XrayNetworks.contains(network)) return false;
        if (query.hasQueryItem("security")) security = query.queryItemValue("security");
        if (security == "tls") TLS->ParseFromLink(link);
        else if (security == "reality") reality->ParseFromLink(link);
        if (network == "xhttp") xhttp->ParseFromLink(link);
        else if (network == "ws") ws->ParseFromLink(link);
        else if (network == "httpupgrade") httpupgrade->ParseFromLink(link);
        else if (network == "grpc") grpc->ParseFromLink(link);

        return true;
    }

    bool xrayStreamSetting::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;

        if (object.contains("network")) network = object.value("network").toString();
        if (!Configs::XrayNetworks.contains(network)) return false;
        if (object.contains("security")) security = object.value("security").toString();
        if (security == "tls" && object["tlsSettings"].isObject()) TLS->ParseFromJson(object["tlsSettings"].toObject());
        else if (security == "reality" && object["realitySettings"].isObject()) reality->ParseFromJson(object["realitySettings"].toObject());
        if (network == "xhttp" && object["xhttpSettings"].isObject()) xhttp->ParseFromJson(object["xhttpSettings"].toObject());
        if (network == "ws" && object["wsSettings"].isObject()) ws->ParseFromJson(object["wsSettings"].toObject());
        if (network == "httpupgrade" && object["httpupgradeSettings"].isObject()) httpupgrade->ParseFromJson(object["httpupgradeSettings"].toObject());
        if (network == "grpc" && object["grpcSettings"].isObject()) grpc->ParseFromJson(object["grpcSettings"].toObject());
        return true;
    }

    bool xrayStreamSetting::ParseFromClash(const clash::Proxies& object) {
        if (!object.network.empty()) network = QString::fromStdString(object.network);
        if (network != "raw" && network != "ws" && network != "grpc" && network != "xhttp") return false;
        if (object.tls) {
            if (object.reality_opts.public_key.empty()) {
                security = "tls";
                TLS->ParseFromClash(object);
            } else {
                security = "reality";
                reality->ParseFromClash(object);
            }
        }
        if (network == "xhttp") xhttp->ParseFromClash(object);
        if (network == "ws") {
            if (object.ws_opts.v2ray_http_upgrade) {
                network = "httpupgrade";
                httpupgrade->ParseFromClash(object);
            } else {
                ws->ParseFromClash(object);
            }
        }
        if (network == "grpc") grpc->ParseFromClash(object);
        return true;
    }

    QString xrayStreamSetting::ExportToLink() {
        QUrlQuery query;
        if (!network.isEmpty()) query.addQueryItem("type", network);
        if (!security.isEmpty()) query.addQueryItem("security", security);
        if (security == "tls") mergeUrlQuery(query, TLS->ExportToLink());
        if (security == "reality") mergeUrlQuery(query, reality->ExportToLink());
        if (network == "xhttp") mergeUrlQuery(query, xhttp->ExportToLink());
        if (network == "ws") mergeUrlQuery(query, ws->ExportToLink());
        if (network == "httpupgrade") mergeUrlQuery(query, httpupgrade->ExportToLink());
        if (network == "grpc") mergeUrlQuery(query, grpc->ExportToLink());
        return query.toString(QUrl::FullyEncoded);
    }

    QJsonObject xrayStreamSetting::ExportToJson() {
        QJsonObject object;
        object["network"] = network;
        object["security"] = security;
        if (security == "tls") object["tlsSettings"] = TLS->ExportToJson();
        else if (security == "reality") object["realitySettings"] = reality->ExportToJson();
        if (network == "xhttp") object["xhttpSettings"] = xhttp->ExportToJson();
        if (network == "ws") object["wsSettings"] = ws->ExportToJson();
        if (network == "httpupgrade") object["httpupgradeSettings"] = httpupgrade->ExportToJson();
        if (network == "grpc") object["grpcSettings"] = grpc->ExportToJson();
        return object;
    }

    QJsonObject xrayStreamSetting::ExportIdentity() {
        QJsonObject object;
        object["network"] = network;
        object["security"] = security;
        if (security == "reality") {
            if (!reality->serverName.isEmpty()) object["sni"] = reality->serverName;
            if (!reality->fingerprint.isEmpty()) object["fingerprint"] = reality->fingerprint;
        } else if (security == "tls") {
            if (!TLS->serverName.isEmpty()) object["sni"] = TLS->serverName;
            if (!TLS->fingerprint.isEmpty()) object["fingerprint"] = TLS->fingerprint;
        }
        return object;
    }


    QString getXrayOutboundDomainStrategy() {
        auto strategy = Configs::dataManager->settingsRepo->direct_dns_strategy;
        if (strategy == "prefer_ipv4") return "UseIPv4v6";
        if (strategy == "prefer_ipv6") return "UseIPv6v4";
        if (strategy == "ipv4_only") return "ForceIPv4";
        if (strategy == "ipv6_only") return "ForceIPv6";
        return "UseIP";
    }

    BuildResult xrayStreamSetting::Build() {
        // Egress interface binding and outbound domain resolution are wired onto
        // the Xray instance after creation (ThroneWiring), not baked into the
        // config, so no sockopt is emitted here.
        auto object = ExportToJson();
        if (network == "xhttp") {
            const auto built = xhttp->Build();
            if (!built.error.isEmpty()) return {{}, "Invalid XHTTP settings: " + built.error};
            object["xhttpSettings"] = built.object;
        }
        return {object, ""};
    }
}
