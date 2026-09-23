#pragma once

#include <QDialog>
#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <functional>
#include <memory>

namespace Configs { class RouteProfile; }

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTimer;
class QTcpServer;

// A guided editor for direct routing exceptions. The callbacks let MainWindow
// temporarily preview a route and restart only the active proxy/core afterward.
class QuickRouteDialog final : public QDialog {
public:
    enum class StartTab { Site, Application };
    using PreviewCallback = std::function<bool(const QJsonObject&, QString*)>;
    using PreviewReadyCallback = std::function<bool()>;
    using FinishPreviewCallback = std::function<void()>;
    using SavedCallback = std::function<void(bool activeChanged)>;
    using ValidateCallback = std::function<bool(const std::shared_ptr<Configs::RouteProfile>&, QString*)>;
    using OpenRoutesCallback = std::function<void()>;

    QuickRouteDialog(QWidget* parent, StartTab initialTab, PreviewCallback beginPreview,
                     PreviewReadyCallback previewReady,
                     FinishPreviewCallback finishPreview, SavedCallback saved, ValidateCallback validate,
                     OpenRoutesCallback openRoutes);
    ~QuickRouteDialog() override;

private:
    void populateProfiles();
    void populateApplications();
    void populateManaged();
    void refreshTarget();
    void refreshCandidateView();
    void refreshAppSelection();
    void startScan();
    void stopScan();
    void saveException();
    void removeException();
    void editException();
    void updateProfileSelection();
    void acceptBrowserCapture(const QByteArray& body);
    QJsonObject draftRecord(QString* error) const;
    QList<int> selectedProfiles() const;
    bool saveManaged(const QJsonObject& record, bool remove, QString* error);

    PreviewCallback beginPreview_;
    PreviewReadyCallback previewReady_;
    FinishPreviewCallback finishPreview_;
    SavedCallback saved_;
    ValidateCallback validate_;
    OpenRoutesCallback openRoutes_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* address_ = nullptr;
    QLineEdit* extraDomain_ = nullptr;
    QGroupBox* relatedBox_ = nullptr;
    QLineEdit* appSearch_ = nullptr;
    QLabel* addressInfo_ = nullptr;
    QLabel* scanInfo_ = nullptr;
    QLabel* emptyCandidates_ = nullptr;
    QLabel* appSelection_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QPushButton* addDomainButton_ = nullptr;
    QStackedWidget* candidatePages_ = nullptr;
    QListWidget* candidates_ = nullptr;
    QListWidget* profiles_ = nullptr;
    QTreeWidget* managed_ = nullptr;
    QPlainTextEdit* ruleDetails_ = nullptr;
    QTreeWidget* installed_ = nullptr;
    QTreeWidget* running_ = nullptr;
    QCheckBox* rememberProfiles_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QTcpServer* captureServer_ = nullptr;
    bool previewActive_ = false;
    bool scanning_ = false;
    bool applicationsLoaded_ = false;
    QJsonObject editingRecord_;
    QStringList lastSkippedSelectors_;
    qint64 scanStartedAtMs_ = 0;
};
