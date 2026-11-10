param([Parameter(Mandatory=$true)][string]$Exe,
      [string]$OutputRoot='')
$ErrorActionPreference='Stop'
if(-not $OutputRoot){$OutputRoot=Join-Path $PSScriptRoot ('..\verification\updater-signatures-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$Exe=(Resolve-Path -LiteralPath $Exe).Path
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$OutputRoot=(Resolve-Path -LiteralPath $OutputRoot).Path
$env:QT_QPA_PLATFORM='offscreen'
function Run-Native([string[]]$Arguments,[int]$Expected){
 $p=Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
 $handle=$p.Handle
 if(-not $p.WaitForExit(30000)){$p.Kill();throw 'Native verifier timeout'}
 $p.WaitForExit()
 if($p.ExitCode -ne $Expected){throw "Expected exit $Expected, got $($p.ExitCode)"}
}
Run-Native @('--update-selftest',('"'+(Join-Path $OutputRoot 'cng')+'"')) 0
$cng=Get-Content -Raw -LiteralPath (Join-Path $OutputRoot 'cng\result.json') | ConvertFrom-Json
if(-not $cng.ok){throw 'CNG selftest failed'}
# Independent .NET/Windows RSA implementation. Private key stays in memory and is disposed.
$rsa=[Security.Cryptography.RSA]::Create(2048)
try {
 $public=$rsa.ExportParameters($false)
 $blob=New-Object IO.MemoryStream
 $writer=New-Object IO.BinaryWriter($blob)
 foreach($n in @(0x31415352,2048,$public.Exponent.Length,$public.Modulus.Length,0,0)){$writer.Write([uint32]$n)}
 $writer.Write([byte[]]$public.Exponent);$writer.Write([byte[]]$public.Modulus);$writer.Flush()
 $keyFile=Join-Path $OutputRoot 'public-key.b64'
 [IO.File]::WriteAllText($keyFile,[Convert]::ToBase64String($blob.ToArray()),(New-Object Text.UTF8Encoding($false)))
 $payload=[ordered]@{schema='steeltally.update/1';version='1.0.4';url='https://updates.example.invalid/gjg_table_qty.exe';size=12345;sha256=('a'*64);notes='PowerShell synthetic signature';catalog_version='1.0.3';rules_version='1.0.3';project_schema='1'}
 $raw=[Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Compress))
 $sig=$rsa.SignData($raw,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
 $envelope=@{payload=[Convert]::ToBase64String($raw);signature=[Convert]::ToBase64String($sig)}
 $manifest=Join-Path $OutputRoot 'manifest.json'
 [IO.File]::WriteAllText($manifest,($envelope | ConvertTo-Json -Compress),(New-Object Text.UTF8Encoding($false)))
 $result=Join-Path $OutputRoot 'valid.json'
 Run-Native @('--update-verify-test',('"'+$manifest+'"'),('"'+$keyFile+'"'),('"'+$result+'"')) 0
 if(-not (Get-Content -Raw -LiteralPath $result | ConvertFrom-Json).ok){throw 'Independent signature rejected'}
 $sig[5]=$sig[5] -bxor 1
 $envelope.signature=[Convert]::ToBase64String($sig)
 $bad=Join-Path $OutputRoot 'tampered.json'
 [IO.File]::WriteAllText($bad,($envelope | ConvertTo-Json -Compress),(New-Object Text.UTF8Encoding($false)))
 Run-Native @('--update-verify-test',('"'+$bad+'"'),('"'+$keyFile+'"'),('"'+(Join-Path $OutputRoot 'rejected.json')+'"')) 1
 @{ok=$true;cng_checks=@($cng.checks).Count;independent_signature=$true;tamper_rejected=$true;private_key_saved=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'result.json') -Encoding UTF8
 Write-Output "Signature evidence: $OutputRoot"
} finally {$rsa.Dispose()}
