#include "include/sys/UrlScheme.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>

// macOS registration is declarative: CFBundleURLTypes and CFBundleDocumentTypes
// in Info.plist plus LaunchServices indexing the bundle. Launching from anywhere
// normally registers it, but a moved bundle leaves a stale path, so on change we
// force a re-index of the current location with `lsregister -f`. No file is
// written here.

static const QString kLsregister =
    "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
    "LaunchServices.framework/Support/lsregister";

// applicationDirPath() is <Bundle>.app/Contents/MacOS; the bundle is two up.
static QString bundlePath() {
    QDir d(QCoreApplication::applicationDirPath());
    if (!d.cdUp() || !d.cdUp()) return {};
    const QString path = d.absolutePath();
    return path.endsWith(".app") ? path : QString();
}

QString UrlScheme_DesiredState() {
    const QString bundle = bundlePath();
    return bundle.isEmpty() ? QString() : "v2|" + bundle;
}

void UrlScheme_Apply() {
    const QString bundle = bundlePath();
    if (bundle.isEmpty()) return;
    QProcess::execute(kLsregister, {"-f", bundle});
}
