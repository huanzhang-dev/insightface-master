param([string]$Destination = '',[string]$DataRoot = '')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
if (!$Destination) { $Destination = Join-Path (Split-Path $sourceRoot -Parent) 'sleep-transfer' }
if (!$DataRoot) { $DataRoot = Join-Path (Split-Path $sourceRoot -Parent) 'sleep-data' }
$Destination = [IO.Path]::GetFullPath($Destination)
if (($Destination+'\').StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package destination must be outside the source tree.' }
& (Join-Path $PSScriptRoot 'setup.ps1') -DataRoot $DataRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
foreach ($name in @('sleep-source.zip','sleep-assets.zip')) { if (Test-Path -LiteralPath (Join-Path $Destination $name)) { throw "Package already exists: $name; choose a new Destination." } }
$sourceArchive = [IO.Compression.ZipFile]::Open((Join-Path $Destination 'sleep-source.zip'),[IO.Compression.ZipArchiveMode]::Create)
try { foreach ($file in (Get-ChildItem $sourceRoot -Recurse -File)) { $relative = $file.FullName.Substring($sourceRoot.Length+1).Replace('\','/'); [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($sourceArchive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $sourceArchive.Dispose() }
$assetArchive = [IO.Compression.ZipFile]::Open((Join-Path $Destination 'sleep-assets.zip'),[IO.Compression.ZipArchiveMode]::Create)
try { foreach ($entry in (Get-Content (Join-Path $sourceRoot 'cmake/assets-manifest.json') -Raw | ConvertFrom-Json)) { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($assetArchive,(Join-Path $DataRoot $entry.path),$entry.path,[IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $assetArchive.Dispose() }
Get-FileHash (Join-Path $Destination 'sleep-source.zip'),(Join-Path $Destination 'sleep-assets.zip') -Algorithm SHA256 | Select-Object Path,Hash | ConvertTo-Json | Set-Content (Join-Path $Destination 'SHA256.json') -Encoding UTF8
Write-Host "Transfer source and asset archives from $Destination; follow docs/OTHER_PC.md."
