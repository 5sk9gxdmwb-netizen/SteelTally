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

# 解析 BCrypt blob
$ms = New-Object IO.MemoryStream($blob)
$br = New-Object IO.BinaryReader($ms)

$magic = $br.ReadUInt32()
$bitLength = $br.ReadUInt32()
$cbPublicExp = $br.ReadUInt32()
$cbModulus = $br.ReadUInt32()
$cbPrime1 = $br.ReadUInt32()
$cbPrime2 = $br.ReadUInt32()

if ($magic -ne 0x31415352) {
    throw "Invalid public key magic: expected 0x31415352, got 0x$($magic.ToString('X8'))"
}

$exponent = $br.ReadBytes($cbPublicExp)
$modulus = $br.ReadBytes($cbModulus)

$br.Dispose()
$ms.Dispose()

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
