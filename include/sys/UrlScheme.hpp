#pragma once

#include <QString>

// Registration of the "throne://" URL scheme with the OS, and of the app as a
// handler for config files ("Open with Throne"). Both describe the same install
// to the same OS registry, so they are written together.
//
// The file handling is additive everywhere: Throne joins the "open with" list of
// the common config types, and never takes over an extension the user already
// has an application for.
//
// The app is distributed as a portable zip, so there is no installer step and
// the executable path changes whenever the user moves or updates the folder.
// Registration therefore runs at startup, needs no elevation, and self-heals:
// we keep a mirror of the last-written state in settings (url_scheme_mirror)
// and only touch the OS when the current desired state differs from it.

// Per-platform: an opaque string identifying the registration for the current
// install location (e.g. the launch command on Windows, the exec target on
// Linux, the bundle path on macOS), prefixed with a revision of the registration
// itself so that adding entries re-registers installs that never moved.
// Compared against the stored mirror. Returns empty if the scheme cannot be
// registered in the current environment.
QString UrlScheme_DesiredState();

// Per-platform: (re)write the OS registration for the current install location.
void UrlScheme_Apply();

// Common: register only if the desired state differs from the stored mirror,
// then update the mirror. A no-op when nothing changed.
void UrlScheme_RegisterIfNeeded();
