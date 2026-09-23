#pragma once

// Shared internals of the src/ui/mainWindow/ translation units. Everything must
// be inline or a macro: unity builds merge several of those files into one TU.

#include <QDialog>

#include "include/global/Configs.hpp"
#include "include/database/GroupsRepo.h"

// Single-flight guard: only one settings dialog may be open at a time.
inline bool dialog_is_using = false;

// Single-flight guard shared by every subscription-update entry point.
inline bool mw_sub_updating = false;

// Expands inside a MainWindow member function: uses `this` and `connect`.
#define USE_DIALOG(a)                                    \
    if (dialog_is_using) return;                         \
    dialog_is_using = true;                              \
    auto dialog = new a(this);                           \
    connect(dialog, &QDialog::finished, this, [=, this] { \
        dialog->deleteLater();                           \
        dialog_is_using = false;                         \
    });                                                  \
    dialog->show();

// Tabs are ordered by the user, so a tab index is not a group id.
inline int tabIndex2GroupId(int index) {
    auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    if (tabOrder.length() <= index) return -1;
    return tabOrder[index];
}

inline int groupId2TabIndex(int gid) {
    auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (int key = 0; key < tabOrder.count(); key++) {
        if (tabOrder[key] == gid) return key;
    }
    return 0;
}
