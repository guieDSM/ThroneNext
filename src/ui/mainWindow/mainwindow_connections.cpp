#include "include/ui/mainwindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMenu>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QToolTip>
#include <memory>

namespace
{
QString ConnectionRouteTooltip(const Stats::ConnectionMetadata& conn)
{
    const auto missing = QApplication::translate("MainWindow", "Not reported");
    QStringList lines{
        QApplication::translate("MainWindow", "Destination: %1").arg(conn.dest.isEmpty() ? missing : conn.dest),
        QApplication::translate("MainWindow", "Domain: %1").arg(
            conn.domain.isEmpty()
                ? QApplication::translate("MainWindow", "Not observed — domain rules had no hostname metadata for this connection")
                : conn.domain),
        QApplication::translate("MainWindow", "Process: %1").arg(conn.process.isEmpty() ? missing : conn.process),
        QApplication::translate("MainWindow", "Process path: %1").arg(conn.processPath.isEmpty() ? missing : conn.processPath),
        QApplication::translate("MainWindow", "Network: %1").arg(conn.network.isEmpty() ? missing : conn.network),
        QApplication::translate("MainWindow", "Protocol: %1").arg(conn.protocol.isEmpty() ? missing : conn.protocol),
        QApplication::translate("MainWindow", "Actual outbound: %1").arg(conn.outbound.isEmpty() ? missing : conn.outbound),
        QString{},
    };

    if (conn.domain.isEmpty())
    {
        lines << QApplication::translate(
            "MainWindow",
            "No domain was observed. A process rule is usually more reliable than a domain rule for this connection.");
    }
    else
    {
        lines << QApplication::translate(
            "MainWindow",
            "Actual outbound is authoritative for this connection. A page may also contact other domains through different routes.");
    }
    return lines.join('\n');
}

void ApplyConnectionRouteTooltip(QTableWidget* table, int row, const Stats::ConnectionMetadata& conn)
{
    const auto tooltip = ConnectionRouteTooltip(conn);
    for (int column = 0; column < table->columnCount(); ++column)
    {
        if (auto* item = table->item(row, column); item != nullptr)
        {
            item->setToolTip(tooltip);
        }
    }
}
}

void MainWindow::setupConnectionList()
{
    ui->connections->horizontalHeader()->setHighlightSections(false);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    ui->connections->verticalHeader()->hide();
    setupConnectionSortMenu();
    connect(ui->connections, &QTableWidget::cellClicked, this, [=,this](int row, int column)
    {
        auto selected = ui->connections->item(row, column);
        if (selected == nullptr) return;
        QApplication::clipboard()->setText(selected->text());
        QPoint pos = ui->connections->mapToGlobal(ui->connections->visualItemRect(selected).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });
}

// Right-click the Traffic / Speed headers to pick the sub-field they sort by;
// left-clicking still sorts by total.
void MainWindow::setupConnectionSortMenu()
{
    auto* header = ui->connections->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [=,this](const QPoint& pos)
    {
        const int columnIndex = header->logicalIndexAt(pos);
        const bool isTraffic = columnIndex == 4;
        const bool isSpeed = columnIndex == 5;
        if (!isTraffic && !isSpeed) return;

        struct SortOption { Stats::ConnectionSort value; QString label; };
        const QList<SortOption> options = isTraffic
            ? QList<SortOption>{
                { Stats::ByTraffic, tr("Total") },
                { Stats::ByDownload, tr("Downloaded") },
                { Stats::ByUpload, tr("Uploaded") } }
            : QList<SortOption>{
                { Stats::BySpeed, tr("Total") },
                { Stats::ByDownloadSpeed, tr("Download Speed") },
                { Stats::ByUploadSpeed, tr("Upload Speed") } };

        QMenu menu(this);
        auto* sortByLabel = menu.addAction(tr("Sort By:"));
        sortByLabel->setEnabled(false);

        const auto current = Stats::connection_lister->getSort();
        for (const auto& opt : options)
        {
            auto* act = menu.addAction(opt.label);
            act->setData(static_cast<int>(opt.value));
            act->setCheckable(true);
            act->setChecked(current == opt.value);
        }

        auto* chosen = menu.exec(header->mapToGlobal(pos));
        if (chosen == nullptr || !chosen->data().isValid()) return;

        Stats::connection_lister->setSort(static_cast<Stats::ConnectionSort>(chosen->data().toInt()));
        Stats::connection_lister->ForceUpdate();
    });
}

void MainWindow::UpdateConnectionList(const QMap<QString, Stats::ConnectionMetadata>& toUpdate, const QMap<QString, Stats::ConnectionMetadata>& toAdd)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    for (int row=0;row<ui->connections->rowCount();row++)
    {
        const auto key = ui->connections->item(row, 0)->data(Stats::IDKEY).toString();
        if (!toUpdate.contains(key))
        {
            ui->connections->removeRow(row);
            row--;
            continue;
        }

        const auto conn = toUpdate[key];
        // C0: Dest (Domain)
        ui->connections->item(row, 0)->setText(DisplayDest(conn.dest, conn.domain));

        // C1: Process
        ui->connections->item(row, 1)->setText(conn.process);

        // C2: Protocol
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        ui->connections->item(row, 2)->setText(prot);

        // C3: Outbound
        ui->connections->item(row, 3)->setText(conn.outbound);

        // C4: Traffic
        ui->connections->item(row, 4)->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");

        // C5: Speed
        ui->connections->item(row, 5)->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
        ApplyConnectionRouteTooltip(ui->connections, row, conn);
    }
    int row = ui->connections->rowCount();
    for (const auto& conn : toAdd)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        // C5: Speed
        f = f0->clone();
        f->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
        ui->connections->setItem(row, 5, f);

        ApplyConnectionRouteTooltip(ui->connections, row, conn);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

void MainWindow::UpdateConnectionListWithRecreate(const QList<Stats::ConnectionMetadata>& connections)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    ui->connections->setRowCount(0);
    int row=0;
    for (const auto& conn : connections)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        // C5: Speed
        f = f0->clone();
        f->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
        ui->connections->setItem(row, 5, f);

        ApplyConnectionRouteTooltip(ui->connections, row, conn);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}
