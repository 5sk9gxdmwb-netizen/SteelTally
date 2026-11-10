param(
 [Parameter(Mandatory=$true)][string]$QtPrefix,
 [Parameter(Mandatory=$true)][string]$CMake,
 [string]$Ninja='C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe',
 [ValidateSet('Ninja','MinGW Makefiles')][string]$Generator='MinGW Makefiles',
 [string]$CompilerRoot='C:\Qt\Tools\mingw1310_64',
 [switch]$DisableUpdates, [string]$UpdateConfig='',
 [string]$UpdateUrl='',
 [string]$UpdatePublicKeyFile='',
 [string]$PublishDirectory='',
 [string]$BuildRoot=(Join-Path $env:TEMP ('SteelTally-static-app-'+[guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
if(-not $UpdateConfig){$UpdateConfig=Join-Path $PSScriptRoot 'update-release.json'}
if($BuildRoot -match '[^\x00-\x7F]'){throw 'BuildRoot must be ASCII'}
if(Test-Path -LiteralPath $BuildRoot){throw 'BuildRoot must be new'}
$config=Join-Path $QtPrefix 'mkspecs\qconfig.pri'
if(-not (Test-Path $config)){throw 'Qt install config is missing'}
if(-not ([IO.File]::ReadAllText($config) -match 'QT_CONFIG[^\r\n]*\bstatic\b')){throw 'Qt prefix is not static'}
$env:PATH="$CompilerRoot\bin;$(Split-Path $Ninja);$(Split-Path $CMake);$env:SystemRoot\System32;$env:SystemRoot"
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
$source=Join-Path $BuildRoot 'table_qty'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'table_qty') -Destination $source -Recurse
# Makefile dependencies must use ASCII physical filenames; qrc aliases stay unchanged.
$qrcPath=Join-Path $source 'resources.qrc'
[xml]$qrc=[IO.File]::ReadAllText($qrcPath)
$asciiAssets=Join-Path $source 'resource-files'
New-Item -ItemType Directory -Path $asciiAssets | Out-Null
$resourceIndex=0
foreach($entry in $qrc.RCC.qresource.file){
 $original=Join-Path $source $entry.InnerText
 $filename=('r{0:D5}' -f $resourceIndex)+[IO.Path]::GetExtension($original)
 Copy-Item -LiteralPath $original -Destination (Join-Path $asciiAssets $filename)
 $entry.InnerText='resource-files/'+$filename
 $resourceIndex++
}
$qrc.Save($qrcPath)
$build=Join-Path $BuildRoot 'build'
$updateKey=''
if($DisableUpdates){
 if($UpdateUrl -or $UpdatePublicKeyFile){throw 'DisableUpdates conflicts with explicit update source'}
 $UpdateUrl=''
}elseif(-not $UpdateUrl -and -not $UpdatePublicKeyFile){
 $releaseConfig=Get-Content -LiteralPath $UpdateConfig -Raw -Encoding UTF8 | ConvertFrom-Json
 $UpdateUrl=[string]$releaseConfig.url
 $updateKey=[string]$releaseConfig.public_key
 if(-not $UpdateUrl -or -not $updateKey){throw 'Release update config is incomplete'}
}
if($UpdatePublicKeyFile){$updateKey=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $UpdatePublicKeyFile).Path).Trim()}
if($UpdateUrl -and (-not $updateKey)){throw 'UpdatePublicKeyFile is required with UpdateUrl'}
& $CMake -S $source -B $build "-DSTEELTALLY_UPDATE_URL=$UpdateUrl" "-DSTEELTALLY_UPDATE_PUBLIC_KEY=$updateKey" -G $Generator "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_PREFIX_PATH=$QtPrefix" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe" "-DCMAKE_C_COMPILER=$CompilerRoot/bin/gcc.exe" '-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++'
if($LASTEXITCODE){throw 'Static application configure failed'}
& $CMake --build $build --parallel 6
if($LASTEXITCODE){throw 'Static application build failed'}
$publish=if($PublishDirectory){$PublishDirectory}else{Join-Path $PSScriptRoot 'release-static'}
if(Test-Path $publish){
 $backup=Join-Path $PSScriptRoot ('旧版可删\release-static-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
 New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
 Move-Item -LiteralPath $publish -Destination $backup
}
New-Item -ItemType Directory -Path $publish | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'gjg_table_qty.exe') -Destination $publish
@{qt_prefix=$QtPrefix;build_root=$BuildRoot;cmake=$CMake;make_program=$Ninja;generator=$Generator;exe_sha256=(Get-FileHash (Join-Path $publish 'gjg_table_qty.exe')).Hash} | ConvertTo-Json | Set-Content (Join-Path $publish 'build-record.json') -Encoding UTF8
Write-Output "Static executable: $publish\gjg_table_qty.exe"
