#pragma once

#include <QDialog>
#include <QPointer>

#include <atomic>

#include "ui_dialog_runtime_stats.h"

#include "include/sys/ProcessMetrics.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
    class DialogRuntimeStats;
}
QT_END_NAMESPACE

class QTimer;

class DialogRuntimeStats : public QDialog {
    Q_OBJECT

public:
    explicit DialogRuntimeStats(QWidget* parent = nullptr);
    ~DialogRuntimeStats() override;

private:
    void refreshLive();
    void probeEgress();

    Ui::DialogRuntimeStats* ui;
    QTimer* timer_ = nullptr;
    Sys::ProcessMetrics metrics_;
    std::atomic<bool> probing_{false};
    std::atomic<bool> connBusy_{false};

    QString lastProbedConfig_;
    qint64 lastProbeSecs_ = 0;
    bool egressSnapshotDone_ = false;
};
