# Windows x64 installer

`package.ps1` takes freshly compiled `build/Throne.exe` and `build/ThroneCore.exe`, deploys the Qt runtime, and creates `ThroneNext-Windows-x64-Setup.exe` in the repository root. The installer creates desktop and Start menu shortcuts, installs per user without elevation, and preserves `config` on uninstall.

Required inputs: Qt 6.11.1 MinGW, MinGW 13.1, NSIS, and `libcronet.dll` compatible with the Go core. Compile the GUI with CMake into `build/Throne.exe`; `build/srslist.h` is required and is fetched in the fork's build workflow. Build the Go core with the tags in `script/build_go.sh` and put `ThroneCore.exe` in `build/`. That script normally writes it to `deployment/windows-amd64/`, so copy it to `build/` before packaging. For example:

```powershell
.\packaging\windows\package.ps1 `
  -QtRoot 'C:\Qt\6.11.1\mingw_64' `
  -MinGWBin 'C:\Qt\Tools\mingw1310_64\bin' `
  -Makensis 'C:\Program Files (x86)\NSIS\makensis.exe' `
  -CronetDll 'C:\build\libcronet.dll'
```

The packaging script accepts only compiled files and Qt deployment output. It rejects configuration, databases, logs, and credentials from its staging directory. The installed executable requires its adjacent runtime files; move or pin the shortcut, rather than moving `Throne.exe` by itself.
