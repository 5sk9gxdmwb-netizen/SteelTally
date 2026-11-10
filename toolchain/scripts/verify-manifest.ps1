#!/usr/bin/env pwsh
# Verify signed manifest and demonstrate acceptance/rejection
param(
    [Parameter(Mandatory=$true)][string]$ManifestPath,
    [Parameter(Mandatory=$true)][string]$PublicKeyPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $ManifestPath)) {
    throw "Manifest not found: $ManifestPath"
}

if (-not (Test-Path $PublicKeyPath)) {
    throw "Public key not found: $PublicKeyPath"
}

# 读取清单
$envelopeJson = [IO.File]::ReadAllText($ManifestPath, [Text.UTF8Encoding]($false))
$envelope = $envelopeJson | ConvertFrom-Json

if (-not $envelope.payload -or -not $envelope.signature) {
    throw "Manifest must contain 'payload' and 'signature' fields"
}

# 解码 payload 和 signature
$payloadBytes = [Convert]::FromBase64String($envelope.payload)
$signatureBytes = [Convert]::FromBase64String($envelope.signature)

Write-Host "Payload: $($payloadBytes.Length) bytes"
Write-Host "Signature: $($signatureBytes.Length) bytes"

# 读取公钥 blob
$publicKeyB64 = [IO.File]::ReadAllText($PublicKeyPath).Trim()
$blob = [Convert]::FromBase64String($publicKeyB64)

# 按固定偏移解析 BCrypt blob（避免 BinaryReader 重载歧义）
if ($blob.Length -lt 24) { throw "Public key blob too short: $($blob.Length) bytes" }
$magic      = [BitConverter]::ToUInt32($blob, 0)
$bitLength  = [BitConverter]::ToUInt32($blob, 4)
$cbPublicExp= [BitConverter]::ToUInt32($blob, 8)
$cbModulus  = [BitConverter]::ToUInt32($blob, 12)

if ($magic -ne 0x31415352) {
    throw "Invalid public key magic: expected 0x31415352, got 0x$($magic.ToString('X8'))"
}
if (($blob.Length - 24) -ne ($cbPublicExp + $cbModulus)) {
    throw "Public key blob length mismatch: header says $($cbPublicExp + $cbModulus) payload bytes, file has $($blob.Length - 24)"
}
if (($bitLength / 8) -ne $cbModulus) {
    throw "RSA modulus length inconsistent: $bitLength-bit key needs $($bitLength / 8) bytes, got $cbModulus"
}

$exponent = New-Object byte[] $cbPublicExp
$modulus  = New-Object byte[] $cbModulus
[Array]::Copy($blob, 24, $exponent, 0, $cbPublicExp)
[Array]::Copy($blob, 24 + $cbPublicExp, $modulus, 0, $cbModulus)

Write-Host "Public key: $bitLength-bit RSA"

# 导入到 .NET RSA
$rsa = [Security.Cryptography.RSA]::Create()
$params = New-Object Security.Cryptography.RSAParameters
$params.Exponent = $exponent
$params.Modulus = $modulus
$rsa.ImportParameters($params)

# 验证签名
$valid = $rsa.VerifyData(
    $payloadBytes,
    $signatureBytes,
    [Security.Cryptography.HashAlgorithmName]::SHA256,
    [Security.Cryptography.RSASignaturePadding]::Pkcs1
)

$rsa.Dispose()

if ($valid) {
    Write-Host "✓ Signature valid" -ForegroundColor Green

    # 解析 payload
    $payloadJson = [Text.Encoding]::UTF8.GetString($payloadBytes)
    $payload = $payloadJson | ConvertFrom-Json

    Write-Host ""
    Write-Host "Manifest contents:"
    Write-Host "  Version: $($payload.version)"
    Write-Host "  URL: $($payload.url)"
    Write-Host "  Size: $($payload.size) bytes"
    Write-Host "  SHA256: $($payload.sha256)"
    Write-Host "  Notes: $($payload.notes)"
    Write-Host "  Catalog: $($payload.catalog_version)"
    Write-Host "  Rules: $($payload.rules_version)"
    Write-Host "  Project schema: $($payload.project_schema)"
} else {
    Write-Host "✗ Signature INVALID" -ForegroundColor Red
    throw "Signature verification failed"
}
