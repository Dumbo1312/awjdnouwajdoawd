param(
    [switch]$Decrypt
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$EmbeddedDir = Join-Path $ProjectDir "Embedded"
$EncryptedDir = Join-Path $EmbeddedDir "encrypted"
$KeyFile = Join-Path (Join-Path $ProjectDir "Tools") "resource_key.bin"
$HeaderFile = Join-Path (Join-Path $ProjectDir "Utilities") "ResourceProtection.h"

$ResourceList = @(
    "DirectML.dll",
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "graphics-hook64.dll",
    "inject-helper64.exe",
    "fortnite.mdl",
    "node2.json",
    "makcu-cpp.dll",
    "svchost.exe"
)

$ResIds = @{
    "DirectML.dll" = 100
    "onnxruntime.dll" = 101
    "onnxruntime_providers_shared.dll" = 102
    "graphics-hook64.dll" = 103
    "inject-helper64.exe" = 104
    "fortnite.mdl" = 105
    "node2.json" = 106
    "makcu-cpp.dll" = 108
    "svchost.exe" = 107
}

function New-MasterKey {
    $key = [byte[]]::new(32)
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($key)
    $rng.Dispose()
    [IO.File]::WriteAllBytes($KeyFile, $key)
    Write-Host "[+] Master key: $([System.BitConverter]::ToString($key[0..7]) -replace '-','')..." -ForegroundColor Green
    return $key
}

function Load-MasterKey {
    if (Test-Path $KeyFile) {
        return [IO.File]::ReadAllBytes($KeyFile)
    }
    return New-MasterKey
}

function Encrypt-Resource {
    param([byte[]]$Data, [byte[]]$Key)
    
    $aes = [Security.Cryptography.Aes]::Create()
    $aes.KeySize = 256
    $aes.BlockSize = 128
    $aes.Mode = [Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [Security.Cryptography.PaddingMode]::PKCS7
    $aes.GenerateIV()
    
    $encryptor = $aes.CreateEncryptor($Key, $aes.IV)
    $ms = New-Object IO.MemoryStream
    $cs = New-Object Security.Cryptography.CryptoStream($ms, $encryptor, [Security.Cryptography.CryptoStreamMode]::Write)
    $cs.Write($Data, 0, $Data.Length)
    $cs.FlushFinalBlock()
    $ciphertext = $ms.ToArray()
    $cs.Dispose(); $ms.Dispose()
    
    $iv = $aes.IV
    $aes.Dispose()
    
    $result = [byte[]]::new($iv.Length + $ciphertext.Length)
    $iv.CopyTo($result, 0)
    $ciphertext.CopyTo($result, $iv.Length)
    
    return $result
}

function Decrypt-Resource {
    param([byte[]]$Data, [byte[]]$Key)
    
    if ($Data.Length -lt 17) { throw "Data too short" }
    
    $iv = $Data[0..15]
    $ciphertext = $Data[16..($Data.Length - 1)]
    
    $aes = [Security.Cryptography.Aes]::Create()
    $aes.KeySize = 256
    $aes.BlockSize = 128
    $aes.Mode = [Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [Security.Cryptography.PaddingMode]::PKCS7
    
    $decryptor = $aes.CreateDecryptor($Key, $iv)
    $ms = New-Object IO.MemoryStream($ciphertext)
    $cs = New-Object Security.Cryptography.CryptoStream($ms, $decryptor, [Security.Cryptography.CryptoStreamMode]::Read)
    $sr = New-Object IO.StreamReader($cs)
    $plaintext = [byte[]]::new($ciphertext.Length)
    $bytesRead = $cs.Read($plaintext, 0, $plaintext.Length)
    $cs.Dispose(); $ms.Dispose(); $aes.Dispose()
    
    return $plaintext[0..($bytesRead - 1)]
}

function Get-SHA256 {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash($stream)
    $stream.Close()
    return [System.BitConverter]::ToString($hash) -replace '-', ''
}

function Write-CppHeader {
    param([byte[]]$Key, [hashtable]$Hashes)
    
    $keyHex = ($Key | ForEach-Object { "0x{0:X2}" -f $_ }) -join ", "
    
    $lines = @(
        '#pragma once',
        '#include <cstdint>',
        '',
        'namespace ResourceProtection',
        '{',
        '    static constexpr std::size_t MASTER_KEY_SIZE = 32;',
        "    static constexpr uint8_t MASTER_KEY[MASTER_KEY_SIZE] = { $keyHex };",
        '',
        '    static constexpr uint32_t RESOURCE_MAGIC = 0x45585230;',
        ''
    )
    
    foreach ($name in $Hashes.Keys) {
        $varName = $name -replace '\.', '_' -replace '-', '_' -replace ' ', '_'
        $lines += "    static constexpr const char* SHA256_$varName = `"$($Hashes[$name].sha256)`";"
    }
    $lines += ""
    $lines += "    static constexpr int RESOURCE_COUNT = $($Hashes.Count);"
    $lines += "}"
    
    $lines -join "`r`n"
}

Write-Host "=== Resource Encryption Tool ===" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $EmbeddedDir)) {
    Write-Host "[!] Missing: $EmbeddedDir" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Path $EncryptedDir -Force | Out-Null

$key = Load-MasterKey
Write-Host "[+] Master key: $([System.BitConverter]::ToString($key[0..7]) -replace '-','')..." -ForegroundColor Green

$resourceHashes = @{}

if ($Decrypt) {
    Write-Host "[*] Decrypting..." -ForegroundColor Yellow
    foreach ($filename in $ResourceList) {
        $encPath = Join-Path $EncryptedDir "$filename.enc"
        $outPath = Join-Path $EmbeddedDir $filename
        if (Test-Path $encPath) {
            $encData = [IO.File]::ReadAllBytes($encPath)
            $magic = [System.BitConverter]::ToUInt32($encData, 0)
            if ($magic -ne 0x45585230) { continue }
            $origSize = [System.BitConverter]::ToUInt32($encData, 4)
            $payload = $encData[8..($encData.Length - 1)]
            $decrypted = Decrypt-Resource -Data $payload -Key $key
            if ($decrypted.Length -gt $origSize) { $decrypted = $decrypted[0..($origSize - 1)] }
            [IO.File]::WriteAllBytes($outPath, $decrypted)
            Write-Host "  OK: $filename" -ForegroundColor Green
        }
    }
    exit 0
}

Write-Host "[*] Encrypting..." -ForegroundColor Yellow

foreach ($filename in $ResourceList) {
    $inputPath = Join-Path $EmbeddedDir $filename
    if (-not (Test-Path $inputPath)) {
        $encPath = Join-Path $EncryptedDir "$filename.enc"
        if (Test-Path $encPath) {
            $encData = [IO.File]::ReadAllBytes($encPath)
            $magic = [System.BitConverter]::ToUInt32($encData, 0)
            if ($magic -eq 0x45585230) {
                $origSize = [System.BitConverter]::ToUInt32($encData, 4)
                $payload = $encData[8..($encData.Length - 1)]
                $decrypted = Decrypt-Resource -Data $payload -Key $key
                if ($decrypted.Length -gt $origSize) { $decrypted = $decrypted[0..($origSize - 1)] }
                [IO.File]::WriteAllBytes($inputPath, $decrypted)
                Write-Host "  Restored from .enc: $filename ($($decrypted.Length) bytes)" -ForegroundColor Cyan
            }
        }
    }
    if (-not (Test-Path $inputPath)) {
        Write-Host "  Skip: $filename (not found) - creating stub" -ForegroundColor DarkYellow
        $stubData = [byte[]]::new(32)
        [IO.File]::WriteAllBytes($inputPath, $stubData)
    }
    
    $fileData = [IO.File]::ReadAllBytes($inputPath)
    $encrypted = Encrypt-Resource -Data $fileData -Key $key
    
    $header = [byte[]]::new(8)
    [System.BitConverter]::GetBytes([uint32]0x45585230).CopyTo($header, 0)
    [System.BitConverter]::GetBytes([uint32]$fileData.Length).CopyTo($header, 4)
    
    $output = [byte[]]::new($header.Length + $encrypted.Length)
    $header.CopyTo($output, 0)
    $encrypted.CopyTo($output, $header.Length)
    
    $outputPath = Join-Path $EncryptedDir "$filename.enc"
    [IO.File]::WriteAllBytes($outputPath, $output)
    
    $sha256 = Get-SHA256 -Path $inputPath
    $inSize = $fileData.Length
    if (Test-Path $outputPath) {
        $outSize = (Get-ChildItem $outputPath -Force).Length
    } else { $outSize = $output.Length }
    Write-Host "  OK: $filename ($inSize -> $outSize bytes)" -ForegroundColor Green
    
    $resourceHashes[$filename] = @{
        sha256 = $sha256
        orig_size = $inSize
        res_id = $ResIds[$filename]
    }
}

$cppHeader = Write-CppHeader -Key $key -Hashes $resourceHashes
[IO.File]::WriteAllText($HeaderFile, $cppHeader, [Text.UTF8Encoding]::new($false))
Write-Host "[+] Header: $HeaderFile" -ForegroundColor Green

Write-Host ""
Write-Host "=== Complete! ===" -ForegroundColor Cyan
Write-Host "Next: keep Tools/resource_key.bin secure!"
