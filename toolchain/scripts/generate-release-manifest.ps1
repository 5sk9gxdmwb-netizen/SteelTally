#!/usr/bin/env pwsh
# Generate signed update manifest for SteelTally GitHub Releases
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$DownloadUrl,
    [Parameter(Mandatory=$true)][string]$PrivateKeyPath,
    [Parameter(Mandatory=$true)][string]$OutputManifest,
    [string]$ReleaseNotes = "更新内容见 GitHub Release",
    [string]$CatalogVersion = "1.0.3",
    [string]$RulesVersion = "1.0.3",
    [string]$ProjectSchema = "1"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $ExePath)) {
    throw "EXE not found: $ExePath"
}

# 验证版本格式：三段非负整数，0-999999，无前导零
if ($Version -notmatch '^(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})$') {
    throw "Version must be three decimal segments, each 0-999999: $Version"
}

# 验证 URL
if ($DownloadUrl -notmatch '^https://[^/]+/.+\.exe$') {
    throw "Download URL must be HTTPS and end with .exe"
}

if ($DownloadUrl.Length -gt 2048) {
    throw "Download URL exceeds 2048 characters"
}

# 读取私钥（.NET RSA XML 格式或 PEM）
if (-not (Test-Path $PrivateKeyPath)) {
    throw "Private key not found: $PrivateKeyPath"
}

$keyContent = [IO.File]::ReadAllText($PrivateKeyPath)
$rsa = [Security.Cryptography.RSA]::Create()

try {
    if ($keyContent -match '<RSAKeyValue>') {
        # XML 格式
        $rsa.FromXmlString($keyContent)
    } elseif ($keyContent -match '-----BEGIN (RSA )?PRIVATE KEY-----') {
        # PEM 格式
        $rsa.ImportFromPem($keyContent)
    } else {
        throw "Private key must be RSA XML or PEM format"
    }
} catch {
    throw "Failed to load private key: $_"
}

$keySize = $rsa.KeySize
if ($keySize -lt 2048 -or $keySize -gt 4096) {
    throw "RSA key size must be 2048-4096 bits, got $keySize"
}

Write-Host "Private key loaded: $keySize-bit RSA"

# 计算 EXE 大小与 SHA256
$exeBytes = [IO.File]::ReadAllBytes($ExePath)
$exeSize = $exeBytes.Length
if ($exeSize -lt 1 -or $exeSize -gt 536870912) {
    throw "EXE size must be 1 to 536870912 bytes, got $exeSize"
}

$sha256 = [Security.Cryptography.SHA256]::Create()
$hash = $sha256.ComputeHash($exeBytes)
$hashHex = [BitConverter]::ToString($hash).Replace('-', '').ToLower()
$sha256.Dispose()

Write-Host "EXE: $exeSize bytes, SHA256: $hashHex"

# 构建 payload JSON（精确字节，不得修改）
$payloadObj = [ordered]@{
    schema = "steeltally.update/1"
    version = $Version
    url = $DownloadUrl
    size = $exeSize
    sha256 = $hashHex
    notes = $ReleaseNotes
    catalog_version = $CatalogVersion
    rules_version = $RulesVersion
    project_schema = $ProjectSchema
}

# 序列化为紧凑 JSON（无 BOM UTF-8）
$payloadJson = $payloadObj | ConvertTo-Json -Compress -Depth 1
$payloadUtf8 = [Text.Encoding]::UTF8.GetBytes($payloadJson)

if ($payloadUtf8.Length -gt 32768) {
    throw "Payload exceeds 32 KiB"
}

Write-Host "Payload: $($payloadUtf8.Length) bytes"

# RSA PKCS#1 SHA-256 签名
$signature = $rsa.SignData(
    $payloadUtf8,
    [Security.Cryptography.HashAlgorithmName]::SHA256,
    [Security.Cryptography.RSASignaturePadding]::Pkcs1
)

$rsa.Dispose()

Write-Host "Signature: $($signature.Length) bytes"

# 构建外层 JSON（payload 和 signature 均 Base64）
$envelope = @{
    payload = [Convert]::ToBase64String($payloadUtf8)
    signature = [Convert]::ToBase64String($signature)
} | ConvertTo-Json -Compress

if ([Text.Encoding]::UTF8.GetByteCount($envelope) -gt 98304) {
    throw "Envelope exceeds 96 KiB"
}

# 写入清单（无 BOM UTF-8）
[IO.File]::WriteAllText($OutputManifest, $envelope, (New-Object Text.UTF8Encoding($false)))

Write-Host "Manifest written: $OutputManifest"
Write-Host "Upload the EXE to: $DownloadUrl"
Write-Host "Serve this manifest at your configured update URL"
