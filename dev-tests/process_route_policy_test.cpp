#include <cassert>

#include <QJsonDocument>

#include "include/database/entities/ProcessRoutePolicy.hpp"

using Configs::ProcessRoutePolicy::jsonUsesProcessSelectors;

static QJsonValue parse(const char* json) {
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json));
    return document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array());
}

int main() {
    assert(jsonUsesProcessSelectors(parse(
        R"({"rules":[{"process_name":["browser.exe"]}]})")));
    assert(jsonUsesProcessSelectors(parse(
        R"({"rules":[{"type":"logical","rules":[{"process_path":["C:\\Apps\\browser.exe"]}]}]})")));
    assert(jsonUsesProcessSelectors(parse(
        R"({"rules":[{"process_path_regex":".*browser.*"}]})")));
    assert(!jsonUsesProcessSelectors(parse(
        R"({"rules":[{"process_name":[]},{"domain_suffix":["example.com"]}]})")));
    assert(!jsonUsesProcessSelectors(parse(
        R"({"rules":[{"process_path":"   "}]})")));
    return 0;
}
