param(
    [Parameter(Mandatory)] [string] $QtRoot,
    [Parameter(Mandatory)] [string] $MinGWBin,
    [Parameter(Mandatory)] [string] $Makensis,
    [Parameter(Mandatory)] [string] $CronetDll,
    [string] $BuildDir = (Join-Path $PSScriptRoot '..\..\build'),
    [string] $OutputFile = (Join-Path $PSScriptRoot '..\..\ThroneNext-Windows-x64-Setup.exe')
)

$ErrorActionPreference = 'Stop'
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = (Resolve-Path $BuildDir).Path
$qtRoot = (Resolve-Path $QtRoot).Path
$minGWBin = (Resolve-Path $MinGWBin).Path
$makensisPath = (Resolve-Path $Makensis).Path
$cronetPath = (Resolve-Path $CronetDll).Path
$outputPath = [IO.Path]::GetFullPath($OutputFile)
$stage = Join-Path $buildDir 'package\stage'
$generated = Join-Path $buildDir 'package\generated'

foreach ($name in @('Throne.exe', 'ThroneCore.exe')) {
    if (-not (Test-Path (Join-Path $buildDir $name))) { throw "Missing compiled $name in $buildDir" }
}
if ($buildDir -ne $sourceDir -and -not $buildDir.StartsWith($sourceDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDir must be inside the source checkout.'
}
$packageDir = Join-Path $buildDir 'package'
if (Test-Path $packageDir) { Remove-Item -LiteralPath $packageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stage, $generated -Force | Out-Null

Copy-Item (Join-Path $buildDir 'Throne.exe') $stage
Copy-Item (Join-Path $buildDir 'ThroneCore.exe') $stage
Copy-Item $cronetPath (Join-Path $stage 'libcronet.dll')
$previousPath = $env:Path
try {
    $env:Path = "$(Join-Path $qtRoot 'bin');$minGWBin;$previousPath"
    & (Join-Path $qtRoot 'bin\windeployqt.exe') --release --no-translations --dir $stage (Join-Path $stage 'Throne.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
} finally {
    $env:Path = $previousPath
}
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item (Join-Path $minGWBin $name) $stage
}
New-Item -ItemType Directory -Path (Join-Path $stage 'translations') -Force | Out-Null
Copy-Item (Join-Path $buildDir 'ru_RU.qm') (Join-Path $stage 'translations\ru_RU.qm')

$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
if (-not $files.Count) { throw 'Staging directory is empty.' }
$installLines = @()
$uninstallLines = @()
$dirs = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($file in $files) {
    $rel = $file.FullName.Substring($stage.Length + 1)
    if ($rel -match '(?i)(^|\\)(config|data|logs|profiles|secrets)(\\|$)' -or $rel -match '(?i)\.(db|sqlite|sqlite3|log|json|pem|key|p12|pfx|bak)$') {
        throw "Private or mutable data found in package: $rel"
    }
    $relativeDir = [IO.Path]::GetDirectoryName($rel)
    $targetDir = if ($relativeDir) { "`$INSTDIR\$relativeDir" } else { '$INSTDIR' }
    $installLines += "SetOutPath `"$targetDir`""
    $installLines += "File `"$($file.FullName)`""
    $uninstallLines += "Delete `"`$INSTDIR\$rel`""
    if ($relativeDir) { [void] $dirs.Add($relativeDir) }
}
foreach ($dir in ($dirs | Sort-Object Length -Descending)) { $uninstallLines += "RMDir `"`$INSTDIR\$dir`"" }
$installInclude = Join-Path $generated 'install-files.nsh'
$uninstallInclude = Join-Path $generated 'uninstall-files.nsh'
[IO.File]::WriteAllLines($installInclude, $installLines)
[IO.File]::WriteAllLines($uninstallInclude, $uninstallLines)

& $makensisPath "/DSOURCE_DIR=$sourceDir" "/DOUTPUT_FILE=$outputPath" "/DINSTALL_FILES=$installInclude" "/DUNINSTALL_FILES=$uninstallInclude" (Join-Path $PSScriptRoot 'ThroneNext.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed: $LASTEXITCODE" }
$hash = Get-FileHash $outputPath -Algorithm SHA256
Set-Content -LiteralPath ($outputPath + '.sha256') -Encoding ascii -Value "$($hash.Hash.ToLowerInvariant()) *$([IO.Path]::GetFileName($outputPath))"
$hash | Format-List Path, Hash
