#pragma once

#include <QObject>
#include <QPalette>

class ThemeManager : public QObject {
    Q_OBJECT
public:
    QString system_style_name = "";
    QPalette system_palette;     // snapshot of the OS palette, taken on first apply
    QString current_theme = "0"; // int: 0:system 1+:builtin string: QStyleFactory

    void ApplyTheme(const QString &theme, bool force = false);
signals:
    void themeChanged(QString themeName);
};

extern ThemeManager *themeManager;
