param([string]$Platform='offscreen')
$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path $PSScriptRoot) ('verification\ui-v1.1-'+$Platform+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM=$Platform
foreach($name in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH')){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
$exe=Join-Path (Split-Path $PSScriptRoot) 'release-static\gjg_table_qty.exe'
$results=@()
foreach($mode in @('ui','section','full','update')){
 $dir=Join-Path $root $mode;New-Item -ItemType Directory -Path $dir|Out-Null
 $flag=switch($mode){'ui'{'--ui-selftest'} 'section'{'--section-selftest'} 'full'{'--selftest'} 'update'{'--update-selftest'}}
 $arguments=if($mode -eq 'update'){@($flag,(Join-Path $dir 'updater-result.json'))}else{@($flag,'--selftest-dir',$dir,'--data-root',(Join-Path $dir 'user-data'))}
 $quotedArguments=@($arguments | ForEach-Object { '"'+$_+'"' })
 $p=Start-Process -FilePath $exe -ArgumentList $quotedArguments -WorkingDirectory $dir -RedirectStandardOutput (Join-Path $dir 'stdout.log') -RedirectStandardError (Join-Path $dir 'stderr.log') -PassThru
 $handle=$p.Handle
 if(-not $p.WaitForExit(120000)){$p.Kill();throw "Timeout: $mode"}
 $p.WaitForExit();$results+=@{mode=$mode;exit_code=$p.ExitCode;directory=$dir};Write-Output "$mode exit=$($p.ExitCode)"
}
$ok=@($results|Where-Object{$_.exit_code -ne 0}).Count -eq 0
@{ok=$ok;platform=$Platform;version='1.1.0';runs=$results;sha256=(Get-FileHash $exe).Hash}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $root 'result.json') -Encoding UTF8
Write-Output "Evidence: $root"
if(-not $ok){throw 'UI release validation failed'}
