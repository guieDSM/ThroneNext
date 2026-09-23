#pragma once

void Windows_SetCrashHandler();

// Until this runs, a crash produces no files. Call once Logging::Init() has.
void Windows_SetCrashDumpPath();

// WER LocalDumps, which catches what the exception filter cannot see: __fastfail,
// heap corruption, and faults in the filter itself. Needs admin (HKLM only), so
// it no-ops until Throne happens to run elevated.
void Windows_ConfigureWER();
