param([string]$DataRoot = '',[string]$AssetArchive = '',[switch]$RestoreModels)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
if (!$DataRoot) { $DataRoot = Join-Path (Split-Path $sourceRoot -Parent) 'sleep-data' }
$DataRoot = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
if (($DataRoot + '\').StartsWith($sourceRoot.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'DataRoot must be outside the source tree.' }
$manifest = Get-Content (Join-Path $sourceRoot 'cmake/assets-manifest.json') -Raw | ConvertFrom-Json
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$archive = $null
if ($AssetArchive) { Add-Type -AssemblyName System.IO.Compression.FileSystem; $archive = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($AssetArchive)) }
try {
    foreach ($entry in $manifest) {
        $path = [IO.Path]::GetFullPath((Join-Path $DataRoot $entry.path))
        if (!$path.StartsWith($DataRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset path escaped DataRoot.' }
        if (!(Test-Path -LiteralPath $path)) {
            New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
            $temporary = $path + '.download'
            if ($archive) {
                $member = $archive.GetEntry($entry.path)
                if (!$member) { throw "Archive member missing: $($entry.path)" }
                $inputStream = $member.Open(); $outputStream = [IO.File]::Create($temporary)
                try { $inputStream.CopyTo($outputStream) } finally { $inputStream.Dispose(); $outputStream.Dispose() }
            } elseif ($RestoreModels -and $entry.url) {
                Invoke-WebRequest -Uri $entry.url -OutFile $temporary -UseBasicParsing
            } else { throw "Missing external asset: $($entry.path). Supply -AssetArchive or copy the external data directory." }
            if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Asset hash mismatch: $($entry.path)" }
            Move-Item -LiteralPath $temporary -Destination $path
        }
        if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Integrity failure: $($entry.path)" }
    }
    foreach ($name in @('tests','output')) { New-Item -ItemType Directory -Path (Join-Path $DataRoot $name) -Force | Out-Null }
} finally { if ($archive) { $archive.Dispose() } }
Write-Host "External assets verified: $DataRoot. Source tree remains free of generated data."
