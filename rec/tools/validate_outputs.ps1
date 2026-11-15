param([Parameter(Mandatory=$true)][string]$BuildDirectory,[ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$build=[IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
if($build.Equals($source,[StringComparison]::OrdinalIgnoreCase) -or $build.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Use an external build directory'}
& cmake --build $build --config $Configuration --parallel
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& ctest --test-dir $build -C $Configuration --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Self-test failed'}
Write-Output 'Build and self-test passed. This does not validate FPS, tracking exports, or real-camera accuracy.'
