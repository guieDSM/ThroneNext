#pragma once

#include <QDialog>
#include <QHash>
#include <QPair>
#include <QPlainTextEdit>
#include <memory>

#include "include/database/entities/RouteProfile.h"

class QLineEdit;
class QCheckBox;
class QCompleter;
class QLabel;

// JSON editor with a context-aware outbound completer: while the cursor is in the value of
// an "outbound" or "final" key it suggests "[id] Name" entries and inserts just the id.
class RawRouteEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit RawRouteEdit(QWidget* parent = nullptr);
    // Each item is a (display, id) pair: the popup shows the display text (e.g. "[Group] Name")
    // while only the id is inserted into the document.
    void setOutboundItems(const QList<QPair<QString, QString>>& items);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void insertOutboundCompletion(const QString& completion);

private:
    [[nodiscard]] bool outboundContext(QString* partial) const;
    void updateCompleter();
    // Code-editor conveniences (auto-close brackets/quotes, skip-over, pair-delete,
    // indentation-preserving newline). Returns true when it consumed the key event.
    bool handleAutoEdit(QKeyEvent* e);
    [[nodiscard]] QChar charBeforeCursor() const;
    [[nodiscard]] QChar charAfterCursor() const;
    QCompleter* completer = nullptr;
    QHash<QString, QString> outboundIdByDisplay; // popup display text -> id to insert
};

// Editor for a "raw" routing profile: a full sing-box route object as JSON.
class RawRouteItem : public QDialog {
    Q_OBJECT

public:
    explicit RawRouteItem(QWidget* parent = nullptr, const std::shared_ptr<Configs::RouteProfile>& routeChain = nullptr);

    std::shared_ptr<Configs::RouteProfile> chain;

signals:
    void settingsChanged(std::shared_ptr<Configs::RouteProfile> routingChain);

private slots:
    void accept() override;

private:
    QLineEdit* nameEdit = nullptr;
    RawRouteEdit* jsonEdit = nullptr;
    QCheckBox* preventCheck = nullptr;
    QLabel* validateLabel = nullptr;
};
