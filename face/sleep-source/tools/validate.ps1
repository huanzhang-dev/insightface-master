param([switch]$DirectML,[switch]$UseExistingEvidence)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dataRoot = if ($env:SLEEP_DATA_ROOT) { [IO.Path]::GetFullPath($env:SLEEP_DATA_ROOT) } else { Join-Path (Split-Path $root -Parent) 'sleep-data' }
function Invoke-Checked([string]$Program, [string[]]$NativeArgs) {
    & $Program @NativeArgs
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $Program $NativeArgs" }
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        $stream.Position = 60
        $offset = $reader.ReadInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE file: $Path" }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose(); $stream.Dispose() }
}
& (Join-Path $PSScriptRoot 'setup.ps1') -DataRoot $dataRoot
$results = [ordered]@{ reused_existing_test_evidence = [bool]$UseExistingEvidence; verified_at = (Get-Date).ToString('o'); toolsets = @(); realtime = @() }
foreach ($target in @(@('vs2019','v142'),@('vs2015','v140'))) {
    if (!$UseExistingEvidence) { Invoke-Checked -Program (Join-Path $root ('build_' + $target[0] + '.cmd')) -NativeArgs @() }
    $exe = Join-Path ($root + '-build') ($target[1] + '-x64/bin/Sleep.exe')
    $reportPath = if ($UseExistingEvidence) { Join-Path $dataRoot ('tests/self_test_' + $target[0] + '.json') } else { Join-Path $dataRoot 'tests/self_test_last.json' }
    if ($UseExistingEvidence) {
        $binaryTime = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
        $newerSources = @(Get-ChildItem (Join-Path $root 'src'),(Join-Path $root 'include') -File | Where-Object { $_.LastWriteTimeUtc -gt $binaryTime })
        Assert-True ($newerSources.Count -eq 0) 'Existing binary predates the source; rerun full validation.'
        Assert-True ((Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -ge $binaryTime) 'Existing functional evidence predates the binary.'
    }
    $test = Get-Content $reportPath -Raw | ConvertFrom-Json
    Assert-True ($test.status -eq 'passed' -and $test.checks -ge 45) 'Functional tests did not complete.'
    if (!$UseExistingEvidence) { Copy-Item -LiteralPath $reportPath -Destination (Join-Path $dataRoot ('tests/self_test_' + $target[0] + '.json')) -Force }
    $build = Join-Path ($root + '-build') ($target[1] + '-x64')
    $configuration = (Get-Content (Join-Path $build 'compiler-check.txt') -Raw).Trim()
    $compiler = [regex]::Match($configuration,'(?m)^compiler=(.*)$').Groups[1].Value.Trim()
    $compilerMachine = Get-PeMachine $compiler
    $targetMachine = Get-PeMachine $exe
    Assert-True ($compilerMachine -eq 0x14c -and $targetMachine -eq 0x8664) 'Expected x86-hosted compiler producing an x64 executable.'
    Assert-True ($configuration -match 'cross_compiling=TRUE') 'Cross toolchain was not active.'
    $dumpbin = Join-Path (Split-Path $compiler -Parent) 'dumpbin.exe'
    $importChecks = @()
    foreach ($binary in (Get-ChildItem (Split-Path $exe -Parent) -File | Where-Object { $_.Extension -in @('.exe','.dll') })) {
        $imports = (& $dumpbin /dependents $binary.FullName | Out-String)
        Assert-True ($LASTEXITCODE -eq 0 -and $imports -notmatch 'opencv') 'Forbidden image-library dependency found or import inspection failed.'
        $importChecks += [ordered]@{ binary = $binary.Name; inspected = $true; forbidden_dependency_found = $false }
    }
    $results.toolsets += [ordered]@{ compiler = $target[0]; compiler_pe_machine = '0x014c'; target_pe_machine = '0x8664'; cmake_configuration = $configuration; binary_sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash; tests = $test; imports = $importChecks }
}
$exe = Join-Path ($root + '-build') 'v142-x64/bin/Sleep.exe'
if (!$UseExistingEvidence) { Invoke-Checked -Program $exe -NativeArgs @('--demo') }
$results.videos = Get-Content (Join-Path $dataRoot 'tests/demo_verification.json') -Raw | ConvertFrom-Json
$simulation = @(Get-Content (Join-Path $dataRoot 'output/temporal_simulation.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
$alarms = @($simulation | Where-Object { $_.alarm })
Assert-True ($alarms.Count -gt 0 -and $alarms[0].time_s -ge 10.45 -and $alarms[0].time_s -le 10.65) 'Scripted prolonged-closure alarm timing failed.'
Assert-True (@($simulation | Where-Object { $_.time_s -lt 10.4 -and $_.alarm }).Count -eq 0) 'Blink/early false alarm in scripted simulation.'
Assert-True (@($simulation | Where-Object { $_.time_s -ge 11.3 -and $_.time_s -lt 13.9 -and !$_.alarm }).Count -eq 0) 'Alert cleared during scripted occlusion.'
Assert-True (@($simulation | Where-Object { $_.time_s -gt 16.5 -and $_.alarm }).Count -eq 0) 'Scripted open-eye recovery failed.'
$results.scripted_first_alarm_s = $alarms[0].time_s
foreach ($fps in @(30,15)) {
    $output = Join-Path $dataRoot ('output/realtime' + $fps)
    if (!$UseExistingEvidence) { Invoke-Checked -Program $exe -NativeArgs @('--source',(Join-Path $dataRoot 'testdata/vision_demo_input.avi'),'--headless','--fps',"$fps",'--output',$output) }
    $perf = Get-Content (Join-Path $output 'performance.json') -Raw | ConvertFrom-Json
    Assert-True ($perf.frames -eq 360 -and !$perf.capture_lost) 'Paced input was not fully processed.'
    Assert-True ([Math]::Abs($perf.preview_fps - $fps) -le $fps * .10) "Preview target $fps FPS was not met."
    Assert-True ($perf.inference_fps -ge 25) 'Host did not sustain 25 accepted inferences/second.'
    $observations = @(Get-Content (Join-Path $output 'observations.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    Assert-True (@($observations | Where-Object { $_.alarm }).Count -eq 0) 'Unexpected alarm on the open-eye photo sequence.'
    foreach ($interval in @(@(5.2,6.8),@(7.2,8.8),@(10.2,11.8))) {
        $phase = @($observations | Where-Object { $_.observation_time_s -ge $interval[0] -and $_.observation_time_s -lt $interval[1] })
        Assert-True ($phase.Count -ge 30) 'Insufficient observations in stress-test interval.'
        Assert-True (@($phase | Where-Object { $_.eyes_observable -or $_.state -ne 'UNABLE_TO_ASSESS' }).Count -eq 0) 'Missing visual evidence incorrectly treated as an observable eye state.'
    }
    $results.realtime += $perf
}
if ($DirectML) {
    $output = Join-Path $dataRoot 'output/directml'
    if (!$UseExistingEvidence) { Invoke-Checked -Program $exe -NativeArgs @('--source',(Join-Path $dataRoot 'testdata/vision_demo_input.avi'),'--headless','--offline','--directml','--frames','90','--output',$output) }
    $results.directml_offline = Get-Content (Join-Path $output 'performance.json') -Raw | ConvertFrom-Json
}
$results.source_hashes = @(@(Get-ChildItem (Join-Path $root 'src'),(Join-Path $root 'include'),(Join-Path $root 'cmake') -File; Get-Item (Join-Path $root 'CMakeLists.txt'),(Join-Path $root 'tools/build_cmake.cmd'),(Join-Path $root 'tools/validate.ps1')) | ForEach-Object { [ordered]@{ path = $_.FullName.Substring($root.Length+1); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash } })
$forbidden = @(Get-ChildItem $root -Recurse -File | Where-Object { $_.Name -match 'opencv|\.sln$|\.vcxproj' })
Assert-True ($forbidden.Count -eq 0) 'Forbidden dependency assets or Visual Studio project files remain in the source directory.'
$nonSource = @(Get-ChildItem $root -Recurse -File | Where-Object { $_.Extension -in @('.dll','.exe','.lib','.pdb','.obj','.onnx','.avi','.png','.jpg','.nupkg','.zip','.pyc') })
Assert-True ($nonSource.Count -eq 0) 'Non-source artifacts remain in the source directory.'
$results.source_only = $true
$results.external_data_root = $dataRoot
$results.no_project_local_dependency_files = $true
$results.no_visual_studio_project_files = $true
$results.native_media_stack = 'Windows WIC / Media Foundation / GDI / MJPEG AVI'
$results.status = 'passed'
$results.physical_ir_camera_tested = $false
$results.real_drowsiness_accuracy_measured = $false
$results.real_eyewear_or_3d_pose_accuracy_measured = $false
$results | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $dataRoot 'tests/verification.json') -Encoding UTF8
Write-Host 'PASS: both toolsets, functional/model tests, complete demo AVI decoding, scripted alarm timing and paced 15/30 FPS playback.'
