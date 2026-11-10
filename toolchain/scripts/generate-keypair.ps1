#!/usr/bin/env pwsh
# Generate RSA keypair for SteelTally update signing
param(
    [ValidateSet(2048, 3072, 4096)][int]$KeySize = 2048,
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$privateKeyPath = Join-Path $OutputDir 'update-private-key.xml'
$publicKeyPath  = Join-Path $OutputDir 'update-public-key.b64'

if (Test-Path $privateKeyPath) {
    throw "Private key already exists: $privateKeyPath (delete manually to regenerate)"
}

Write-Host "Generating $KeySize-bit RSA keypair..."

$utf8 = New-Object System.Text.UTF8Encoding($false)
$rsa  = [Security.Cryptography.RSA]::Create($KeySize)

try {
    # 私钥：XML 含全部参数，仅用于本地签名
    $privateXml = $rsa.ToXmlString($true)
    if ([string]::IsNullOrWhiteSpace($privateXml)) { throw 'RSA key export returned empty XML' }
    [IO.File]::WriteAllText($privateKeyPath, $privateXml, $utf8)

    # 公钥：Windows BCrypt BCRYPT_RSAPUBLIC_BLOB
    $p = $rsa.ExportParameters($false)
    $blob = New-Object System.Collections.Generic.List[byte]
    $blob.AddRange([BitConverter]::GetBytes([uint32]0x31415352))        # Magic 'RSA1'
    $blob.AddRange([BitConverter]::GetBytes([uint32]$KeySize))          # BitLength
    $blob.AddRange([BitConverter]::GetBytes([uint32]$p.Exponent.Length)) # cbPublicExp
    $blob.AddRange([BitConverter]::GetBytes([uint32]$p.Modulus.Length))  # cbModulus
    $blob.AddRange([BitConverter]::GetBytes([uint32]0))                  # cbPrime1
    $blob.AddRange([BitConverter]::GetBytes([uint32]0))                  # cbPrime2
    $blob.AddRange([byte[]]$p.Exponent)
    $blob.AddRange([byte[]]$p.Modulus)

    [IO.File]::WriteAllText($publicKeyPath, [Convert]::ToBase64String($blob.ToArray()), $utf8)
}
finally {
    $rsa.Dispose()
}

# 写入后校验，避免静默产出空文件
$privBytes = (New-Object IO.FileInfo($privateKeyPath)).Length
$pubBytes  = (New-Object IO.FileInfo($publicKeyPath)).Length
if ($privBytes -lt 500) { throw "Private key file looks invalid: $privBytes bytes at $privateKeyPath" }
if ($pubBytes  -lt 100) { throw "Public key file looks invalid: $pubBytes bytes at $publicKeyPath" }

Write-Host "Private key (XML): $privateKeyPath ($privBytes bytes)"
Write-Host "  ** KEEP THIS SECRET ** Use only for signing, never distribute."
Write-Host "Public key (Base64 BCrypt blob): $publicKeyPath ($pubBytes bytes)"
Write-Host "  Use this with -UpdatePublicKeyFile when building the client."
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Build client with: -UpdateUrl 'https://...' -UpdatePublicKeyFile '$publicKeyPath'"
Write-Host "  2. Sign releases with: .\generate-release-manifest.ps1 -PrivateKeyPath '$privateKeyPath' ..."
