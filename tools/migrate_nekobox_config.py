#!/usr/bin/env python3
"""Import legacy NekoBox state into a fresh Throne database.

The importer is offline, never controls either VPN core, never prints user
values, accepts only a fresh target, creates a backup, and commits atomically.
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping


MIGRATION_MARKER = "migration_nekobox_v1"
REQUIRED_TABLES = {
    "entity_ids", "groups", "groups_order", "profiles",
    "route_profiles", "route_rules", "settings",
}
NUMERIC_JSON = re.compile(r"^(0|[1-9][0-9]*)\.json$")


class MigrationError(RuntimeError):
    """An expected migration failure represented by a non-secret issue code."""


@dataclass(frozen=True)
class ImportPlan:
    groups: tuple[dict[str, Any], ...]
    profiles: tuple[dict[str, Any], ...]
    routes: tuple[dict[str, Any], ...]
    group_id_map: Mapping[int, int]
    profile_id_map: Mapping[int, int]
    active_group_id: int
    selected_profile_id: int
    active_route_id: int


def _load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise MigrationError(f"invalid_json:{path.name}") from exc
    if not isinstance(value, dict):
        raise MigrationError(f"expected_object:{path.name}")
    return value


def _text(value: Any) -> str:
    return value if isinstance(value, str) else ""


def _integer(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return default


def _positive_port(value: Any) -> int:
    port = _integer(value)
    if not 1 <= port <= 65535:
        raise MigrationError("invalid_profile_port")
    return port


def _string_list(value: Any) -> list[str]:
    if isinstance(value, list):
        return [item for item in value if isinstance(item, str) and item]
    if not isinstance(value, str) or not value:
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def _lines(value: Any) -> list[str]:
    if not isinstance(value, str):
        return []
    result: list[str] = []
    seen: set[str] = set()
    for item in value.splitlines():
        item = item.strip()
        if not item or item.startswith("#") or item in seen:
            continue
        seen.add(item)
        result.append(item)
    return result


def _xhttp_extra(stream: Mapping[str, Any]) -> dict[str, Any]:
    """Return supported XHTTP extras without silently dropping malformed JSON."""
    raw_extra = stream.get("extra")
    if raw_extra is None or raw_extra == "":
        extra: dict[str, Any] = {}
    elif isinstance(raw_extra, dict):
        extra = dict(raw_extra)
    elif isinstance(raw_extra, str):
        try:
            parsed = json.loads(raw_extra)
        except json.JSONDecodeError as exc:
            raise MigrationError("invalid_xhttp_extra") from exc
        if not isinstance(parsed, dict):
            raise MigrationError("invalid_xhttp_extra")
        extra = parsed
    else:
        raise MigrationError("invalid_xhttp_extra")

    aliases = {
        "xPaddingBytes": ("xPaddingBytes", "x_padding_bytes"),
        "xPaddingObfsMode": ("xPaddingObfsMode", "x_padding_obfs_mode"),
        "xPaddingKey": ("xPaddingKey", "x_padding_key"),
        "xPaddingHeader": ("xPaddingHeader", "x_padding_header"),
        "xPaddingPlacement": ("xPaddingPlacement", "x_padding_placement"),
        "xPaddingMethod": ("xPaddingMethod", "x_padding_method"),
        "uplinkHTTPMethod": ("uplinkHTTPMethod", "uplink_http_method"),
        "sessionIDPlacement": ("sessionIDPlacement", "session_id_placement"),
        "sessionIDKey": ("sessionIDKey", "session_id_key"),
        "sessionIDTable": ("sessionIDTable", "session_id_table"),
        "sessionIDLength": ("sessionIDLength", "session_id_length"),
        "seqPlacement": ("seqPlacement", "seq_placement"),
        "seqKey": ("seqKey", "seq_key"),
        "uplinkDataPlacement": ("uplinkDataPlacement", "uplink_data_placement"),
        "uplinkDataKey": ("uplinkDataKey", "uplink_data_key"),
        "uplinkChunkSize": ("uplinkChunkSize", "uplink_chunk_size"),
        "noGRPCHeader": ("noGRPCHeader", "no_grpc_header"),
        "noSSEHeader": ("noSSEHeader", "no_sse_header"),
        "scMaxEachPostBytes": ("scMaxEachPostBytes", "sc_max_each_post_bytes"),
        "scMinPostsIntervalMs": ("scMinPostsIntervalMs", "sc_min_posts_interval_ms"),
        "scMaxBufferedPosts": ("scMaxBufferedPosts", "sc_max_buffered_posts"),
        "scStreamUpServerSecs": ("scStreamUpServerSecs", "sc_stream_up_server_secs"),
        "serverMaxHeaderBytes": ("serverMaxHeaderBytes", "server_max_header_bytes"),
    }
    for destination, candidates in aliases.items():
        for candidate in candidates:
            if candidate in stream and stream[candidate] not in (None, "", False, 0):
                extra[destination] = stream[candidate]
                break
    return extra


def _legacy_vless_outbound(bean: Mapping[str, Any]) -> dict[str, Any]:
    address = _text(bean.get("addr")).strip()
    uuid = _text(bean.get("pass")).strip()
    if not address or not uuid:
        raise MigrationError("incomplete_vless_profile")
    raw_stream = bean.get("stream")
    if not isinstance(raw_stream, dict):
        raise MigrationError("missing_vless_stream")
    stream = raw_stream
    legacy_network = _text(stream.get("net")).strip().lower() or "tcp"
    network = "raw" if legacy_network == "tcp" else legacy_network
    if network not in {"raw", "ws", "httpupgrade", "grpc", "xhttp"}:
        raise MigrationError("unsupported_vless_transport")

    public_key = _text(stream.get("pbk")).strip()
    legacy_security = _text(stream.get("sec")).strip().lower()
    security = "reality" if public_key else legacy_security
    if security not in {"", "none", "tls", "reality"}:
        raise MigrationError("unsupported_vless_security")
    if security in {"", "none"}:
        security = "none"

    stream_settings: dict[str, Any] = {"network": network, "security": security}
    server_name = _text(stream.get("sni"))
    fingerprint = _text(stream.get("utls"))
    alpn = _string_list(stream.get("alpn"))
    if security == "reality":
        reality: dict[str, Any] = {}
        for key, value in (
            ("serverName", server_name), ("fingerprint", fingerprint),
            ("password", public_key), ("shortId", _text(stream.get("sid"))),
            ("spiderX", _text(stream.get("spx"))),
        ):
            if value:
                reality[key] = value
        stream_settings["realitySettings"] = reality
    elif security == "tls":
        tls: dict[str, Any] = {}
        if server_name:
            tls["serverName"] = server_name
        if fingerprint:
            tls["fingerprint"] = fingerprint
        if alpn:
            tls["alpn"] = alpn
        stream_settings["tlsSettings"] = tls

    path = _text(stream.get("path"))
    if network == "grpc":
        stream_settings["grpcSettings"] = {"serviceName": path} if path else {}
    elif network == "ws":
        ws: dict[str, Any] = {}
        if path:
            ws["path"] = path
        host = _text(stream.get("host"))
        if host:
            ws["host"] = host
        early_data = _integer(stream.get("ed_len"))
        if early_data > 0:
            ws["ed"] = early_data
        stream_settings["wsSettings"] = ws
    elif network == "httpupgrade":
        http_upgrade: dict[str, Any] = {}
        if path:
            http_upgrade["path"] = path
        host = _text(stream.get("host"))
        if host:
            http_upgrade["host"] = host
        stream_settings["httpupgradeSettings"] = http_upgrade
    elif network == "xhttp":
        xhttp: dict[str, Any] = {}
        for key, value in (
            ("host", _text(stream.get("host"))),
            ("path", path),
            ("mode", _text(stream.get("mode"))),
        ):
            if value:
                xhttp[key] = value
        extra = _xhttp_extra(stream)
        if extra:
            xhttp["extra"] = extra
        stream_settings["xhttpSettings"] = xhttp

    settings: dict[str, Any] = {
        "address": address,
        "port": _positive_port(bean.get("port")),
        "id": uuid,
        "encryption": "none",
    }
    flow = _text(bean.get("flow")).strip()
    if flow and flow != "none":
        settings["flow"] = flow
    return {
        "tag": _text(bean.get("name")),
        "protocol": "vless",
        "settings": settings,
        "streamSettings": stream_settings,
    }


def _legacy_socks_outbound(bean: Mapping[str, Any]) -> dict[str, Any]:
    address = _text(bean.get("addr")).strip()
    if not address:
        raise MigrationError("incomplete_socks_profile")
    outbound: dict[str, Any] = {
        "type": "socks", "tag": _text(bean.get("name")), "server": address,
        "server_port": _positive_port(bean.get("port")),
    }
    username = _text(bean.get("username") or bean.get("user"))
    password = _text(bean.get("password"))
    if username:
        outbound["username"] = username
    if password:
        outbound["password"] = password
    if _integer(bean.get("v"), 5) == 4:
        outbound["version"] = "4"
    return outbound


def _convert_profile(source: Mapping[str, Any], new_id: int, new_gid: int) -> dict[str, Any]:
    profile_type = _text(source.get("type")).strip().lower()
    bean = source.get("bean")
    if not isinstance(bean, dict):
        raise MigrationError("missing_profile_bean")
    if profile_type == "vless":
        target_type, outbound = "xrayvless", _legacy_vless_outbound(bean)
    elif profile_type == "socks":
        target_type, outbound = "socks", _legacy_socks_outbound(bean)
    else:
        raise MigrationError("unsupported_profile_type")
    traffic = source.get("traffic") if isinstance(source.get("traffic"), dict) else {}
    return {
        "id": new_id, "gid": new_gid, "type": target_type,
        "name": _text(bean.get("name")),
        "outbound_json": json.dumps(outbound, ensure_ascii=False, separators=(",", ":")),
        "traffic_dl": max(0, _integer(traffic.get("dl"))),
        "traffic_up": max(0, _integer(traffic.get("ul"))),
    }


def _route_rules(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, str) or not value.strip():
        return []
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError as exc:
        raise MigrationError("invalid_route_custom_json") from exc
    if isinstance(parsed, list):
        rules = parsed
    elif isinstance(parsed, dict) and isinstance(parsed.get("rules"), list):
        rules = parsed["rules"]
    else:
        raise MigrationError("invalid_route_custom_shape")
    if not all(isinstance(rule, dict) for rule in rules):
        raise MigrationError("invalid_route_rule_shape")
    return [dict(rule) for rule in rules]


def _modernise_route_rule(value: Any) -> Any:
    if isinstance(value, list):
        return [_modernise_route_rule(item) for item in value]
    if not isinstance(value, dict):
        return value
    result = {key: _modernise_route_rule(item) for key, item in value.items()}
    outbound = result.get("outbound")
    if outbound == "block":
        result.pop("outbound", None)
        result["action"] = "reject"
    elif isinstance(outbound, (str, int)) and "action" not in result:
        result["action"] = "route"
    return result


def _convert_route(source: Mapping[str, Any], new_id: int, name: str) -> dict[str, Any]:
    rules: list[dict[str, Any]] = []
    basic_rules = (
        ("direct_domain", "domain_suffix", "direct", False),
        ("direct_ip", "ip_cidr", "direct", False),
        ("proxy_domain", "domain_suffix", "proxy", False),
        ("proxy_ip", "ip_cidr", "proxy", False),
        ("block_domain", "domain_suffix", "", True),
        ("block_ip", "ip_cidr", "", True),
    )
    for source_key, selector, outbound, reject in basic_rules:
        values = _lines(source.get(source_key))
        if not values:
            continue
        rule: dict[str, Any] = {selector: values, "action": "reject" if reject else "route"}
        if outbound:
            rule["outbound"] = outbound
        rules.append(rule)
    rules.extend(_modernise_route_rule(rule) for rule in _route_rules(source.get("custom")))
    final = _text(source.get("def_outbound")).strip().lower() or "proxy"
    if final == "block":
        rules.append({"action": "reject"})
        final = "direct"
    elif final not in {"proxy", "direct"}:
        final = "proxy"
    raw_route = {"rules": rules, "final": final}
    return {
        "id": new_id, "name": name,
        "raw_route": json.dumps(raw_route, ensure_ascii=False, separators=(",", ":")),
    }


def _numeric_json_files(directory: Path) -> Iterable[tuple[int, Path]]:
    for path in directory.iterdir():
        match = NUMERIC_JSON.match(path.name)
        if match and path.is_file():
            yield int(match.group(1)), path


def build_plan(source_root: Path) -> ImportPlan:
    groups_dir, profiles_dir, routes_dir = (
        source_root / "groups", source_root / "profiles", source_root / "routes_box",
    )
    if not all(path.is_dir() for path in (groups_dir, profiles_dir, routes_dir)):
        raise MigrationError("legacy_layout_missing")
    group_sources = {gid: _load_object(path) for gid, path in _numeric_json_files(groups_dir)}
    profile_sources: dict[int, dict[str, Any]] = {}
    source_gids: set[int] = set(group_sources)
    for fallback_id, path in _numeric_json_files(profiles_dir):
        source = _load_object(path)
        old_id = _integer(source.get("id"), fallback_id)
        if old_id in profile_sources:
            raise MigrationError("duplicate_profile_id")
        old_gid = _integer(source.get("gid"))
        source_gids.add(old_gid)
        profile_sources[old_id] = source
    if not profile_sources:
        raise MigrationError("no_profiles")

    settings_path = groups_dir / "nekobox.json"
    legacy_settings = _load_object(settings_path) if settings_path.is_file() else {}
    legacy_group_order: list[int] = []
    group_order_path = groups_dir / "pm.json"
    if group_order_path.is_file():
        raw_order = _load_object(group_order_path).get("groups")
        if isinstance(raw_order, list):
            legacy_group_order = [_integer(item, -1) for item in raw_order]
    ordered_gids: list[int] = []
    for gid in (*legacy_group_order, *sorted(source_gids)):
        if gid in source_gids and gid not in ordered_gids:
            ordered_gids.append(gid)
    group_id_map = {old_gid: index + 1 for index, old_gid in enumerate(ordered_gids)}
    profile_id_map = {old_id: index + 1 for index, old_id in enumerate(sorted(profile_sources))}

    profiles: list[dict[str, Any]] = []
    profile_ids_by_gid: dict[int, list[int]] = {gid: [] for gid in ordered_gids}
    for old_id in sorted(profile_sources):
        source = profile_sources[old_id]
        old_gid = _integer(source.get("gid"))
        converted = _convert_profile(source, profile_id_map[old_id], group_id_map[old_gid])
        profiles.append(converted)
        profile_ids_by_gid[old_gid].append(converted["id"])
    groups: list[dict[str, Any]] = []
    for old_gid in ordered_gids:
        source = group_sources.get(old_gid, {})
        groups.append({
            "id": group_id_map[old_gid],
            "archive": 1 if source.get("archive") is True else 0,
            "skip_auto_update": 1 if source.get("skip_auto_update") is True else 0,
            "name": _text(source.get("name")).strip() or f"Imported profiles {old_gid}",
            "url": _text(source.get("url")), "info": _text(source.get("info")),
            "sub_last_update": max(0, _integer(source.get("lastup"))),
            "front_proxy_id": -1, "landing_proxy_id": -1,
            "profiles_json": json.dumps(profile_ids_by_gid[old_gid], separators=(",", ":")),
        })

    routes: list[dict[str, Any]] = []
    route_name_to_id: dict[str, int] = {}
    route_paths = sorted(
        (path for path in routes_dir.iterdir() if path.is_file() and ".bak" not in path.name),
        key=lambda path: path.name.casefold(),
    )
    for path in route_paths:
        route_id = len(routes) + 1
        routes.append(_convert_route(_load_object(path), route_id, path.name))
        route_name_to_id[path.name] = route_id
    if not routes:
        routes.append(_convert_route({}, 1, "Default"))
        route_name_to_id["Default"] = 1
    old_current_group = _integer(legacy_settings.get("current_group"), ordered_gids[0])
    old_selected = _integer(legacy_settings.get("remember_id"), -1)
    return ImportPlan(
        groups=tuple(groups), profiles=tuple(profiles), routes=tuple(routes),
        group_id_map=group_id_map, profile_id_map=profile_id_map,
        active_group_id=group_id_map.get(old_current_group, 1),
        selected_profile_id=profile_id_map.get(old_selected, -1919),
        active_route_id=route_name_to_id.get(_text(legacy_settings.get("active_routing")), 1),
    )


def _table_names(connection: sqlite3.Connection) -> set[str]:
    return {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}


def _validate_fresh_target(connection: sqlite3.Connection) -> None:
    if REQUIRED_TABLES - _table_names(connection):
        raise MigrationError("target_schema_incomplete")
    if connection.execute("SELECT 1 FROM settings WHERE key=?", (MIGRATION_MARKER,)).fetchone():
        raise MigrationError("target_already_migrated")
    if connection.execute("SELECT COUNT(*) FROM profiles").fetchone()[0] != 0:
        raise MigrationError("target_has_profiles")
    if connection.execute("SELECT COUNT(*) FROM groups").fetchone()[0] > 1:
        raise MigrationError("target_not_fresh")
    if connection.execute("SELECT COUNT(*) FROM route_profiles").fetchone()[0] > 1:
        raise MigrationError("target_not_fresh")


def _upsert_setting(connection: sqlite3.Connection, key: str, value: str) -> None:
    connection.execute(
        "INSERT INTO settings(key,value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (key, value),
    )


def apply_plan(target_db: Path, plan: ImportPlan, backup_path: Path | None = None) -> Path:
    if not target_db.is_file():
        raise MigrationError("target_database_missing")
    target = target_db.resolve()
    backup = (backup_path or target_db.with_suffix(f".pre-nekobox-{int(time.time())}.bak")).resolve()
    if backup == target:
        raise MigrationError("backup_equals_target")
    if backup.exists():
        raise MigrationError("backup_already_exists")
    backup.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(target, timeout=10)
    connection.execute("PRAGMA busy_timeout=10000")
    connection.execute("PRAGMA foreign_keys=ON")
    try:
        _validate_fresh_target(connection)
        destination = sqlite3.connect(backup)
        try:
            connection.backup(destination)
        finally:
            destination.close()
        try:
            connection.execute("BEGIN IMMEDIATE")
            _validate_fresh_target(connection)
            for table in ("route_rules", "route_profiles", "profiles", "groups_order", "groups"):
                connection.execute(f"DELETE FROM {table}")
            now = int(time.time())
            for order, group in enumerate(plan.groups):
                connection.execute(
                    """INSERT INTO groups(
                       id,archive,skip_auto_update,name,url,info,sub_last_update,front_proxy_id,
                       landing_proxy_id,column_width_json,profiles_json,scroll_last_profile,
                       auto_clear_unavailable,test_sort_by,traffic_sort_by,test_items_to_show,
                       type_sort_by,created_at,updated_at)
                       VALUES(?,?,?,?,?,?,?,?,?,'[]',?,-1,0,0,0,0,0,?,?)""",
                    (group["id"], group["archive"], group["skip_auto_update"], group["name"],
                     group["url"], group["info"], group["sub_last_update"],
                     group["front_proxy_id"], group["landing_proxy_id"],
                     group["profiles_json"], now, now),
                )
                connection.execute(
                    "INSERT INTO groups_order(group_id,display_order) VALUES(?,?)",
                    (group["id"], order),
                )
            for profile in plan.profiles:
                connection.execute(
                    """INSERT INTO profiles(
                       id,type,name,gid,latency,latency_at,dl_speed,ul_speed,test_country,ip_out,
                       outbound_json,traffic_dl,traffic_up,created_at,updated_at)
                       VALUES(?,?,?,?,0,0,'','','','',?,?,?,?,?)""",
                    (profile["id"], profile["type"], profile["name"], profile["gid"],
                     profile["outbound_json"], profile["traffic_dl"], profile["traffic_up"], now, now),
                )
            for route in plan.routes:
                connection.execute(
                    """INSERT INTO route_profiles(
                       id,name,default_outbound_id,is_raw,raw_route,prevent_modifications,is_remote,
                       remote_url,auto_update,remote_last_update,created_at,updated_at)
                       VALUES(?,?,-1,1,?,0,0,'',0,0,?,?)""",
                    (route["id"], route["name"], route["raw_route"], now, now),
                )
            connection.execute(
                "UPDATE entity_ids SET profile_last_id=?,group_last_id=?,route_profile_last_id=?",
                (len(plan.profiles), len(plan.groups), len(plan.routes)),
            )
            for key, value in {
                "current_group": str(plan.active_group_id),
                "remember_id": str(plan.selected_profile_id),
                "current_route_id": str(plan.active_route_id),
                "language": "4", "theme": "AuroraDark",
                # Keep the migrated client side-by-side with the user's live
                # NekoBox instance during acceptance testing.  The existing
                # always-on VPN owns 2080 and must not be displaced.
                "inbound_socks_port": "3080",
                "core_dns_in_port": "15533",
                "dns_server_listen_port": "15353",
                "redirect_listen_port": "18443",
                "remember_enable": "false", "system_proxy_enabled": "false",
                "tun_mode_enabled": "false", "windows_set_admin": "false",
                MIGRATION_MARKER: str(now),
            }.items():
                _upsert_setting(connection, key, value)
            connection.commit()
        except Exception:
            connection.rollback()
            raise
    finally:
        connection.close()
    return backup


def _summary(plan: ImportPlan, committed: bool, backup_created: bool) -> str:
    counts: dict[str, int] = {}
    for profile in plan.profiles:
        counts[profile["type"]] = counts.get(profile["type"], 0) + 1
    return json.dumps({
        "ok": True, "committed": committed, "backup_created": backup_created,
        "groups": len(plan.groups), "profiles": len(plan.profiles),
        "profile_types": counts, "routes": len(plan.routes),
    }, sort_keys=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Safely migrate legacy NekoBox state to Throne")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--target-db", type=Path, required=True)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--commit", action="store_true", help="apply; default is a dry-run")
    args = parser.parse_args(argv)
    try:
        plan = build_plan(args.source.resolve())
        if not args.commit:
            print(_summary(plan, committed=False, backup_created=False))
            return 0
        backup = apply_plan(args.target_db.resolve(), plan, args.backup)
        print(_summary(plan, committed=True, backup_created=backup.is_file()))
        return 0
    except MigrationError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True), file=sys.stderr)
        return 2
    except (OSError, sqlite3.Error) as exc:
        print(json.dumps({"ok": False, "error": type(exc).__name__}, sort_keys=True), file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
