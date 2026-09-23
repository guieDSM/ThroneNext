#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace Configs::ProcessRoutePolicy {
    inline bool jsonUsesProcessSelectors(const QJsonValue& value) {
        if (value.isArray()) {
            for (const auto& item : value.toArray()) {
                if (jsonUsesProcessSelectors(item)) return true;
            }
            return false;
        }
        if (!value.isObject()) return false;

        const QJsonObject object = value.toObject();
        const QStringList selectors = {
            QStringLiteral("process_name"),
            QStringLiteral("process_path"),
            QStringLiteral("process_path_regex"),
        };
        for (const QString& selector : selectors) {
            const QJsonValue field = object.value(selector);
            if ((field.isString() && !field.toString().trimmed().isEmpty()) ||
                (field.isArray() && !field.toArray().isEmpty())) {
                return true;
            }
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (jsonUsesProcessSelectors(it.value())) return true;
        }
        return false;
    }
}
