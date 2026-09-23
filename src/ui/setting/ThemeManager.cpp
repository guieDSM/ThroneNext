#include <QStyle>
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QColor>
#include <QMap>

#include "include/ui/setting/ThemeManager.hpp"
#include "iostream"

ThemeManager *themeManager = new ThemeManager;

extern QString ReadFileText(const QString &path);

struct ThemeColors {
    QColor window, windowText;
    QColor base, alternateBase;
    QColor text;
    QColor button, buttonText;
    QColor brightText;
    QColor highlight, highlightedText;
    QColor link;            // paints the active/running config row
    QColor tooltipBase, tooltipText;
    QColor placeholder;
    QColor disabledText;    // dimmed text for disabled controls
};

static QPalette buildThemePalette(const ThemeColors &c) {
    QPalette p;

    const auto setAll = [&](QPalette::ColorRole role, const QColor &col) {
        p.setColor(QPalette::Active, role, col);
        p.setColor(QPalette::Inactive, role, col);
        p.setColor(QPalette::Disabled, role, col);
    };

    setAll(QPalette::Window,          c.window);
    setAll(QPalette::WindowText,      c.windowText);
    setAll(QPalette::Base,            c.base);
    setAll(QPalette::AlternateBase,   c.alternateBase);
    setAll(QPalette::Text,            c.text);
    setAll(QPalette::Button,          c.button);
    setAll(QPalette::ButtonText,      c.buttonText);
    setAll(QPalette::BrightText,      c.brightText);
    setAll(QPalette::ToolTipBase,     c.tooltipBase);
    setAll(QPalette::ToolTipText,     c.tooltipText);
    setAll(QPalette::Highlight,       c.highlight);
    setAll(QPalette::HighlightedText, c.highlightedText);
    setAll(QPalette::Link,            c.link);
    setAll(QPalette::LinkVisited,     c.link);
    setAll(QPalette::PlaceholderText, c.placeholder);

    // Derive the 3D bevel shades from the button tone so any frame/bevel the
    // stylesheet doesn't cover matches the theme instead of Qt's light defaults.
    setAll(QPalette::Light,    c.button.lighter(130));
    setAll(QPalette::Midlight, c.button.lighter(115));
    setAll(QPalette::Mid,      c.button.darker(130));
    setAll(QPalette::Dark,     c.button.darker(160));
    setAll(QPalette::Shadow,   c.window.darker(180));

    // Disabled controls get dimmed text regardless of the group defaults above.
    p.setColor(QPalette::Disabled, QPalette::WindowText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Link,            c.disabledText);

    return p;
}

// Lower-case theme name -> its complete palette. Built lazily on first use so the
// QPalette objects are constructed after QApplication exists. The keys double as
// the definition of "custom theme" used by ApplyTheme.
static const QMap<QString, QPalette> &customThemePalettes() {
    static const QMap<QString, QPalette> palettes = [] {
        QMap<QString, QPalette> m;

        // Light gray, near-monochrome.
        m["flatgray"] = buildThemePalette({
            .window = "#FFFFFF", .windowText = "#57595B",
            .base = "#FFFFFF", .alternateBase = "#F6F6F6",
            .text = "#57595B",
            .button = "#F2F2F2", .buttonText = "#57595B",
            .brightText = "#FFFFFF",
            .highlight = "#D6D6D6", .highlightedText = "#2D2F31",
            .link = "#2A6CB0",
            .tooltipBase = "#FFFFFF", .tooltipText = "#57595B",
            .placeholder = "#9AA0A6", .disabledText = "#B0B0B0",
        });

        // Light blue.
        m["lightblue"] = buildThemePalette({
            .window = "#EAF7FF", .windowText = "#386487",
            .base = "#FFFFFF", .alternateBase = "#DAEFFF",
            .text = "#386487",
            .button = "#DEF0FE", .buttonText = "#386487",
            .brightText = "#FFFFFF",
            .highlight = "#C0DCF2", .highlightedText = "#1B3B57",
            .link = "#1D6FB8",
            .tooltipBase = "#EAF7FF", .tooltipText = "#386487",
            .placeholder = "#7F9DB5", .disabledText = "#A6BCCE",
        });

        // Light pink.
        m["softpink"] = buildThemePalette({
            .window = "#FFF0FB", .windowText = "#883983",
            .base = "#FFFFFF", .alternateBase = "#FBDDF5",
            .text = "#883983",
            .button = "#FCE1F6", .buttonText = "#883983",
            .brightText = "#FFFFFF",
            .highlight = "#F1C1E7", .highlightedText = "#5A2456",
            .link = "#B92BA6",
            .tooltipBase = "#FFF0FB", .tooltipText = "#883983",
            .placeholder = "#C08BBA", .disabledText = "#CBA6C6",
        });

        // Dark gray.
        m["blacksoft"] = buildThemePalette({
            .window = "#444444", .windowText = "#DCDCDC",
            .base = "#444444", .alternateBase = "#525252",
            .text = "#DCDCDC",
            .button = "#484848", .buttonText = "#DCDCDC",
            .brightText = "#FFFFFF",
            .highlight = "#646464", .highlightedText = "#FFFFFF",
            .link = "#5AB0FF",
            .tooltipBase = "#484848", .tooltipText = "#DCDCDC",
            .placeholder = "#9A9A9A", .disabledText = "#808080",
        });

        // QDarkStyle (dark navy). Colors mirror the bundled darkstyle.qss.
        m["qdarkstyle"] = buildThemePalette({
            .window = "#19232D", .windowText = "#DFE1E2",
            .base = "#19232D", .alternateBase = "#37414F",
            .text = "#DFE1E2",
            .button = "#455364", .buttonText = "#DFE1E2",
            .brightText = "#FFFFFF",
            .highlight = "#346792", .highlightedText = "#DFE1E2",
            .link = "#6FC0FF",
            .tooltipBase = "#346792", .tooltipText = "#DFE1E2",
            .placeholder = "#9DA9B5", .disabledText = "#788D9C",
        });

        // A calmer, higher-contrast dark theme designed for long-running desktop
        // use. The cool blue accent keeps active routes and connection state easy
        // to scan without turning every control into a bright focal point.
        m["auroradark"] = buildThemePalette({
            .window = "#0B1017", .windowText = "#E7EEF7",
            .base = "#0F1722", .alternateBase = "#131E2B",
            .text = "#E7EEF7",
            .button = "#172333", .buttonText = "#E7EEF7",
            .brightText = "#FFFFFF",
            .highlight = "#2F7FD3", .highlightedText = "#FFFFFF",
            .link = "#62B4FF",
            .tooltipBase = "#182536", .tooltipText = "#F4F8FC",
            .placeholder = "#748399", .disabledText = "#536277",
        });

        return m;
    }();
    return palettes;
}

void ThemeManager::ApplyTheme(const QString &theme, bool force) {
    if (this->system_style_name.isEmpty()) {
        this->system_style_name = qApp->style()->name();
        this->system_palette = qApp->palette();
    }

    if (this->current_theme == theme && !force) {
        return;
    }

    const auto lowerTheme = theme.toLower();
    const auto &palettes = customThemePalettes();
    const bool leavingCustom = palettes.contains(current_theme.toLower());
    const bool enteringCustom = palettes.contains(lowerTheme);

    if (enteringCustom) {
        // Custom themes own their whole look: install the complete palette first
        // so no color role leaks from Qt or a previously applied theme, then
        // layer the stylesheet on top.
        qApp->setPalette(palettes.value(lowerTheme));
        if (lowerTheme == "auroradark") {
            qApp->setStyle("Fusion");
            qApp->setStyleSheet(ReadFileText(":/qss/auroradark.css"));
        } else if (lowerTheme == "qdarkstyle") {
            qApp->setStyleSheet(ReadFileText(":/qdarkstyle/dark/darkstyle.qss"));
        } else {
            qApp->setStyleSheet(ReadFileText(":/qss/" + lowerTheme + ".css"));
        }
    } else if (lowerTheme == "system") {
        // Back to the OS style + palette we snapshotted on first apply.
        if (leavingCustom) qApp->setPalette(system_palette);
        qApp->setStyleSheet("");
        qApp->setStyle(system_style_name);
    } else {
        // A Qt QStyleFactory style (Fusion, windows11, ...). Let the Qt style own
        // the palette; just drop any custom palette we installed before.
        if (leavingCustom) qApp->setPalette(system_palette);
        qApp->setStyleSheet("");
        qApp->setStyle(theme);
    }

    current_theme = theme;

    emit themeChanged(theme);
}
