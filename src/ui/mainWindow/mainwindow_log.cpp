#include "include/ui/mainwindow.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QFontDatabase>
#include <QMenu>
#include <QMutexLocker>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include "3rdparty/qv2ray/v2/ui/LogHighlighter.hpp"

namespace {
    constexpr qsizetype MAX_PENDING_LOG_CHARS = 2 * 1024 * 1024;

    // Bypasses QTextEdit::append()'s per-call layout/scroll work, which dominates
    // when the core spams lines; one edit block per batch instead.
    inline void FastAppendTextDocument(const QString &message, QTextDocument *doc) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::End);
        cursor.beginEditBlock();
        cursor.insertBlock();
        cursor.insertText(message);
        cursor.endEditBlock();
    }
}

void MainWindow::applyLogBrowserFont() {
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    int pt = qApp->font().pointSize();
    if (pt <= 0) pt = Configs::dataManager->settingsRepo->font_size;
    if (pt > 0) logFont.setPointSize(pt);
    ui->masterLogBrowser->setFont(logFont);
}

void MainWindow::setLogHighlighter(bool darkMode) {
    // A QSyntaxHighlighter attaches to the document and is never evicted by
    // constructing another, so the old one must be deleted or they stack up.
    delete logHighlighter;
    logHighlighter = new SyntaxHighlighter(darkMode, qvLogDocument);
}

void MainWindow::append_log(const QString &log) {
    if (log.size() > 20000) {
        append_log(QString("TRUNCATED LONG LOG: ") + log.first(1000) + "...");
        return;
    }
    QMutexLocker locker(&logMutex);
    if (logQueue.size() > 1000) {
        // log is overloaded, just discard it
        return;
    }
    logQueue.enqueue(log);
    if (logQueue.size() == 1) logWaiter.wakeOne();
}

void MainWindow::log_process_loop() {
    while (true) {
        logMutex.lock();
        while (logQueue.isEmpty()) {
            logWaiter.wait(&logMutex);
        }
        // Drain and snapshot under one lock, then filter unlocked: a burst becomes a
        // single UI append and producers never block on the regex work.
        QQueue<QString> pending;
        pending.swap(logQueue);
        const LogFilter filter{
            Configs::dataManager->settingsRepo->log_enable_include,
            Configs::dataManager->settingsRepo->log_enable_exclude,
            includeKeywords, excludeKeywords, includeCombined, excludeCombined,
        };
        logMutex.unlock();

        QString batchToPrint;
        for (const auto& entry : pending) {
            for (const auto& logLine : entry.split('\n')) {
                if (should_print_log(logLine, filter)) {
                    batchToPrint += logLine;
                    batchToPrint += '\n';
                }
            }
        }

        const QString trimmedBatch = batchToPrint.trimmed();
        if (trimmedBatch.isEmpty()) continue;

        bool needsPost;
        {
            QMutexLocker pendingLocker(&logPendingMutex);
            if (!logPendingText.isEmpty()) logPendingText += '\n';
            logPendingText += trimmedBatch;
            if (logPendingText.size() > MAX_PENDING_LOG_CHARS) {
                const auto cut = logPendingText.indexOf('\n', logPendingText.size() - MAX_PENDING_LOG_CHARS);
                logPendingText = cut < 0 ? QString() : logPendingText.mid(cut + 1);
            }
            needsPost = !logFlushScheduled;
            logFlushScheduled = true;
        }
        // At most one flush in flight; anything produced meanwhile is picked up by
        // the flush that is already pending, so the UI event queue cannot grow.
        if (needsPost) runOnUiThread([this] { flush_log_batch(); });
    }
}

void MainWindow::flush_log_batch() {
    QString batch;
    {
        QMutexLocker pendingLocker(&logPendingMutex);
        batch.swap(logPendingText);
        logFlushScheduled = false;
    }
    if (batch.isEmpty()) return;

    auto bar = ui->masterLogBrowser->verticalScrollBar();
    if (Configs::dataManager->settingsRepo->log_auto_scroll) {
        FastAppendTextDocument(batch, qvLogDocument);
        bar->setValue(bar->maximum());
    } else {
        auto layout = qvLogDocument->documentLayout();
        // Anchor to the block at the top of the viewport; if the append
        // shifts its document-Y, replay the original sub-block offset.
        QTextBlock anchorBlock = ui->masterLogBrowser->cursorForPosition(QPoint(0, 0)).block();
        int viewportOffset = bar->value() - static_cast<int>(layout->blockBoundingRect(anchorBlock).y());
        FastAppendTextDocument(batch, qvLogDocument);
        if (anchorBlock.isValid()) {
            int newY = static_cast<int>(layout->blockBoundingRect(anchorBlock).y());
            bar->setValue(newY + viewportOffset);
        }
    }
}

bool MainWindow::should_print_log(const QString &log, const LogFilter &filter) {
    if (QStringView(log).trimmed().isEmpty()) return false;
    bool result = true;
    if (filter.enableInclude) {
        result = false;
        for (const auto& includeKeyword : filter.includeKeywords) {
            if (log.contains(includeKeyword)) {
                result = true;
                break;
            }
        }
        if (!result && !filter.includeCombined.pattern().isEmpty() && filter.includeCombined.match(log).hasMatch()) {
            result = true;
        }
    }
    if (result && filter.enableExclude) {
        for (const auto& excludeKeyword : filter.excludeKeywords) {
            if (log.contains(excludeKeyword)) {
                result = false;
                break;
            }
        }
        if (result && !filter.excludeCombined.pattern().isEmpty() && filter.excludeCombined.match(log).hasMatch()) {
            result = false;
        }
    }
    return result;
}

void MainWindow::on_masterLogBrowser_customContextMenuRequested(const QPoint &pos) {
    QMenu *menu = ui->masterLogBrowser->createStandardContextMenu();

    auto sep = new QAction(this);
    sep->setSeparator(true);
    menu->addAction(sep);

    auto action_clear = new QAction(this);
    action_clear->setText(tr("Clear"));
    connect(action_clear, &QAction::triggered, this, [=,this] {
        {
            // Otherwise a flush already in flight repaints what was just cleared.
            QMutexLocker pendingLocker(&logPendingMutex);
            logPendingText.clear();
        }
        qvLogDocument->clear();
        ui->masterLogBrowser->clear();
    });
    menu->addAction(action_clear);

    menu->exec(ui->masterLogBrowser->viewport()->mapToGlobal(pos));
}
