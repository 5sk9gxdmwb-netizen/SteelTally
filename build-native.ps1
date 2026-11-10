param([string]$QtRoot='C:\Qt\6.8.3\mingw_64',[string]$CompilerRoot='C:\Qt\Tools\mingw1310_64',
      [switch]$DisableUpdates, [string]$UpdateConfig='',
 [string]$UpdateUrl='', [string]$UpdatePublicKeyFile='',
      [string]$BuildRoot=(Join-Path $env:TEMP ('SteelTally-native-' + [guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
if(-not $UpdateConfig){$UpdateConfig=Join-Path $PSScriptRoot 'update-release.json'}
$env:PATH="$CompilerRoot\bin;$QtRoot\bin;$env:SystemRoot\System32;$env:SystemRoot"
if ($BuildRoot -match '[^\x00-\x7F]') { throw 'BuildRoot must be an ASCII path' }
if (Test-Path -LiteralPath $BuildRoot) { throw 'BuildRoot must be a new directory' }
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'table_qty\src') -Destination $BuildRoot -Recurse
$src=Join-Path $BuildRoot 'src'
$updateKey=''
if($DisableUpdates){
 if($UpdateUrl -or $UpdatePublicKeyFile){throw 'DisableUpdates conflicts with explicit update source'}
 $UpdateUrl=''
}elseif(-not $UpdateUrl -and -not $UpdatePublicKeyFile){
 $releaseConfig=Get-Content -LiteralPath $UpdateConfig -Raw -Encoding UTF8 | ConvertFrom-Json
 $UpdateUrl=[string]$releaseConfig.url
 $updateKey=[string]$releaseConfig.public_key
 if(-not $UpdateUrl -or -not $updateKey){throw 'Release update config is incomplete'}
}
if($UpdatePublicKeyFile){$updateKey=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $UpdatePublicKeyFile).Path).Trim()}
if($UpdateUrl -and -not $updateKey){throw 'Update public key required'}
if($UpdateUrl -and ($UpdateUrl -notmatch '^https://' -or $UpdateUrl -match '["\\\r\n]')){throw 'Invalid HTTPS update URL'}
if($updateKey -match '[^A-Za-z0-9+/=]'){throw 'Public key must be base64'}
$template=[IO.File]::ReadAllText((Join-Path $src 'updateconfig.h.in'))
[IO.File]::WriteAllText((Join-Path $src 'updateconfig.h'),$template.Replace('@STEELTALLY_UPDATE_URL@',$UpdateUrl).Replace('@STEELTALLY_UPDATE_PUBLIC_KEY@',$updateKey),(New-Object Text.UTF8Encoding($false)))
$out=Join-Path $BuildRoot 'build'
New-Item -ItemType Directory -Path $out | Out-Null
$include=@("-I$src","-I$QtRoot\include")
foreach($module in @('Core','Gui','Widgets','Svg','PrintSupport','Network')) { $include+="-I$QtRoot\include\Qt$module" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'table_qty\assets') -Destination $BuildRoot -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'table_qty\resources.qrc') -Destination $BuildRoot
$resources=Join-Path $out 'qrc_resources.cpp'
& "$QtRoot\bin\rcc.exe" --name steeltally_resources (Join-Path $BuildRoot 'resources.qrc') -o $resources
if($LASTEXITCODE){throw 'Resource compilation failed'}
$moc=@()
foreach($name in @('mainwindow','tableinputpanel','sectioncalculator')) {
    $target="$out\moc_$name.cpp"
    & "$QtRoot\bin\moc.exe" @include "$src\$name.h" -o $target
    if($LASTEXITCODE){throw "moc failed: $name"}
    $moc+=$target
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'table_qty\steeltally.rc') -Destination $BuildRoot
$iconResource=Join-Path $out 'steeltally-res.o'
& "$CompilerRoot\bin\windres.exe" "-I$BuildRoot" (Join-Path $BuildRoot 'steeltally.rc') -O coff -o $iconResource
if($LASTEXITCODE){throw 'Windows icon/version resource compilation failed'}
$files=Get-ChildItem -LiteralPath $src -Filter '*.cpp' -File | Select-Object -ExpandProperty FullName
& "$CompilerRoot\bin\g++.exe" -std=c++17 -O2 -mwindows -DNDEBUG -DUNICODE -D_UNICODE @include @files @moc $resources $iconResource "-L$QtRoot\lib" -lQt6Widgets -lQt6Svg -lQt6PrintSupport -lQt6Gui -lQt6Network -lQt6Core -lbcrypt -o "$out\gjg_table_qty.exe"
if($LASTEXITCODE){throw 'Native compile failed'}
$publish=Join-Path $PSScriptRoot 'build'
if (Test-Path -LiteralPath $publish) {
    $backup=Join-Path $PSScriptRoot ('旧版可删\build-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
    Move-Item -LiteralPath $publish -Destination $backup
}
New-Item -ItemType Directory -Path $publish | Out-Null
Copy-Item -LiteralPath "$out\gjg_table_qty.exe" -Destination $publish
Copy-Item -LiteralPath (Join-Path $BuildRoot 'resources.qrc') -Destination $publish
Write-Output "$publish\gjg_table_qty.exe"
Write-Output "Build staging retained: $BuildRoot"
