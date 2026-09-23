#include "include/database/Database.h"
#include <cassert>

// The database reports errors through UI helpers; capture those notifications
// without opening dialogs in this isolated in-memory integration test.
static int notifications = 0;
int MessageBoxWarning(const QString &, const QString &) { ++notifications; return 0; }
void runOnUiThread(const std::function<void()> &callback, bool) { callback(); }

int main() {
    Configs::Database db(":memory:");
    db.exec("CREATE TABLE profiles (id INTEGER PRIMARY KEY, type TEXT, name TEXT, gid INTEGER, "
            "latency INTEGER, latency_at INTEGER, dl_speed TEXT, ul_speed TEXT, test_country TEXT, "
            "ip_out TEXT, outbound_json TEXT, traffic_dl INTEGER, traffic_up INTEGER)");
    Configs::ProfileInsertRow row{};
    row.id = 1;
    row.name = "synthetic";
    row.outbound_json = "{}";
    assert(db.execBatchInsertProfiles({row}));
    assert(notifications == 0);
    assert(!db.execBatchInsertProfiles({row})); // duplicate key must propagate
    assert(notifications == 1);
    auto query = db.query("SELECT count(*) FROM profiles");
    assert(query && query->executeStep() && query->getColumn(0).getInt() == 1);
    query.reset();
    db.exec("PRAGMA query_only=ON");
    row.id = 2;
    assert(!db.execBatchInsertProfiles({row})); // read-only/disk failure path
    assert(notifications == 2);
    query = db.query("SELECT count(*) FROM profiles");
    assert(query && query->executeStep() && query->getColumn(0).getInt() == 1);
}
