#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include "include/database/entities/ProcessRoutePolicy.hpp"
#include "include/database/entities/RouteProfile.h"
#include "include/database/entities/SimpleRouteInputPolicy.hpp"
#include <iostream>

#include "include/database/ProfilesRepo.h"

#include "include/global/Configs.hpp"

namespace Configs {
    bool isOutboundIDValid(int id) {
        switch (id) {
            case -1:
                return true;
            case -2:
                return true;
            default:
                return Configs::dataManager->profilesRepo->GetProfile(id) != nullptr;
        }
    }

    int getOutboundID(const QString& name) {
        if (name == "proxy") return -1;
        if (name == "direct") return -2;
        if (auto profile = Configs::dataManager->profilesRepo->GetProfileByName(name)) return profile->id;

        return INVALID_ID;
    }

    // --- Raw routing profile outbound helpers ---
    // In a raw profile the user references outbounds by numeric id in `outbound` (anywhere,
    // including nested logical rules) and the top-level `final`. These walk the JSON to
    // collect / translate those ids.
    static void collectRawOutboundIdsRec(const QJsonValue& node, QList<int>& out) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    if (!out.contains(id)) out.append(id);
                } else {
                    collectRawOutboundIdsRec(it.value(), out);
                }
            }
        } else if (node.isArray()) {
            for (const auto& e : node.toArray()) collectRawOutboundIdsRec(e, out);
        }
    }

    QList<int> RouteProfile::CollectRawOutboundIds(const QJsonObject& route) {
        QList<int> out;
        collectRawOutboundIdsRec(route, out);
        return out;
    }

    static QJsonValue translateRawOutboundsRec(const QJsonValue& node, const std::map<int, QString>& outboundMap) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            QJsonObject res;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    auto found = outboundMap.find(id);
                    res[it.key()] = found != outboundMap.end() ? QJsonValue(found->second) : QJsonValue("proxy");
                } else {
                    res[it.key()] = translateRawOutboundsRec(it.value(), outboundMap);
                }
            }
            return res;
        }
        if (node.isArray()) {
            QJsonArray res;
            for (const auto& e : node.toArray()) res.append(translateRawOutboundsRec(e, outboundMap));
            return res;
        }
        return node;
    }

    QJsonObject RouteProfile::TranslateRawOutbounds(const QJsonObject& route, const std::map<int, QString>& outboundMap) {
        return translateRawOutboundsRec(route, outboundMap).toObject();
    }

    // Import-side remap: source ids -> local ids by matching the exported name, predefined
    // negatives kept, unresolved -> proxy.
    static QJsonValue remapRawOutboundsByNameRec(const QJsonValue& node, const QJsonObject& names, QString* warnings) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            QJsonObject res;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    if (id < 0) { res[it.key()] = id; continue; }
                    const QString nm = names.value(QString::number(id)).toString();
                    std::shared_ptr<Profile> local;
                    if (!nm.isEmpty()) local = Configs::dataManager->profilesRepo->GetProfileByName(nm);
                    if (local) {
                        res[it.key()] = local->id;
                    } else {
                        res[it.key()] = static_cast<int>(proxyID);
                        if (warnings) warnings->append(QString("outbound \"%1\" not found, using proxy\n").arg(nm.isEmpty() ? QString::number(id) : nm));
                    }
                } else {
                    res[it.key()] = remapRawOutboundsByNameRec(it.value(), names, warnings);
                }
            }
            return res;
        }
        if (node.isArray()) {
            QJsonArray res;
            for (const auto& e : node.toArray()) res.append(remapRawOutboundsByNameRec(e, names, warnings));
            return res;
        }
        return node;
    }

    static QJsonObject remapRawOutboundsByName(const QJsonObject& route, const QJsonObject& names, QString* warnings) {
        return remapRawOutboundsByNameRec(route, names, warnings).toObject();
    }

    QList<std::shared_ptr<RouteRule>> RouteProfile::get_simple_rules() {
        QList<std::shared_ptr<RouteRule>> rules;

        auto rule = RouteRule();
        rule.type = simpleAddressProxy;
        rule.action = "route";
        rule.outboundID = getOutboundID("proxy");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleAddressBypass;
        rule.action = "route";
        rule.outboundID = getOutboundID("direct");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleAddressBlock;
        rule.action = "reject";
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessNameProxy;
        rule.action = "route";
        rule.outboundID = getOutboundID("proxy");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessNameBypass;
        rule.action = "route";
        rule.outboundID = getOutboundID("direct");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessNameBlock;
        rule.action = "reject";
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessPathProxy;
        rule.action = "route";
        rule.outboundID = getOutboundID("proxy");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessPathBypass;
        rule.action = "route";
        rule.outboundID = getOutboundID("direct");
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessPathBlock;
        rule.action = "reject";
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleAddressWarpBypass;
        rule.action = "route";
        rule.outboundID = warpBypassID;
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessNameWarpBypass;
        rule.action = "route";
        rule.outboundID = warpBypassID;
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        rule = RouteRule();
        rule.type = simpleProcessPathWarpBypass;
        rule.action = "route";
        rule.outboundID = warpBypassID;
        rule.name = ruleTypeToString(static_cast<ruleType>(rule.type));
        rules << std::make_shared<RouteRule>(rule);

        return rules;
    }

    void RouteProfile::reset_simple_rule(std::shared_ptr<RouteRule>& rule) {
        auto cleanRules = get_simple_rules();
        for (auto r : cleanRules) {
            if (r->type == rule->type) {
                rule = std::move(r);
                return;
            }
        }
    }

    RouteProfile::RouteProfile(const RouteProfile& other) {
        id = other.id;
        name = QString(other.name);
        for (const auto& item: other.Rules) {
            Rules.push_back(std::make_shared<RouteRule>(*item));
        }
        defaultOutboundID = other.defaultOutboundID;
        isRaw = other.isRaw;
        rawRoute = other.rawRoute;
        preventModifications = other.preventModifications;
        isRemote = other.isRemote;
        remoteURL = other.remoteURL;
        autoUpdate = other.autoUpdate;
        remoteLastUpdate = other.remoteLastUpdate;
    }

    static void appendWarning(QString* warnings, const QString& msg) {
        if (warnings) warnings->append(msg + "\n");
    }

    // Parse one rule JSON object into a RouteRule. Tolerant by design (for sharing): an
    // outbound that can't be resolved on this machine falls back to proxy with a warning
    // rather than failing the whole import. The schema-only keys (name/type) are skipped
    // here and applied by the caller.
    static std::shared_ptr<RouteRule> parse_rule_object(const QJsonObject& obj, QString* warnings) {
        auto rule = std::make_shared<RouteRule>();
        for (const auto& key: obj.keys()) {
            if (key == "name" || key == "type") continue;
            auto val = obj.value(key);
            if (key == "outbound") {
                if (val.isDouble()) {
                    const int id = val.toInt();
                    if (isOutboundIDValid(id)) {
                        rule->outboundID = id;
                    } else {
                        appendWarning(warnings, QString("outbound id %1 not found, using proxy").arg(id));
                        rule->outboundID = proxyID;
                    }
                } else if (val.isString()) {
                    const int id = getOutboundID(val.toString());
                    if (id != INVALID_ID) {
                        rule->outboundID = id;
                    } else {
                        appendWarning(warnings, QString("outbound \"%1\" not found, using proxy").arg(val.toString()));
                        rule->outboundID = proxyID;
                    }
                }
            } else if (val.isArray()) {
                rule->set_field_value(key, QJsonArray2QListString(val.toArray()));
            } else if (val.isString()) {
                rule->set_field_value(key, {val.toString()});
            } else if (val.isBool()) {
                rule->set_field_value(key, {val.toBool() ? "true":"false"});
            }
        }
        return rule;
    }

    QList<std::shared_ptr<RouteRule>> RouteProfile::parseJsonArray(const QJsonArray& arr, QString* parseError, QString* warnings) {
        if (arr.empty()) {
            parseError->append("Input is not a valid json array");
            return {};
        }

        auto rules = QList<std::shared_ptr<RouteRule>>();
        auto ruleID = 1;
        for (const auto& item: arr) {
            if (!item.isObject()) {
                parseError->append(QString("expected array of json objects but have member of type '%1'").arg(item.type()));
                return {};
            }
            const QJsonObject ro = item.toObject();
            auto rule = parse_rule_object(ro, warnings);
            // Preserve an explicit name if the array carries one (our exported rules do); plain
            // sing-box rule arrays have no name, so those still get a stable placeholder.
            const QString nm = ro.value("name").toString();
            rule->name = nm.isEmpty() ? ("imported rule #" + Int2String(ruleID++)) : nm;
            rules << rule;
        }

        return rules;
    }

    QJsonObject RouteProfile::ToShareObject() {
        QJsonObject root;
        root["kind"] = "throne-route-profile";
        root["v"] = 1;
        root["name"] = name;
        if (isRaw) {
            root["raw"] = true;
            root["prevent_modifications"] = preventModifications;
            const auto routeObj = QString2QJsonObject(rawRoute);
            root["route"] = routeObj;
            // carry an id->name map of the referenced server profiles so the importer can
            // re-resolve them by name on another machine.
            QJsonObject names;
            for (const int oid : CollectRawOutboundIds(routeObj)) {
                if (oid < 0) continue; // predefined outbounds (proxy/direct/warp-bypass) are stable
                if (auto p = Configs::dataManager->profilesRepo->GetProfile(oid))
                    names[QString::number(oid)] = p->name;
            }
            root["outbound_names"] = names;
            return root;
        }
        root["default_outbound"] = outboundIDToString(defaultOutboundID);
        QJsonArray rulesArr;
        for (const auto& rule: Rules) {
            if (rule->type != custom && rule->isEmpty()) continue; // drop unused simple-rule stubs
            auto obj = rule->to_share_json();
            if (obj.isEmpty()) continue; // outbound profile missing on this machine
            rulesArr.append(obj);
        }
        root["rules"] = rulesArr;
        return root;
    }

    QString RouteProfile::ToShareLink() {
        const auto json = QJsonDocument(ToShareObject()).toJson(QJsonDocument::Compact);
        const auto b64 = json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        return QStringLiteral("throne://route/") + QString::fromLatin1(b64);
    }

    std::shared_ptr<RouteProfile> RouteProfile::FromShareInput(const QString& input, QString* fatalError, QString* warnings, bool* wasOldArray) {
        if (wasOldArray) *wasOldArray = false;
        QString text = input.trimmed();
        if (text.isEmpty()) {
            fatalError->append("Empty input");
            return nullptr;
        }

        // throne://route/<base64> deep link
        if (text.startsWith("throne://route/", Qt::CaseInsensitive)) {
            const QUrl u(text);
            if (!u.isValid()) {
                fatalError->append("Deep link is invalid");
                return nullptr;
            }
            text = u.path().mid(1);
            if (text.isEmpty()) {
                fatalError->append("Deep link has no data");
                return nullptr;
            }
        }

        // Resolve to JSON: try raw first, then base64 (url-safe, then standard).
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isNull()) {
            doc = QJsonDocument::fromJson(QByteArray::fromBase64(text.toUtf8(), QByteArray::Base64UrlEncoding));
            if (doc.isNull())
                doc = QJsonDocument::fromJson(QByteArray::fromBase64(text.toUtf8()));
        }
        if (doc.isNull()) {
            fatalError->append("Input is not valid JSON, base64, or a Throne route link");
            return nullptr;
        }

        // New schema: a tagged object carrying the whole profile.
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.value("kind").toString() != QStringLiteral("throne-route-profile")) {
                fatalError->append("Unrecognized route object");
                return nullptr;
            }
            if (root.value("raw").toBool()) {
                auto profile = std::make_shared<RouteProfile>();
                profile->id = -1;
                profile->isRaw = true;
                profile->name = root.value("name").toString();
                profile->preventModifications = root.value("prevent_modifications").toBool();
                QJsonObject routeObj = root.value("route").toObject();
                routeObj = remapRawOutboundsByName(routeObj, root.value("outbound_names").toObject(), warnings);
                profile->rawRoute = QJsonObject2QString(routeObj, false);
                return profile;
            }
            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->name = root.value("name").toString();
            profile->defaultOutboundID = stringToOutboundID(root.value("default_outbound").toString());
            int fallbackNum = 1;
            for (const auto& v: root.value("rules").toArray()) {
                if (!v.isObject()) continue;
                const QJsonObject ro = v.toObject();
                auto rule = parse_rule_object(ro, warnings);
                rule->type = tokenToRuleType(ro.value("type").toString());
                rule->name = ro.value("name").toString();
                if (rule->name.isEmpty()) rule->name = "rule_" + Int2String(fallbackNum++);
                profile->Rules << rule;
            }
            return profile;
        }

        // Legacy schema: a bare array of rules (no name / default outbound).
        if (doc.isArray()) {
            QString fe;
            auto rules = parseJsonArray(doc.array(), &fe, warnings);
            if (!fe.isEmpty()) {
                fatalError->append(fe);
                return nullptr;
            }
            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->Rules = rules;
            if (wasOldArray) *wasOldArray = true;
            return profile;
        }

        fatalError->append("Unsupported input");
        return nullptr;
    }

    QList<std::shared_ptr<RouteProfile>> RouteProfile::FromRemoteRoutesLink(const QString& input, bool* wasRemoteRouteLink, QString* error) {
        if (wasRemoteRouteLink) *wasRemoteRouteLink = false;
        const QString text = input.trimmed();
        if (!text.startsWith("throne://remoteroute/", Qt::CaseInsensitive)) return {};
        if (wasRemoteRouteLink) *wasRemoteRouteLink = true;

        const QUrl u(text);
        if (!u.isValid()) {
            if (error) *error = "Deep link is invalid";
            return {};
        }
        QString base64 = u.path().mid(1);
        if (base64.isEmpty()) {
            if (error) *error = "Deep link has no data";
            return {};
        }
        const QString data = DecodeB64IfValid(base64);
        if (data.isEmpty()) {
            if (error) *error = "Base64 is invalid.";
            return {};
        }

        QList<std::shared_ptr<RouteProfile>> res;
        for (const auto& v : data.split('\n', Qt::SkipEmptyParts)) {
            const QString eurl = v.trimmed();
            if (!eurl.startsWith("http://", Qt::CaseInsensitive) && !eurl.startsWith("https://", Qt::CaseInsensitive)) continue;
            const QUrl link(eurl);
            if (!link.isValid()) continue;

            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->isRemote = true;
            profile->remoteURL = link.toString(QUrl::RemoveFragment);
            profile->name = link.fragment();
            if (profile->name.isEmpty()) profile->name = QUrl(eurl).host();
            res << profile;
        }
        if (res.isEmpty() && error) *error = "The link did not contain any valid http(s) routing profile URLs.";
        return res;
    }

    QJsonArray RouteProfile::get_route_rules(bool forView, std::map<int, QString> outboundMap) {
        QJsonArray res;
        bool added_adblock = false;
        auto createAdblockRule = []() -> QJsonObject {
            QJsonObject obj;
            obj["action"] = "reject";
            QJsonArray jarray;
            jarray.append("throne-adblocksingbox");
            obj["rule_set"] = jarray;
            return obj;
        };
        for (const auto &item: Rules) {
            if (item->type != custom && item->isEmpty()) continue;
            auto outboundTag = QString();
            if (outboundMap.contains(item->outboundID)) outboundTag = outboundMap[item->outboundID];
            auto rule_json = item->get_rule_json(forView, outboundTag);
            if (rule_json.empty()) {
                MW_show_log("Aborted generating routing section, an error has occurred");
                return {};
            }
            if (!added_adblock && Configs::dataManager->settingsRepo->adblock_enable && rule_json["action"] == "route") {
                res += createAdblockRule();
                added_adblock = true;
            }                
            res += rule_json;
        }
        if (!added_adblock && Configs::dataManager->settingsRepo->adblock_enable)
            res += createAdblockRule();

        return res;
    }

    std::shared_ptr<RouteProfile> RouteProfile::GetDefaultChain() {
        auto defaultChain = std::make_shared<RouteProfile>();
        defaultChain->name = "Default";
        auto defaultRule = std::make_shared<RouteRule>();
        defaultRule->name = "Route DNS";
        defaultRule->action = "hijack-dns";
        defaultRule->protocol = "dns";
        defaultChain->Rules << defaultRule;
        return defaultChain;
    }

    std::shared_ptr<QList<int>> RouteProfile::get_used_outbounds() {
        auto res = std::make_shared<QList<int>>();
        if (isRaw) {
            // referenced outbounds come from the raw route JSON (so they get built and
            // their server domains added to direct DNS, exactly like structured rules).
            *res = CollectRawOutboundIds(QString2QJsonObject(rawRoute));
            return res;
        }
        for (const auto& item: Rules) {
            res->push_back(item->outboundID);
        }
        return res;
    }

    std::shared_ptr<QStringList> RouteProfile::get_used_rule_sets() {
        auto res = std::make_shared<QStringList>();
        for (const auto& item: Rules) {
            for (const auto& ruleItem: item->rule_set) {
                res->push_back(ruleItem);
            }
        }
        return res;
    }

    QStringList RouteProfile::get_direct_sites() {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == directID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geosite-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->domain) {
                    res << QString("domain:" + domain);
                }
                for (const auto& suffix: item->domain_suffix) {
                    res << QString("suffix:" + suffix);
                }
                for (const auto& keyword: item->domain_keyword) {
                    res << QString("keyword:" + keyword);
                }
                for (const auto& regex: item->domain_regex) {
                    res << QString("regex:" + regex);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_proxy_sites() {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == proxyID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geosite-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->domain) {
                    res << QString("domain:" + domain);
                }
                for (const auto& suffix: item->domain_suffix) {
                    res << QString("suffix:" + suffix);
                }
                for (const auto& keyword: item->domain_keyword) {
                    res << QString("keyword:" + keyword);
                }
                for (const auto& regex: item->domain_regex) {
                    res << QString("regex:" + regex);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_direct_ips()
    {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == directID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geoip-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->ip_cidr) {
                    res << QString("ip:" + domain);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_hijacked_ips()
    {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->action == "route" && item->outboundID == directID) continue;
            if (item->action != "route" && item->action != "reject") continue;
            // ip_is_private covers every range the Tun bypass carves out, so it
            // hijacks all of them at once.
            if (item->ip_is_private) res << tunBypassablePrivateRanges();
            for (const auto& cidr: item->ip_cidr) res << cidr;
        }
        return res;
    }

    bool RouteProfile::UsesProcessRules() const {
        if (isRaw) return ProcessRoutePolicy::jsonUsesProcessSelectors(QString2QJsonObject(rawRoute));
        for (const auto& rule : Rules) {
            if (!rule->process_name.isEmpty() || !rule->process_path.isEmpty() ||
                !rule->process_path_regex.isEmpty()) {
                return true;
            }
        }
        return false;
    }

    bool RouteProfile::IsEmpty() {
        if (isRaw) return rawRoute.trimmed().isEmpty();
        for (const auto& item: Rules) {
            if (!item->isEmpty()) return false;
        }
        return true;
    }

    void RouteProfile::ResetRules() {
        Rules.clear();
    }

    void RouteProfile::ResetSimpleRule(ruleType type) {
        for (std::shared_ptr<RouteRule> &item: Rules) {
            if (item->type == type) {
                reset_simple_rule(item);
                return;
            }
        }
        auto cleanRules = get_simple_rules();
        for (std::shared_ptr<RouteRule> &item: cleanRules) {
            if (item->type == type) {
                Rules << item;
                return;
            }
        }
    }

    QString RouteProfile::GetSimpleRules(simpleAction action)
    {
        QList<int> types;
        if (action == proxy) {
            types << simpleAddressProxy;
            types << simpleProcessNameProxy;
            types << simpleProcessPathProxy;
        } else if (action == bypass) {
            types << simpleAddressBypass;
            types << simpleProcessNameBypass;
            types << simpleProcessPathBypass;
        } else if (action == warpBypass) {
            types << simpleAddressWarpBypass;
            types << simpleProcessNameWarpBypass;
            types << simpleProcessPathWarpBypass;
        } else {
            types << simpleAddressBlock;
            types << simpleProcessNameBlock;
            types << simpleProcessPathBlock;
        }
        QString res;
        for (const auto& item: Rules)
        {
            if (types.contains(item->type))
            {
                for (const auto& domain : item->domain) res += QString("domain:" + domain + "\n");
                for (const auto& domain_suffix : item->domain_suffix) res += QString("suffix:" + domain_suffix + "\n");
                for (const auto& domain_keyword : item->domain_keyword) res += QString("keyword:" + domain_keyword + "\n");
                for (const auto& domain_regex : item->domain_regex) res += QString("regex:" + domain_regex + "\n");
                for (const auto& rule_set : item->rule_set) res += QString("ruleset:" + rule_set + "\n");
                for (const auto& ip_cidr : item->ip_cidr) res += QString("ip:" + ip_cidr + "\n");
                for (const auto& process_name : item->process_name) res += QString("processName:" + process_name + "\n");
                for (const auto& process_path : item->process_path) res += QString("processPath:" + process_path + "\n");
            }
        }
        return res;
    }


    QString RouteProfile::UpdateSimpleRules(const QString& content, simpleAction action)
    {
        QString res;
        const auto items = content.split('\n');
        QList<SimpleRouteInput::ParsedLine> parsedItems;
        int lineNumber = 0;
        for (const QString& raw : items) {
            ++lineNumber;
            const auto parsed = SimpleRouteInput::parseLine(raw);
            if (parsed.isIgnored()) continue;
            if (!parsed.isValid()) {
                res += QStringLiteral("line %1: %2 — %3\n")
                    .arg(lineNumber)
                    .arg(raw.trimmed(), parsed.error);
                continue;
            }
            parsedItems.append(parsed);
        }

        // Validation is transactional: a bad line never wipes the last valid rules when
        // the editor changes tab or the user presses Save.
        if (!res.isEmpty()) return res;

        QList<ruleType> types;
        if (action == proxy) {
            types << simpleAddressProxy;
            types << simpleProcessNameProxy;
            types << simpleProcessPathProxy;
        } else if (action == bypass) {
            types << simpleAddressBypass;
            types << simpleProcessNameBypass;
            types << simpleProcessPathBypass;
        } else if (action == warpBypass) {
            types << simpleAddressWarpBypass;
            types << simpleProcessNameWarpBypass;
            types << simpleProcessPathWarpBypass;
        } else {
            types << simpleAddressBlock;
            types << simpleProcessNameBlock;
            types << simpleProcessPathBlock;
        }
        for (auto t : types) {
            ResetSimpleRule(t);
        }

        auto addressType = [action]() {
            if (action == proxy) return simpleAddressProxy;
            if (action == bypass) return simpleAddressBypass;
            if (action == warpBypass) return simpleAddressWarpBypass;
            return simpleAddressBlock;
        };
        auto processNameType = [action]() {
            if (action == proxy) return simpleProcessNameProxy;
            if (action == bypass) return simpleProcessNameBypass;
            if (action == warpBypass) return simpleProcessNameWarpBypass;
            return simpleProcessNameBlock;
        };
        auto processPathType = [action]() {
            if (action == proxy) return simpleProcessPathProxy;
            if (action == bypass) return simpleProcessPathBypass;
            if (action == warpBypass) return simpleProcessPathWarpBypass;
            return simpleProcessPathBlock;
        };
        auto appendUnique = [](QList<QString>& values, const QString& value) {
            if (!values.contains(value)) values.append(value);
        };

        for (const auto& parsed : parsedItems) {
            ruleType type = addressType();
            if (parsed.kind == SimpleRouteInput::Kind::ProcessName) type = processNameType();
            if (parsed.kind == SimpleRouteInput::Kind::ProcessPath) type = processPathType();
            auto rule = get_simple_rule_by_type(type);
            if (!rule) {
                res += "internal error, failed to get rule for: " + ruleTypeToString(type) + "\n";
                continue;
            }

            switch (parsed.kind) {
                case SimpleRouteInput::Kind::ExactDomain: appendUnique(rule->domain, parsed.value); break;
                case SimpleRouteInput::Kind::DomainSuffix: appendUnique(rule->domain_suffix, parsed.value); break;
                case SimpleRouteInput::Kind::DomainKeyword: appendUnique(rule->domain_keyword, parsed.value); break;
                case SimpleRouteInput::Kind::DomainRegex: appendUnique(rule->domain_regex, parsed.value); break;
                case SimpleRouteInput::Kind::RuleSet: appendUnique(rule->rule_set, parsed.value); break;
                case SimpleRouteInput::Kind::IpCidr: appendUnique(rule->ip_cidr, parsed.value); break;
                case SimpleRouteInput::Kind::ProcessName: appendUnique(rule->process_name, parsed.value); break;
                case SimpleRouteInput::Kind::ProcessPath: appendUnique(rule->process_path, parsed.value); break;
                default: break;
            }
        }
        FilterEmptyRules();
        return res;
    }

    void RouteProfile::FilterEmptyRules() {
        QList<std::shared_ptr<RouteRule>> newRules;
        for (const auto& rule : Rules) {
            if (!rule->isEmpty()) newRules.append(rule);
        }
        Rules = newRules;
    }

    std::shared_ptr<RouteRule> RouteProfile::get_simple_rule_by_type(ruleType type) {
        for (auto r : Rules) {
            if (r->type == type) return r;
        }
        return nullptr;
    }

}
