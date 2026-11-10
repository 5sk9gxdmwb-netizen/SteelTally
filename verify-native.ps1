param([switch]$Isolated)
$ErrorActionPreference='Stop'
$qt='C:\Qt\6.8.3\mingw_64'
$compiler='C:\Qt\Tools\mingw1310_64'
$env:PATH="$compiler\bin;$qt\bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_PLUGIN_PATH="$qt\plugins"
$env:QT_QPA_PLATFORM_PLUGIN_PATH="$qt\plugins\platforms"
Remove-Item Env:QML2_IMPORT_PATH -ErrorAction SilentlyContinue
$exe=Join-Path $PSScriptRoot 'build\gjg_table_qty.exe'
$root=Join-Path $PSScriptRoot ('verification\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
if($Isolated) {
    $runtime=Join-Path $root 'isolated-runtime'
    New-Item -ItemType Directory -Path $runtime | Out-Null
    Copy-Item -LiteralPath $exe -Destination $runtime
    & "$qt\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $runtime 'gjg_table_qty.exe')
    if($LASTEXITCODE){throw 'Isolated runtime deployment failed'}
    # windeployqt only deploys qwindows; these checks explicitly use offscreen.
    Copy-Item -LiteralPath "$qt\plugins\platforms\qoffscreen.dll" -Destination (Join-Path $runtime 'platforms')
    $exe=Join-Path $runtime 'gjg_table_qty.exe'
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH=$runtime
    $env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $runtime 'platforms'
    if(Test-Path (Join-Path $runtime 'assets')) {throw 'Isolated test must not contain external assets'}
}
$results=@()
foreach($mode in @('calculation','interaction')) {
    $dir=Join-Path $root $mode
    New-Item -ItemType Directory -Path $dir | Out-Null
    $flag=if($mode -eq 'calculation'){'--section-selftest'}else{'--selftest'}
    $argsList=@($flag,'--selftest-dir',('"'+$dir+'"'),'--data-root',('"'+(Join-Path $dir 'user-data')+'"'))
    $process=Start-Process -FilePath $exe -ArgumentList $argsList -WorkingDirectory $dir -RedirectStandardOutput (Join-Path $dir 'stdout.log') -RedirectStandardError (Join-Path $dir 'stderr.log') -PassThru
    # Cache the native handle before waiting so Windows PowerShell retains ExitCode.
    $nativeHandle=$process.Handle
    if(-not $process.WaitForExit(120000)) { $process.Kill(); throw "Selftest timeout: $mode" }
    $process.WaitForExit()
    $code=$process.ExitCode
    if($null -eq $code) { throw "Missing native exit code: $mode" }
    $log=[IO.File]::ReadAllText((Join-Path $dir 'stdout.log'))
    $results+=@{mode=$mode;exit_code=$code;directory=$dir;log=$log}
    Write-Output "$mode exit=$code"
    Write-Output $log
}
$ok=($results | Where-Object { $_.exit_code -ne 0 }).Count -eq 0
@{ok=$ok;runs=$results;exe=$exe;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'result.json') -Encoding UTF8
Write-Output "Evidence: $root"
if(-not $ok){throw 'Native verification failed'}
