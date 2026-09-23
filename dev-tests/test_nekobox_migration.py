from __future__ import annotations

import importlib.util
import json
import sqlite3
import sys
import tempfile
import unittest
from contextlib import closing
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "migrate_nekobox_config.py"
SPEC = importlib.util.spec_from_file_location("migrate_nekobox_config", MODULE_PATH)
assert SPEC and SPEC.loader
migration = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = migration
SPEC.loader.exec_module(migration)


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def create_fresh_database(path: Path) -> None:
    with closing(sqlite3.connect(path)) as db:
        db.executescript(
            """
            CREATE TABLE entity_ids(profile_last_id INTEGER, group_last_id INTEGER,
                route_profile_last_id INTEGER, otp_profile_last_id INTEGER);
            INSERT INTO entity_ids VALUES(0,0,0,0);
            CREATE TABLE groups(
                id INTEGER PRIMARY KEY, archive INTEGER, skip_auto_update INTEGER, name TEXT,
                url TEXT, info TEXT, sub_last_update INTEGER, front_proxy_id INTEGER,
                landing_proxy_id INTEGER, column_width_json TEXT, profiles_json TEXT,
                scroll_last_profile INTEGER, auto_clear_unavailable INTEGER, test_sort_by INTEGER,
                traffic_sort_by INTEGER, test_items_to_show INTEGER, type_sort_by INTEGER,
                created_at INTEGER, updated_at INTEGER);
            CREATE TABLE groups_order(group_id INTEGER PRIMARY KEY, display_order INTEGER);
            CREATE TABLE profiles(
                id INTEGER PRIMARY KEY, type TEXT, name TEXT, gid INTEGER, latency INTEGER,
                latency_at INTEGER, dl_speed TEXT, ul_speed TEXT, test_country TEXT, ip_out TEXT,
                outbound_json TEXT, traffic_dl INTEGER, traffic_up INTEGER, created_at INTEGER,
                updated_at INTEGER, FOREIGN KEY(gid) REFERENCES groups(id) ON DELETE CASCADE);
            CREATE TABLE route_profiles(
                id INTEGER PRIMARY KEY, name TEXT, default_outbound_id INTEGER, is_raw INTEGER,
                raw_route TEXT, prevent_modifications INTEGER, is_remote INTEGER, remote_url TEXT,
                auto_update INTEGER, remote_last_update INTEGER, created_at INTEGER, updated_at INTEGER);
            CREATE TABLE route_rules(route_profile_id INTEGER, rule_order INTEGER);
            CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
            INSERT INTO groups VALUES(1,0,0,'Default','','',0,-1,-1,'[]','[]',-1,0,0,0,0,0,0,0);
            INSERT INTO groups_order VALUES(1,0);
            INSERT INTO route_profiles VALUES(1,'Default',-1,0,'',0,0,'',0,0,0,0);
            INSERT INTO settings VALUES('current_group','1');
            """
        )


def create_source(root: Path, *, malformed_extra: bool = False) -> None:
    write_json(root / "groups" / "0.json", {"id": 0, "name": "Local"})
    write_json(root / "groups" / "2.json", {
        "id": 2, "name": "Sub", "url": "https://example.test/sub",
    })
    write_json(root / "groups" / "pm.json", {"groups": [2, 0]})
    write_json(root / "groups" / "nekobox.json", {
        "current_group": 2, "remember_id": 22, "active_routing": "Default",
    })
    write_json(root / "profiles" / "11.json", {
        "id": 11, "gid": 0, "type": "socks",
        "bean": {"name": "Local proxy", "addr": "127.0.0.1", "port": 1080, "v": 5},
    })
    write_json(root / "profiles" / "22.json", {
        "id": 22, "gid": 2, "type": "vless",
        "bean": {
            "name": "XHTTP test", "addr": "edge.example.test", "port": 443,
            "pass": "00000000-0000-4000-8000-000000000000",
            "stream": {
                "net": "xhttp", "sec": "tls", "sni": "cdn.example.test",
                "utls": "chrome", "path": "/upload", "mode": "packet-up",
                "extra": "not-json" if malformed_extra else '{"uplinkHTTPMethod":"PUT"}',
            },
        },
    })
    write_json(root / "routes_box" / "Default", {
        "direct_domain": "home.example\n", "def_outbound": "proxy",
        "custom": json.dumps({
            "rules": [{"domain_suffix": ["blocked.example"], "outbound": "block"}],
        }),
    })


class NekoBoxMigrationTests(unittest.TestCase):
    def test_plan_preserves_xhttp_method_and_modernises_block(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source"
            create_source(source)
            plan = migration.build_plan(source)
            self.assertEqual((len(plan.groups), len(plan.profiles)), (2, 2))
            self.assertEqual(plan.selected_profile_id, 2)
            xhttp = json.loads(plan.profiles[1]["outbound_json"])
            self.assertEqual(xhttp["streamSettings"]["network"], "xhttp")
            self.assertEqual(
                xhttp["streamSettings"]["xhttpSettings"]["extra"]["uplinkHTTPMethod"], "PUT",
            )
            route = json.loads(plan.routes[0]["raw_route"])
            self.assertEqual(route["rules"][0]["outbound"], "direct")
            self.assertEqual(route["rules"][1]["action"], "reject")
            self.assertNotIn("outbound", route["rules"][1])

    def test_malformed_xhttp_extra_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source"
            create_source(source, malformed_extra=True)
            with self.assertRaisesRegex(migration.MigrationError, "invalid_xhttp_extra"):
                migration.build_plan(source)

    def test_apply_is_transactional_and_creates_backup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, database, backup = root / "source", root / "throne.db", root / "before.bak"
            create_source(source)
            create_fresh_database(database)
            result = migration.apply_plan(database, migration.build_plan(source), backup)
            self.assertEqual(result, backup.resolve())
            with closing(sqlite3.connect(database)) as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM profiles").fetchone()[0], 2)
                self.assertEqual(db.execute("SELECT COUNT(*) FROM groups").fetchone()[0], 2)
                settings = dict(db.execute("SELECT key,value FROM settings"))
                self.assertEqual(settings["language"], "4")
                self.assertEqual(settings["theme"], "AuroraDark")
                self.assertEqual(settings["remember_enable"], "false")
                self.assertEqual(settings["system_proxy_enabled"], "false")
                self.assertEqual(settings["inbound_socks_port"], "3080")
                self.assertEqual(settings["core_dns_in_port"], "15533")
                self.assertEqual(settings["dns_server_listen_port"], "15353")
                self.assertEqual(settings["redirect_listen_port"], "18443")
                self.assertEqual(settings["tun_mode_enabled"], "false")
                self.assertIn(migration.MIGRATION_MARKER, settings)
            with closing(sqlite3.connect(backup)) as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM profiles").fetchone()[0], 0)

    def test_nonempty_target_is_rejected_without_backup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, database = root / "source", root / "throne.db"
            create_source(source)
            create_fresh_database(database)
            with closing(sqlite3.connect(database)) as db:
                db.execute(
                    "INSERT INTO profiles VALUES(1,'socks','existing',1,0,0,'','','','', '{}',0,0,0,0)",
                )
                db.commit()
            with self.assertRaisesRegex(migration.MigrationError, "target_has_profiles"):
                migration.apply_plan(database, migration.build_plan(source), root / "none.bak")
            self.assertFalse((root / "none.bak").exists())


if __name__ == "__main__":
    unittest.main()
