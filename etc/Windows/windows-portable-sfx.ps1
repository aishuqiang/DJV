param(
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,

    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ZipPath)) {
    throw "Portable ZIP not found: $ZipPath"
}

$zipItem = Get-Item -LiteralPath $ZipPath
if (-not $OutputPath) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($zipItem.Name)
    $OutputPath = Join-Path $zipItem.DirectoryName "$baseName-portable.exe"
}

$programFilesRoots = @(
    $env:ProgramFiles,
    [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
) | Where-Object { $_ }

$sevenZipCandidates = foreach ($root in $programFilesRoots) {
    Join-Path $root "7-Zip\7z.exe"
}
$sevenZipCandidates = $sevenZipCandidates | Where-Object { Test-Path -LiteralPath $_ }

if (-not $sevenZipCandidates) {
    throw "7-Zip was not found. Install 7-Zip or run this script on GitHub Actions windows-latest."
}

$sevenZip = $sevenZipCandidates[0]
$sevenZipDir = Split-Path -Parent $sevenZip
$sfxCandidates = @(
    (Join-Path $sevenZipDir "7z.sfx")
) | Where-Object { Test-Path -LiteralPath $_ }

if (-not $sfxCandidates) {
    throw "7-Zip SFX module was not found next to $sevenZip."
}

$sfxModule = $sfxCandidates[0]
$workDir = Join-Path $env:TEMP ("djv-sfx-" + [Guid]::NewGuid().ToString("N"))
$extractDir = Join-Path $workDir "extract"
$archivePath = Join-Path $workDir "payload.7z"
$configPath = Join-Path $workDir "config.txt"

New-Item -ItemType Directory -Path $extractDir | Out-Null
Expand-Archive -Path $zipItem.FullName -DestinationPath $extractDir -Force

$roots = Get-ChildItem -LiteralPath $extractDir -Directory
if ($roots.Count -ne 1) {
    throw "Expected the portable ZIP to contain exactly one root folder, found $($roots.Count)."
}

$portableRoot = $roots[0].FullName
$batPath = Join-Path $portableRoot "DJV.bat"
if (-not (Test-Path -LiteralPath $batPath)) {
    throw "DJV.bat was not found in portable package root: $portableRoot"
}
$exePath = Join-Path $portableRoot "bin\djv.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "bin\djv.exe was not found in portable package root: $portableRoot"
}

Push-Location $portableRoot
try {
    & $sevenZip a -t7z -mx=9 $archivePath ".\*"
}
finally {
    Pop-Location
}

if ($LASTEXITCODE -ne 0) {
    throw "7-Zip failed to create SFX payload archive."
}

$config = @'
;!@Install@!UTF-8!
Title="DJV Portable"
ExtractTitle="Extracting DJV Portable"
ExtractDialogText="Preparing DJV..."
GUIMode="1"
RunProgram="bin\\djv.exe"
;!@InstallEnd@!
'@

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($configPath, $config, $utf8NoBom)

$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDir = Split-Path -Parent $outputFullPath
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$outStream = [System.IO.File]::Create($outputFullPath)
try {
    foreach ($part in @($sfxModule, $configPath, $archivePath)) {
        $bytes = [System.IO.File]::ReadAllBytes($part)
        $outStream.Write($bytes, 0, $bytes.Length)
    }
}
finally {
    $outStream.Close()
}

Write-Host "Portable self-extracting EXE created:"
Write-Host $outputFullPath
