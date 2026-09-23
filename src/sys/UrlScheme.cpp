#include "include/sys/UrlScheme.hpp"

#include "include/global/Configs.hpp"

void UrlScheme_RegisterIfNeeded() {
    const QString desired = UrlScheme_DesiredState();
    if (desired.isEmpty()) return;

    auto settings = Configs::dataManager->settingsRepo.get();
    if (settings->url_scheme_mirror == desired) return; // nothing changed; leave the OS untouched

    UrlScheme_Apply();
    settings->url_scheme_mirror = desired;
    settings->Save();
}
