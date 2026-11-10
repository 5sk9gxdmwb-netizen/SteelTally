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

$privateKeyPath = Join-Path $OutputDir "update-private-key.xml"
$publicKeyPath = Join-Path $OutputDir "update-public-key.b64"

if (Test-Path $privateKeyPath) {
    throw "Private key already exists: $privateKeyPath (delete manually to regenerate)"
}

Write-Host "Generating $KeySize-bit RSA keypair..."

$rsa = [Security.Cryptography.RSA]::Create($KeySize)

# 导出私钥（XML 格式，含私钥参数）
$privateXml = $rsa.ToXmlString($true)
[IO.File]::WriteAllText($privateKeyPath, $privateXml, (New-Object Text.UTF8Encoding($false)))

Write-Host "Private key (XML): $privateKeyPath"
Write-Host "  ** KEEP THIS SECRET ** Use only for signing, never distribute."

# 导出公钥为 Windows BCrypt BCRYPT_RSAPUBLIC_BLOB
$params = $rsa.ExportParameters($false)

# 构建 blob：6个 uint32 + 指数 + 模数（均 little-endian 结构，但指数和模数本身是 big-endian 字节）
$magic = [uint32]0x31415352  # 'RSA1'
$bitLength = [uint32]$KeySize
$cbPublicExp = [uint32]$params.Exponent.Length
$cbModulus = [uint32]$params.Modulus.Length
$cbPrime1 = [uint32]0
$cbPrime2 = [uint32]0

$ms = New-Object IO.MemoryStream
$bw = New-Object IO.BinaryWriter($ms)
$bw.Write($magic)
$bw.Write($bitLength)
$bw.Write($cbPublicExp)
$bw.Write($cbModulus)
$bw.Write($cbPrime1)
$bw.Write($cbPrime2)
$bw.Write($params.Exponent)
$bw.Write($params.Modulus)
$bw.Flush()

$blob = $ms.ToArray()
$bw.Dispose()
$ms.Dispose()

$publicKeyB64 = [Convert]::ToBase64String($blob)
[IO.File]::WriteAllText($publicKeyPath, $publicKeyB64, (New-Object Text.UTF8Encoding($false)))

$rsa.Dispose()

Write-Host "Public key (Base64 BCrypt blob): $publicKeyPath"
Write-Host "  Use this with -UpdatePublicKeyFile when building the client."
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Build client with: -UpdateUrl 'https://...' -UpdatePublicKeyFile '$publicKeyPath'"
Write-Host "  2. Sign releases with: .\generate-release-manifest.ps1 -PrivateKeyPath '$privateKeyPath' ..."
