param([string]$QtRoot='C:\Qt\6.8.3\mingw_64',[string]$CompilerRoot='C:\Qt\Tools\mingw1310_64')
$ErrorActionPreference='Stop'
$env:PATH="$CompilerRoot\bin;$QtRoot\bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_PLUGIN_PATH="$QtRoot\plugins"
$env:QT_QPA_PLATFORM_PLUGIN_PATH="$QtRoot\plugins\platforms"
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$exe=Join-Path $PSScriptRoot 'build\gjg_table_qty.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Run build-native.ps1 first'}
Start-Process -FilePath $exe -WorkingDirectory $PSScriptRoot
