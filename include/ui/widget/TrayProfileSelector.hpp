#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>
#include <utility>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

// A small frameless popup used from the system-tray menu to pick a proxy profile or a
// routing profile. It replaces the old nested/paginated tray submenus.
//
// Why a widget instead of submenus: a tray context menu is not always painted by Qt.
// On Linux it is exported over SNI/DBusMenu and drawn by the shell, and on macOS it is a
// native NSMenu; in both cases a submenu that is populated lazily/dynamically will not
// reliably expand. A real Qt-drawn window has none of those constraints and behaves
// identically on every platform.
//
// Behaviour: a rounded card with a debounced search box on top over a compact, paginated
// list. A single click (or Enter) selects; it closes when it loses focus (like a menu) and
// also has an explicit Close button.
class TrayProfileSelector : public QFrame {
    Q_OBJECT
public:
    enum Mode { Server, Routing };

    // Data is read live from the config layer; only actions and the "running" snapshot
    // are injected, so the widget stays decoupled from MainWindow's internals.
    struct Callbacks {
        std::function<void(int id)> startProfile;  // start a proxy profile by id
        std::function<void()>       stopProfile;   // stop the running proxy
        std::function<void(int id)> chooseRoute;   // apply a routing profile by id
        std::function<bool()>       isRunning;     // a proxy profile is currently running
        std::function<int()>        runningId;     // running profile id, -1 if none
        std::function<int()>        runningGid;    // running profile's group id, -1 if none
        std::function<QString()>    runningName;   // running profile name, empty if none
    };

    TrayProfileSelector(Mode mode, Callbacks cb, QWidget *parent = nullptr);

    // Show the panel anchored near globalPos (kept fully on the containing screen) and
    // focus the search box. Resets navigation/search to the top level first.
    void popupAt(const QPoint &globalPos);

protected:
    bool event(QEvent *e) override;                     // close when the panel loses activation
    void keyPressEvent(QKeyEvent *e) override;          // Esc closes
    bool eventFilter(QObject *obj, QEvent *e) override; // list/search key handling

private:
    void rebuild();                     // repopulate for the current view/query/page
    void activateItem(QListWidgetItem *it);
    void goBackToGroups();
    void changePage(int delta);
    // Lazily build the per-popup search caches (all id/name pairs + pre-lowercased names),
    // so live filtering scans memory instead of hitting the DB on every keystroke.
    void ensureServerCache();
    void ensureRouteCache();

    Mode m_mode;
    Callbacks m_cb;
    int m_groupId = -1;   // Server mode: -1 = group list; else the drilled-in group id
    int m_page = 0;       // current page within the shown list
    bool m_armed = false; // gate close-on-deactivate until the initial show settles
    QString m_query;      // active (debounced) search text

    // Built once per popup on first search. Server = all profiles across groups; Route =
    // all routing profiles. The *Lower lists are the names pre-folded for fast matching.
    bool m_serverCacheBuilt = false;
    QList<std::pair<int, QString>> m_serverCache;
    QStringList m_serverCacheLower;
    bool m_routeCacheBuilt = false;
    QList<std::pair<int, QString>> m_routeCache;
    QStringList m_routeCacheLower;

    QLineEdit *m_search = nullptr;
    QTimer *m_debounce = nullptr;
    QPushButton *m_backBtn = nullptr;
    QLabel *m_title = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QLabel *m_pageLabel = nullptr;
    QPushButton *m_nextBtn = nullptr;
};
