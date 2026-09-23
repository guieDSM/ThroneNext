#include "include/ui/setting/RawRouteItem.h"

#include "include/global/Configs.hpp"
#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

// ---------------------------------------------------------------- RawRouteEdit

RawRouteEdit::RawRouteEdit(QWidget* parent) : QPlainTextEdit(parent) {
    setLineWrapMode(QPlainTextEdit::NoWrap);
    completer = new QCompleter(this);
    completer->setModel(new QStringListModel(completer));
    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    connect(completer, static_cast<void (QCompleter::*)(const QString&)>(&QCompleter::activated),
            this, &RawRouteEdit::insertOutboundCompletion);
}

void RawRouteEdit::setOutboundItems(const QList<QPair<QString, QString>>& items) {
    QStringList display;
    outboundIdByDisplay.clear();
    for (const auto& [text, id] : items) {
        display << text;
        outboundIdByDisplay.insert(text, id);
    }
    completer->setModel(new QStringListModel(display, completer));
}

bool RawRouteEdit::outboundContext(QString* partial) const {
    const QTextCursor tc = textCursor();
    const QString line = tc.block().text().left(tc.positionInBlock());
    // cursor is in a bare (unquoted) value right after "outbound": or "final":
    static const QRegularExpression re(QStringLiteral(R"rx("(?:outbound|final)"\s*:\s*([^",}\]\s]*)$)rx"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return false;
    if (partial) *partial = m.captured(1);
    return true;
}

void RawRouteEdit::insertOutboundCompletion(const QString& completion) {
    // completion is the popup display text (e.g. "[Group] Name"); insert its mapped id.
    const QString id = outboundIdByDisplay.value(completion);
    if (id.isEmpty()) return;
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, completer->completionPrefix().length());
    tc.removeSelectedText();
    tc.insertText(id);
    setTextCursor(tc);
}

void RawRouteEdit::updateCompleter() {
    QString partial;
    if (!outboundContext(&partial)) {
        completer->popup()->hide();
        return;
    }
    if (partial != completer->completionPrefix()) {
        completer->setCompletionPrefix(partial);
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
    }
    QRect cr = cursorRect();
    cr.setWidth(completer->popup()->sizeHintForColumn(0) + completer->popup()->verticalScrollBar()->sizeHint().width());
    completer->complete(cr);
}

QChar RawRouteEdit::charBeforeCursor() const {
    const QTextCursor tc = textCursor();
    if (tc.positionInBlock() == 0) return {};
    return tc.block().text().at(tc.positionInBlock() - 1);
}

QChar RawRouteEdit::charAfterCursor() const {
    const QTextCursor tc = textCursor();
    const QString t = tc.block().text();
    if (tc.positionInBlock() >= t.length()) return {};
    return t.at(tc.positionInBlock());
}

bool RawRouteEdit::handleAutoEdit(QKeyEvent* e) {
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool plainOrShift = (mods & ~Qt::ShiftModifier) == 0;

    // Enter: keep the current line's indentation; open a block when between a pair.
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && plainOrShift) {
        QTextCursor tc = textCursor();
        if (tc.hasSelection()) return false;
        QString indent;
        for (const QChar ch : tc.block().text()) {
            if (ch == ' ' || ch == '\t') indent += ch;
            else break;
        }
        const QChar before = charBeforeCursor();
        const QChar after = charAfterCursor();
        const bool pair = (before == '{' && after == '}') || (before == '[' && after == ']');
        const bool opens = (before == '{' || before == '[');
        tc.beginEditBlock();
        if (pair) {
            tc.insertText("\n" + indent + QStringLiteral("  ") + "\n" + indent);
            tc.movePosition(QTextCursor::Up);
            tc.movePosition(QTextCursor::EndOfBlock);
        } else if (opens) {
            tc.insertText("\n" + indent + QStringLiteral("  "));
        } else {
            tc.insertText("\n" + indent);
        }
        tc.endEditBlock();
        setTextCursor(tc);
        return true;
    }

    // Backspace inside an empty pair removes both halves.
    if (key == Qt::Key_Backspace && mods == Qt::NoModifier) {
        const QChar before = charBeforeCursor();
        const QChar after = charAfterCursor();
        if ((before == '{' && after == '}') || (before == '[' && after == ']') || (before == '"' && after == '"')) {
            QTextCursor tc = textCursor();
            tc.deletePreviousChar();
            tc.deleteChar();
            setTextCursor(tc);
            return true;
        }
        return false;
    }

    const QString t = e->text();
    if (t.isEmpty()) return false;
    const QChar typed = t.at(0);

    // Typing a closing bracket/quote right before its match just steps over it.
    if ((typed == '}' || typed == ']' || typed == '"') && charAfterCursor() == typed) {
        QTextCursor tc = textCursor();
        tc.movePosition(QTextCursor::Right);
        setTextCursor(tc);
        return true;
    }

    // Auto-close an opening bracket/quote and place the cursor inside.
    if (typed == '{' || typed == '[' || typed == '"') {
        if (textCursor().hasSelection()) return false;
        if (typed == '"' && charAfterCursor().isLetterOrNumber()) return false;
        const QChar close = typed == '{' ? QChar('}') : (typed == '[' ? QChar(']') : QChar('"'));
        QTextCursor tc = textCursor();
        tc.insertText(QString(typed) + close);
        tc.movePosition(QTextCursor::Left);
        setTextCursor(tc);
        return true;
    }

    return false;
}

void RawRouteEdit::keyPressEvent(QKeyEvent* e) {
    if (completer->popup()->isVisible()) {
        switch (e->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                e->ignore();
                return; // let the popup handle it
            default:
                break;
        }
    }

    if (handleAutoEdit(e)) {
        updateCompleter();
        return;
    }

    QPlainTextEdit::keyPressEvent(e);
    updateCompleter();
}

// ---------------------------------------------------------------- RawRouteItem

RawRouteItem::RawRouteItem(QWidget* parent, const std::shared_ptr<Configs::RouteProfile>& routeChain) : QDialog(parent) {
    setWindowTitle(tr("Raw routing profile"));
    chain = std::make_shared<Configs::RouteProfile>(*routeChain);
    chain->isRaw = true;

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    nameEdit = new QLineEdit(chain->name, this);
    form->addRow(tr("Name"), nameEdit);
    layout->addLayout(form);

    preventCheck = new QCheckBox(tr("Prevent modifications"), this);
    preventCheck->setChecked(chain->preventModifications);
    preventCheck->setToolTip(tr("Use the route object as written (outbound ids are resolved to tags; process rules enable find_process).\n"
                                "Throne will NOT add its DNS-hijack or xray bridge plumbing, so DNS, chained/xray\n"
                                "outbounds and other Throne features may break. For advanced users only."));
    layout->addWidget(preventCheck);

    jsonEdit = new RawRouteEdit(this);
    jsonEdit->setPlainText(chain->rawRoute.isEmpty()
        ? QStringLiteral("{\n"
                         "  \"rules\": [\n"
                         "    {\n"
                         "      \"domain_suffix\": [\".example.com\"],\n"
                         "      \"outbound\": -2\n"
                         "    }\n"
                         "  ],\n"
                         "  \"final\": -1\n"
                         "}")
        : chain->rawRoute);
    layout->addWidget(jsonEdit, 1);

    // outbound suggestions: same display as the structured route editor's outbound selector —
    // plain proxy/direct/warp-bypass, then "[Group] Name" in group order. Only the id is inserted.
    QList<QPair<QString, QString>> items;
    items.append({QStringLiteral("proxy"), QString::number(-1)});
    items.append({QStringLiteral("direct"), QString::number(-2)});
    items.append({QStringLiteral("warp-bypass"), QString::number(Configs::warpBypassID)});
    QMap<int, QString> idToName;
    for (const auto& [pid, pname] : Configs::dataManager->profilesRepo->GetAllProfileIDNameMapped())
        idToName.insert(pid, pname);
    for (int groupID : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(groupID);
        if (!group) continue;
        for (int profileID : group->profiles) {
            if (!idToName.contains(profileID)) continue;
            items.append({QString("[%1] %2").arg(group->name, idToName[profileID]), QString::number(profileID)});
        }
    }
    jsonEdit->setOutboundItems(items);

    validateLabel = new QLabel(this);
    layout->addWidget(validateLabel);
    auto validate = [this] {
        QJsonParseError err{};
        QJsonDocument::fromJson(jsonEdit->toPlainText().toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            validateLabel->setText(tr("Valid JSON"));
            validateLabel->setStyleSheet(QStringLiteral("color: #2e7d32;"));
        } else {
            validateLabel->setText(tr("Invalid JSON: %1 (offset %2)").arg(err.errorString()).arg(err.offset));
            validateLabel->setStyleSheet(QStringLiteral("color: #c62828;"));
        }
    };
    connect(jsonEdit, &QPlainTextEdit::textChanged, this, validate);
    validate();

    auto* formatBtn = new QPushButton(tr("Format JSON"), this);
    connect(formatBtn, &QPushButton::clicked, this, [this] {
        const auto doc = QJsonDocument::fromJson(jsonEdit->toPlainText().toUtf8());
        if (!doc.isObject()) {
            MessageBoxInfo(tr("Raw route"), tr("The route must be a valid JSON object"));
            return;
        }
        jsonEdit->setPlainText(QJsonObject2QString(doc.object(), false));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    auto* bottom = new QHBoxLayout();
    bottom->addWidget(formatBtn);
    bottom->addStretch();
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    connect(buttons, &QDialogButtonBox::accepted, this, &RawRouteItem::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(640, 520);
}

void RawRouteItem::accept() {
    chain->name = nameEdit->text();
    if (chain->name.isEmpty()) {
        MessageBoxWarning(tr("Invalid operation"), tr("Cannot create Route Profile with empty name"));
        return;
    }
    const auto doc = QJsonDocument::fromJson(jsonEdit->toPlainText().toUtf8());
    if (!doc.isObject()) {
        MessageBoxWarning(tr("Invalid route"), tr("The route must be a valid JSON object"));
        return;
    }
    chain->isRaw = true;
    chain->preventModifications = preventCheck->isChecked();
    chain->rawRoute = QJsonObject2QString(doc.object(), false);
    emit settingsChanged(chain);
    QDialog::accept();
}
