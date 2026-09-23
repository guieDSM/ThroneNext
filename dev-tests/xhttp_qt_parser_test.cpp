#include "include/configs/common/xrayStreamSetting.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

#include <cassert>

// Minimal implementations of the generic JSON helpers used by the production
// parser. Keeping them here lets this focused test link without the database/UI
// portions of Throne.
QJsonObject QString2QJsonObject(const QString &jsonString) {
    return QJsonDocument::fromJson(jsonString.toUtf8()).object();
}

QString QJsonObject2QString(const QJsonObject &jsonObject, bool compact) {
    return QJsonDocument(jsonObject).toJson(
        compact ? QJsonDocument::Compact : QJsonDocument::Indented);
}

QList<QString> QJsonArray2QListString(const QJsonArray &array) {
    QList<QString> result;
    for (const auto &value : array) result.append(value.toString());
    return result;
}

QJsonArray QListStr2QJsonArray(const QList<QString> &list) {
    QJsonArray result;
    for (const auto &value : list) result.append(value);
    return result;
}

namespace Configs {
    void mergeUrlQuery(QUrlQuery &baseQuery, const QString &strQuery) {
        const QUrlQuery query(strQuery);
        for (const auto &item : query.queryItems()) baseQuery.addQueryItem(item.first, item.second);
    }

    QStringList jsonObjectToQStringList(const QJsonObject &object) {
        QStringList result;
        for (const auto &key : object.keys()) result << key << object.value(key).toString();
        return result;
    }

    QJsonObject qStringListToJsonObject(const QStringList &list) {
        QJsonObject result;
        if (list.size() % 2 != 0) return result;
        for (qsizetype index = 0; index < list.size(); index += 2) {
            result[list[index]] = list[index + 1];
        }
        return result;
    }
}

int main() {
    {
        Configs::xrayWS ws;
        assert(ws.ParseFromJson(QJsonObject{
            {"path", "/legacy"},
            {"headers", QJsonObject{{"Host", "cdn.example.test"}, {"X-Test", "1"}}},
        }));
        assert(ws.host == "cdn.example.test");
        const auto modern = ws.ExportToJson();
        assert(modern.value("host").toString() == "cdn.example.test");
        const auto headers = modern.value("headers").toObject();
        assert(!headers.contains("Host"));
        assert(headers.value("X-Test").toString() == "1");
    }
    MW_show_log = [](const QString &) {};

    const auto extra = QString::fromLatin1(QUrl::toPercentEncoding(
        R"json({"uplinkHTTPMethod":"PUT","marker":"True story","xmux":{"future":7}})json"));
    const auto headers = QString::fromLatin1(QUrl::toPercentEncoding(
        R"json({"User-Agent":"Canary","X-Test":"one|two"})json"));
    const auto link = QString(
        "vless://id@example.test:443?type=xhttp&mode=packet-up&extra=%1&headers=%2&"
        "uplinkHTTPMethod=GET&max_concurrency=2&maxConcurrency=4&"
        "c_max_reuse_times=3&hMaxRequestTimes=9&h_keep_alive_period=30")
                          .arg(extra, headers);

    Configs::xrayXHTTP parsed;
    assert(parsed.ParseFromLink(link));
    assert(parsed.mode == "packet-up");
    assert(parsed.uplinkHTTPMethod == "GET");
    assert(parsed.maxConcurrency == "4");
    assert(parsed.cMaxReuseTimes == "3");
    assert(parsed.hMaxRequestTimes == "9");
    assert(parsed.hKeepAlivePeriod == 30);
    assert(parsed.headers == QStringList({"User-Agent", "Canary", "X-Test", "one|two"}));

    const auto exported = parsed.ExportToJson();
    const auto exportedExtra = exported.value("extra").toObject();
    assert(exportedExtra.value("uplinkHTTPMethod").toString() == "GET");
    assert(exportedExtra.value("marker").toString() == "True story");
    assert(exportedExtra.value("headers").toObject().value("X-Test").toString() == "one|two");
    assert(exportedExtra.value("xmux").toObject().value("maxConcurrency").toString() == "4");
    assert(parsed.Validate().isEmpty());

    Configs::xrayXHTTP invalid;
    invalid.mode = "stream-up";
    invalid.uplinkHTTPMethod = "GET";
    assert(invalid.Validate().contains("packet-up"));

    const auto malformedExtra = QString::fromLatin1(
        QUrl::toPercentEncoding(R"json({"uplinkHTTPMethod":)json"));
    Configs::xrayXHTTP malformedLink;
    assert(!malformedLink.ParseFromLink(
        QString("vless://id@example.test:443?type=xhttp&extra=%1")
            .arg(malformedExtra)));

    Configs::xrayXHTTP malformedJson;
    assert(!malformedJson.ParseFromJson(QJsonObject{
        {"host", "example.test"},
        {"extra", "not-json"},
    }));
}
