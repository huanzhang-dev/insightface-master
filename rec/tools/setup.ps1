param([string]$Destination='')
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$destinationRoot=if([string]::IsNullOrWhiteSpace($Destination)){$source}else{[IO.Path]::GetFullPath($Destination).TrimEnd('\')}
$manifest=Get-Content -LiteralPath (Join-Path $source 'cmake\dependencies.json') -Raw | ConvertFrom-Json
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
foreach($item in $manifest) {
 $destination=[IO.Path]::GetFullPath((Join-Path $destinationRoot $item.path))
 if(-not $destination.StartsWith($destinationRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe manifest path'}
 if((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $item.sha256){continue}
 New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
 $temporary=$destination+'.download'
 for($attempt=0;$attempt -lt 3;$attempt++) {
  try {
   Invoke-WebRequest -Uri $item.url -OutFile $temporary -UseBasicParsing -TimeoutSec 300
   if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $item.sha256){throw 'Downloaded checksum does not match manifest'}
   Move-Item -LiteralPath $temporary -Destination $destination -Force
   break
  } catch {if($attempt -eq 2){throw}; Start-Sleep -Seconds 2}
 }
}
if(-not(Test-Path -LiteralPath (Join-Path $destinationRoot 'third_party\opencv\build\include\opencv2\opencv.hpp'))) {
 # Windows tar can read the self-extracting 7z archive without launching its GUI.
 $tar=Get-Command tar.exe -ErrorAction Stop
 & $tar.Source -xf (Join-Path $destinationRoot 'third_party\opencv-complete.exe') -C (Join-Path $destinationRoot 'third_party') 'opencv/build'
 if($LASTEXITCODE -ne 0){throw 'OpenCV SDK extraction failed'}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($pair in @(@('onnxruntime-directml.nupkg','onnxruntime'),@('directml.nupkg','directml'),@('tesseract.nupkg','tesseract'))) {
 $destination=Join-Path $destinationRoot ('third_party\'+$pair[1])
 if(-not(Test-Path -LiteralPath $destination)){[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $destinationRoot ('third_party\'+$pair[0])),$destination)}
}
Write-Output ('Verified dependencies and models in '+$destinationRoot+'. Configure with CMake; see README.txt.')
