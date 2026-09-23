#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace Configs::SimpleRouteInput {
    enum class Kind {
        Ignored,
        ExactDomain,
        DomainSuffix,
        DomainKeyword,
        DomainRegex,
        RuleSet,
        IpCidr,
        ProcessName,
        ProcessPath,
        Invalid,
    };

    struct ParsedLine {
        Kind kind = Kind::Invalid;
        QString value;
        QString error;

        [[nodiscard]] bool isValid() const { return kind != Kind::Invalid; }
        [[nodiscard]] bool isIgnored() const { return kind == Kind::Ignored; }
    };

    inline bool isAsciiDomainLabelValid(const QByteArray& label) {
        if (label.isEmpty() || label.size() > 63) return false;
        if (label.front() == '-' || label.back() == '-') return false;
        for (const char c : label) {
            const bool alphaNumeric = (c >= 'a' && c <= 'z') ||
                                      (c >= 'A' && c <= 'Z') ||
                                      (c >= '0' && c <= '9');
            if (!alphaNumeric && c != '-' && c != '_') return false;
        }
        return true;
    }

    inline QString normalizeDomain(QString value, const bool suffix, QString* error) {
        value = value.trimmed();
        if (suffix) {
            if (value.startsWith("*.")) value.remove(0, 2);
            while (value.startsWith('.')) value.remove(0, 1);
        } else if (value.startsWith("*.") || value.startsWith('.')) {
            if (error) *error = QStringLiteral("wildcards and leading dots require suffix:");
            return {};
        }

        while (value.endsWith('.')) value.chop(1);
        if (value.isEmpty()) {
            if (error) *error = QStringLiteral("domain is empty");
            return {};
        }
        if (value.contains('*') || value.contains('/') || value.contains('\\') ||
            value.contains(':') || value.contains('@') || value.contains('?') ||
            value.contains('#')) {
            if (error) *error = QStringLiteral("expected a hostname, not a URL, wildcard, or path");
            return {};
        }
        for (const QChar c : value) {
            if (c.isSpace()) {
                if (error) *error = QStringLiteral("domain contains whitespace");
                return {};
            }
        }

        const QByteArray ace = QUrl::toAce(value);
        if (ace.isEmpty() || ace.size() > 253) {
            if (error) *error = QStringLiteral("domain is not valid");
            return {};
        }
        const QList<QByteArray> labels = ace.split('.');
        for (const QByteArray& label : labels) {
            if (!isAsciiDomainLabelValid(label)) {
                if (error) *error = QStringLiteral("domain contains an invalid label");
                return {};
            }
        }
        return QString::fromLatin1(ace).toLower();
    }

    inline ParsedLine parseLine(const QString& raw) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) return {Kind::Ignored, {}, {}};

        const qsizetype colon = line.indexOf(':');
        if (colon < 0) {
            QString error;
            const QString domain = normalizeDomain(line, true, &error);
            if (domain.isEmpty()) {
                return {Kind::Invalid, {}, error.isEmpty()
                    ? QStringLiteral("unknown rule; add a supported prefix")
                    : error};
            }
            return {Kind::DomainSuffix, domain, {}};
        }

        const QString prefix = line.left(colon).trimmed().toCaseFolded();
        QString value = line.mid(colon + 1).trimmed();
        if (value.isEmpty()) return {Kind::Invalid, {}, QStringLiteral("rule value is empty")};

        if (prefix == QStringLiteral("domain")) {
            QString error;
            value = normalizeDomain(value, false, &error);
            return value.isEmpty() ? ParsedLine{Kind::Invalid, {}, error}
                                   : ParsedLine{Kind::ExactDomain, value, {}};
        }
        if (prefix == QStringLiteral("suffix") || prefix == QStringLiteral("domain_suffix")) {
            QString error;
            value = normalizeDomain(value, true, &error);
            return value.isEmpty() ? ParsedLine{Kind::Invalid, {}, error}
                                   : ParsedLine{Kind::DomainSuffix, value, {}};
        }
        if (prefix == QStringLiteral("keyword") || prefix == QStringLiteral("domain_keyword"))
            return {Kind::DomainKeyword, value, {}};
        if (prefix == QStringLiteral("regex") || prefix == QStringLiteral("domain_regex"))
            return {Kind::DomainRegex, value, {}};
        if (prefix == QStringLiteral("ruleset") || prefix == QStringLiteral("rule_set"))
            return {Kind::RuleSet, value, {}};
        if (prefix == QStringLiteral("ip") || prefix == QStringLiteral("cidr") ||
            prefix == QStringLiteral("ip_cidr"))
            return {Kind::IpCidr, value, {}};
        if (prefix == QStringLiteral("processname") || prefix == QStringLiteral("process_name"))
            return {Kind::ProcessName, value, {}};
        if (prefix == QStringLiteral("processpath") || prefix == QStringLiteral("process_path"))
            return {Kind::ProcessPath, value, {}};

        return {Kind::Invalid, {}, QStringLiteral("unknown rule prefix: %1").arg(line.left(colon).trimmed())};
    }

    inline QString canonicalText(const ParsedLine& parsed) {
        switch (parsed.kind) {
            case Kind::ExactDomain: return QStringLiteral("domain:") + parsed.value;
            case Kind::DomainSuffix: return QStringLiteral("suffix:") + parsed.value;
            case Kind::DomainKeyword: return QStringLiteral("keyword:") + parsed.value;
            case Kind::DomainRegex: return QStringLiteral("regex:") + parsed.value;
            case Kind::RuleSet: return QStringLiteral("ruleset:") + parsed.value;
            case Kind::IpCidr: return QStringLiteral("ip:") + parsed.value;
            case Kind::ProcessName: return QStringLiteral("processName:") + parsed.value;
            case Kind::ProcessPath: return QStringLiteral("processPath:") + parsed.value;
            default: return {};
        }
    }

    inline QString conflictKey(const ParsedLine& parsed) {
        QString value = parsed.value;
        switch (parsed.kind) {
            case Kind::ExactDomain:
            case Kind::DomainSuffix:
            case Kind::DomainKeyword:
            case Kind::ProcessName:
            case Kind::ProcessPath:
                value = value.toCaseFolded();
                break;
            default:
                break;
        }
        return QString::number(static_cast<int>(parsed.kind)) + ':' + value;
    }
}
