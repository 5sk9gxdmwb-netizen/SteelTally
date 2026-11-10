param([Parameter(Mandatory=$true)][string]$BuildRoot,
 [Parameter(Mandatory=$true)][string]$CMake,
 [string]$CompilerRoot='C:\Qt\Tools\mingw1310_64',
 [string]$OutputRoot='')
$ErrorActionPreference='Stop'
if(-not $OutputRoot){$OutputRoot=Join-Path $PSScriptRoot ('..\verification\updater-transactions-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$OutputRoot=(Resolve-Path -LiteralPath $OutputRoot).Path
$env:PATH="$CompilerRoot\bin;$(Split-Path $CMake);$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM='offscreen'
$utf8=New-Object Text.UTF8Encoding($false)
$rsa=[Security.Cryptography.RSA]::Create(2048)
$owned=@()
try {
 $p=$rsa.ExportParameters($false);$stream=New-Object IO.MemoryStream;$writer=New-Object IO.BinaryWriter($stream)
 foreach($n in @(0x31415352,2048,$p.Exponent.Length,$p.Modulus.Length,0,0)){$writer.Write([uint32]$n)}
 $writer.Write([byte[]]$p.Exponent);$writer.Write([byte[]]$p.Modulus);$writer.Flush()
 $key=[Convert]::ToBase64String($stream.ToArray())
 [IO.File]::WriteAllText((Join-Path $OutputRoot 'public-key.b64'),$key,$utf8)
 # Reconfigure an isolated copied application staging, never the published default-unconfigured build.
 & $CMake -S (Join-Path $BuildRoot 'table_qty') -B (Join-Path $BuildRoot 'build') "-DSTEELTALLY_UPDATE_URL=https://updates.example.invalid/manifest.json" "-DSTEELTALLY_UPDATE_PUBLIC_KEY=$key"
 if($LASTEXITCODE){throw 'Fixture configure failed'}
 & $CMake --build (Join-Path $BuildRoot 'build') --parallel 6
 if($LASTEXITCODE){throw 'Fixture build failed'}
 $testExe=Join-Path $BuildRoot 'build\gjg_table_qty.exe'
 $source=@'
#include <windows.h>
#include <shellapi.h>
#include <string>
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){
 int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
#ifdef FAIL_HEALTH
 return 9;
#else
 for(int i=1;i+2<argc;++i) if(std::wstring(argv[i])==L"--update-health"){
  std::wstring path=std::wstring(argv[i+1])+L"\\health";
  std::wstring nonce=argv[i+2];std::string text(nonce.begin(),nonce.end());
  HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return 8;DWORD written=0;WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr);CloseHandle(file);
 }
 Sleep(12000);return 0;
#endif
}
'@
 $cpp=Join-Path $OutputRoot 'health-fixture.cpp';[IO.File]::WriteAllText($cpp,$source,$utf8)
 $good=Join-Path $OutputRoot 'healthy.exe';$bad=Join-Path $OutputRoot 'unhealthy.exe'
 & "$CompilerRoot\bin\g++.exe" -std=c++17 -static -mwindows $cpp -lshell32 -o $good
 if($LASTEXITCODE){throw 'Healthy fixture compilation failed'}
 & "$CompilerRoot\bin\g++.exe" -std=c++17 -static -mwindows -DFAIL_HEALTH $cpp -lshell32 -o $bad
 if($LASTEXITCODE){throw 'Unhealthy fixture compilation failed'}
 $results=@()
 foreach($mode in @('success','rollback','tampered','invalid-path','invalid-args')){
  $dir=Join-Path $OutputRoot $mode;New-Item -ItemType Directory -Path $dir | Out-Null
  $target=Join-Path $dir 'app.exe';Copy-Item -LiteralPath $testExe -Destination $target
  $oldHash=(Get-FileHash -LiteralPath $target).Hash
  if($mode -in @('invalid-path','invalid-args')){
   $arguments=if($mode -eq 'invalid-path'){@('--update-helper',('"'+$dir+'"'))}else{@('--update-helper',('"'+$dir+'"'),'extra')}
   $h=Start-Process -FilePath $target -ArgumentList $arguments -PassThru;$owned+=,$h;$null=$h.Handle
   if(-not $h.WaitForExit(30000)){throw 'Invalid helper did not exit'};$h.WaitForExit()
   $expected=if($mode -eq 'invalid-path'){21}else{20}
   if($h.ExitCode -ne $expected -or (Get-FileHash $target).Hash -ne $oldHash){throw "Unsafe rejection: $mode"}
   $results+=@{mode=$mode;ok=$true;exit_code=$h.ExitCode};continue
  }
  $stage=Join-Path ([IO.Path]::GetTempPath()) ('SteelTally-update-'+[guid]::NewGuid().ToString())
  New-Item -ItemType Directory -Path $stage | Out-Null
  $nonce=[guid]::NewGuid().ToString()
  $candidate=if($mode -eq 'rollback'){$bad}else{$good}
  $download=Join-Path $stage 'download.exe';Copy-Item -LiteralPath $candidate -Destination $download
  $payload=[ordered]@{schema='steeltally.update/1';version='1.0.4';url='https://updates.example.invalid/app.exe';size=(Get-Item $download).Length;sha256=(Get-FileHash $download).Hash.ToLower();notes='Local transaction test';catalog_version='1';rules_version='1';project_schema='1'}
  $raw=[Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Compress))
  $sig=$rsa.SignData($raw,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
  $envelope=@{payload=[Convert]::ToBase64String($raw);signature=[Convert]::ToBase64String($sig)}
  [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'),($envelope | ConvertTo-Json -Compress),$utf8)
  if($mode -eq 'tampered'){[IO.File]::AppendAllText($download,'tampered')}
  $parent=Start-Process -FilePath $target -ArgumentList @('--data-root',('"'+(Join-Path $dir 'user-data')+'"')) -PassThru
  $owned+=,$parent;$null=$parent.Handle
  $ticket=@{target=$target;pid=[string]$parent.Id;creation=[string]$parent.StartTime.ToUniversalTime().ToFileTimeUtc();nonce=$nonce;arguments=@('--data-root',(Join-Path $dir 'user-data'))}
  [IO.File]::WriteAllText((Join-Path $stage 'transaction.json'),($ticket | ConvertTo-Json -Compress),$utf8)
  $helper=Join-Path $stage 'helper.exe';Copy-Item -LiteralPath $target -Destination $helper
  $h=Start-Process -FilePath $helper -ArgumentList @('--update-helper',('"'+$stage+'"')) -PassThru;$owned+=,$h;$null=$h.Handle
  if($mode -eq 'tampered'){
   if(-not $h.WaitForExit(30000)){throw 'Tampered helper timeout'};$h.WaitForExit()
   if($h.ExitCode -ne 22 -or $parent.HasExited -or (Get-FileHash $target).Hash -ne $oldHash){throw 'Tampered candidate was not safely rejected'}
   $parent.Kill();$parent.WaitForExit()
  }else{
   $limit=[DateTime]::UtcNow.AddSeconds(30)
   while(-not (Test-Path -LiteralPath (Join-Path $stage 'ready')) -and -not $h.HasExited -and [DateTime]::UtcNow -lt $limit){Start-Sleep -Milliseconds 100}
   if(-not (Test-Path -LiteralPath (Join-Path $stage 'ready'))){throw "Helper readiness failed: $stage"}
   # Simulates the application's user-approved exit, only after readiness is observed.
   $parent.Kill();$parent.WaitForExit()
   if(-not $h.WaitForExit(65000)){throw 'Transaction timeout'};$h.WaitForExit()
   if($mode -eq 'success'){
    if($h.ExitCode -ne 0 -or (Get-FileHash $target).Hash -ne (Get-FileHash $good).Hash){throw 'Successful replacement mismatch'}
   }else{
    if($h.ExitCode -ne 25 -or (Get-FileHash $target).Hash -ne $oldHash){throw 'Rollback mismatch'}
   }
  }
  # Stop only processes whose canonical executable is in this test's unique directory.
  Get-Process | Where-Object {try {$_.Path -eq $target}catch{$false}} | Stop-Process
  Copy-Item -LiteralPath (Join-Path $stage 'update.log') -Destination $dir
  if(Test-Path (Join-Path $stage 'result.json')){Copy-Item -LiteralPath (Join-Path $stage 'result.json') -Destination $dir}
  $results+=@{mode=$mode;ok=$true;exit_code=$h.ExitCode;stage=$stage;target=$target}
 }
 @{ok=$true;tests=$results;scope='Local synthetic transactions; no network or real project; ephemeral private key never saved'} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'result.json') -Encoding UTF8
 Write-Output "Transaction evidence: $OutputRoot"
}finally{
 $rsa.Dispose()
 foreach($p in $owned){try{if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}catch{}}
}
