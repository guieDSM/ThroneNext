#include <cassert>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include "include/database/entities/QuickRoutePolicy.hpp"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace Configs::QuickRoute;

    assert(registrableDomain("m.shop.example.co.uk") == "example.co.uk");
    assert(registrableDomain("www.example.com") == "example.com");
    assert(registrableDomain("user.github.io") == "user.github.io");
    assert(registrableDomain("co.uk").isEmpty());

    const auto site = parseSiteOrIp("https://www.example.com/product/123?x=1");
    assert(site.kind == "site" && site.value == "example.com");
    assert(parseSiteOrIp("www.example.com").value == "example.com");
    const auto ip = parseSiteOrIp("https://192.0.2.10/path");
    assert(ip.kind == "ip" && ip.value == "192.0.2.10/32");
    const auto cidr = parseSiteOrIp("2001:db8::/32");
    assert(cidr.kind == "ip" && cidr.value.endsWith("/32"));
    assert(!parseSiteOrIp("file:///etc/passwd").error.isEmpty());

    Configs::RouteProfile coveredProfile;
    coveredProfile.isRaw = true;
    coveredProfile.rawRoute = R"({"rules":[{"action":"route","outbound":"direct","domain_suffix":["example.com","example.org"]}]})";
    const QJsonObject requested{{"kind", "site"}, {"domains", QJsonArray{"www.example.com", "s8.example.org", "other.example.net"}}};
    QStringList coveredSelectors;
    const auto reduced = withoutCoveredSelectors(coveredProfile, requested, &coveredSelectors);
    assert(reduced.value("domains").toArray() == QJsonArray{"example.net"});
    assert(coveredSelectors.size() == 2);
    assert(withoutCoveredSelectors(coveredProfile, QJsonObject{{"kind", "site"},
        {"domains", QJsonArray{"example.com"}}}).isEmpty());

    Configs::RouteProfile orderedProfile;
    auto proxyFirst = std::make_shared<Configs::RouteRule>();
    proxyFirst->domain_suffix = {"example.com"};
    proxyFirst->outboundID = Configs::proxyID;
    auto directSecond = std::make_shared<Configs::RouteRule>();
    directSecond->domain_suffix = {"example.com"};
    directSecond->outboundID = Configs::directID;
    orderedProfile.Rules = {proxyFirst, directSecond};
    assert(!withoutCoveredSelectors(orderedProfile,
        QJsonObject{{"kind", "site"}, {"domains", QJsonArray{"example.com"}}}).isEmpty());
    orderedProfile.Rules = {directSecond, proxyFirst};
    assert(withoutCoveredSelectors(orderedProfile,
        QJsonObject{{"kind", "site"}, {"domains", QJsonArray{"example.com"}}}).isEmpty());

    const auto diagnostic = analyzeConsoleText(
        "GET https://ads.example.net/js/ad.js net::ERR_BLOCKED_BY_CLIENT\n"
        "GET https://s8.example.org/x/segment/index.m3u8 404 (Not Found)\n"
        "GET https://api2direct.example.edu/hello net::ERR_TIMED_OUT\n"
        "Uncaught TypeError: jQuery(...).live is not a function\n",
        "example.com");
    assert(diagnostic.browserBlocked == 1 && diagnostic.httpErrors == 1 && diagnostic.scriptErrors == 1);
    assert(diagnostic.candidates.size() == 2);
    assert(diagnostic.candidates.at(0).domain == "example.org");
    assert(diagnostic.candidates.at(1).domain == "example.edu");

    const auto harEntry = [](const QString& url, int status, const QString& kind,
                             const QString& body = {}, const QString& networkError = {}) {
        return QJsonObject{{"request", QJsonObject{{"url", url}}},
            {"response", QJsonObject{{"status", status},
                {"content", QJsonObject{{"mimeType", "text/html"}, {"text", body}}}}},
            {"_resourceType", kind}, {"_error", networkError}};
    };
    const QJsonArray harEntries{
        harEntry("https://example.com/episodes/test", 200, "document", "<title>Episode</title>"),
        harEntry("https://example.org/player?id=private", 200, "document",
                 "<html><title>404 Not Found</title><h1>404 Not Found</h1></html>"),
        harEntry("https://ads.example.net/ad.js", 0, "script", {}, "net::ERR_BLOCKED_BY_CLIENT"),
        harEntry("https://s8.example.org/video/index.m3u8", 404, "media"),
        harEntry("https://metrics.example.edu/metrika.js", 200, "script")};
    const QByteArray har = QJsonDocument(QJsonObject{{"log", QJsonObject{{"entries", harEntries}}}})
                               .toJson(QJsonDocument::Compact);
    QString harError;
    const auto harDiagnostic = analyzeHar(har, "example.com", &harError);
    assert(harError.isEmpty());
    assert(harDiagnostic.htmlErrorPages == 1 && harDiagnostic.httpErrors == 1);
    assert(harDiagnostic.browserBlocked == 1 && harDiagnostic.candidates.size() == 1);
    assert(harDiagnostic.candidates.first().domain == "example.org");
    assert(harDiagnostic.candidates.first().url.isEmpty());
    assert(analyzeHar("{}", "example.com", &harError).candidates.isEmpty());
    assert(!harError.isEmpty());

    const QJsonObject record{{"id", "test"}, {"kind", "site"}, {"display", "example.com"},
                             {"domains", QJsonArray{"example.com", "player.example.net"}}};
    Configs::RouteProfile raw;
    raw.isRaw = true;
    raw.rawRoute = R"({"rules":[{"action":"route","domain_suffix":["old.example"],"outbound":-1}],"final":-1})";
    QString error;
    assert(insertRule(raw, record, &error));
    const auto inserted = QJsonDocument::fromJson(raw.rawRoute.toUtf8()).object();
    assert(inserted.value("rules").toArray().size() == 2);
    assert(inserted.value("rules").toArray().first().toObject() == directRule(record));
    assert(inserted.value("final").toInt() == -1);
    assert(!insertRule(raw, record, &error));
    assert(!error.isEmpty());
    assert(removeRule(raw, record, &error));
    assert(QJsonDocument::fromJson(raw.rawRoute.toUtf8()).object().value("rules").toArray().size() == 1);

    Configs::RouteProfile structured;
    assert(insertRule(structured, record, &error));
    assert(structured.Rules.size() == 1);
    assert(structured.Rules.first()->domain_suffix.contains("player.example.net"));
    assert(!insertRule(structured, record, &error));
    assert(structured.Rules.size() == 1);
    assert(removeRule(structured, record, &error));
    assert(structured.Rules.isEmpty());

    const QJsonObject appRecord{{"id", "steam"}, {"kind", "app"}, {"display", "Steam"},
                                {"paths", QJsonArray{"C:\\Steam\\steam.exe", "C:\\Steam\\steamwebhelper.exe"}}};
    assert(insertRule(raw, appRecord, &error));
    assert(QJsonDocument::fromJson(raw.rawRoute.toUtf8()).object().value("rules").toArray().first()
        .toObject().contains("process_path"));
    assert(removeRule(raw, appRecord, &error));
    assert(insertRule(structured, appRecord, &error));
    assert(structured.Rules.first()->process_path.size() == 2);
    assert(removeRule(structured, appRecord, &error));

    const QList<QJsonObject> records{record};
    assert(readRecords(writeRecords(records)).size() == 1);

    if (argc > 1) {
        QFile capture(QString::fromLocal8Bit(argv[1]));
        assert(capture.open(QIODevice::ReadOnly));
        QString error;
        const auto result = analyzeHar(capture.readAll(), "example.com", &error);
        assert(error.isEmpty());
        QTextStream out(stdout);
        out << "html_error_pages=" << result.htmlErrorPages << '\n';
        for (const auto& candidate : result.candidates)
            out << "candidate=" << candidate.domain << " issue=" << candidate.issue << '\n';
    }
}
