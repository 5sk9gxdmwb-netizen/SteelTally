param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$compiler='C:\Qt\Tools\mingw1310_64'
$exePath=(Resolve-Path -LiteralPath $Exe).Path
$root=Join-Path $PSScriptRoot ('verification\static-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$runtime=Join-Path $root 'runtime'
New-Item -ItemType Directory -Path $runtime | Out-Null
Copy-Item -LiteralPath $exePath -Destination $runtime
$native=Join-Path $runtime 'gjg_table_qty.exe'
$imports=& "$compiler\bin\objdump.exe" -p $native
if($LASTEXITCODE){throw 'Cannot inspect PE imports'}
$imports | Set-Content (Join-Path $root 'pe-imports.txt')
$dependencies=@($imports | Select-String 'DLL Name:' | ForEach-Object {$_.Line.Split(':',2)[1].Trim()})
if($dependencies | Where-Object {$_ -match '^(Qt6|libgcc|libstdc|libwinpthread)'}){throw 'Non-system runtime dependency found'}
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM='offscreen'
foreach($name in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH')) {Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
$runs=@()
foreach($mode in @('calculation','interaction','windows')) {
 $env:QT_QPA_PLATFORM=if($mode -eq 'windows'){'windows'}else{'offscreen'}
 $dir=Join-Path $root $mode
 New-Item -ItemType Directory -Path $dir | Out-Null
 $flag=if($mode -eq 'calculation'){'--section-selftest'}else{'--selftest'}
 $arguments=@($flag,'--selftest-dir',('"'+$dir+'"'),'--data-root',('"'+(Join-Path $dir 'user-data')+'"'))
 $process=Start-Process -FilePath $native -ArgumentList $arguments -WorkingDirectory $dir -RedirectStandardOutput (Join-Path $dir 'stdout.log') -RedirectStandardError (Join-Path $dir 'stderr.log') -PassThru
 $handle=$process.Handle
 if(-not $process.WaitForExit(120000)){$process.Kill();throw 'Static selftest timeout'}
 $process.WaitForExit()
 $code=$process.ExitCode
 if($null -eq $code){throw 'Missing native exit code'}
 $runs+=@{mode=$mode;exit_code=$code;directory=$dir}
 Write-Output "$mode exit=$code"
}
$ok=($runs | Where-Object {$_.exit_code -ne 0}).Count -eq 0
@{ok=$ok;runs=$runs;dependencies=$dependencies;sha256=(Get-FileHash $native).Hash;scope='Local single-EXE isolation, not a separate Windows machine'} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $root 'result.json') -Encoding UTF8
Write-Output "Evidence: $root"
if(-not $ok){throw 'Static validation failed'}
