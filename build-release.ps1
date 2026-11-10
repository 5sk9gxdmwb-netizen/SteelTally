param([string]$PublishDirectory='', [switch]$DisableUpdates)
$ErrorActionPreference='Stop'
# Public release source/key are loaded by build-static.ps1 from update-release.json.
# Use the isolated, validated Schannel partial-record fix; original SDK is retained.
$argsForBuild=@{
 QtPrefix='C:\SteelTallySchannelDiagnostic\install'
 CMake='E:\SteelTallyToolchain\official-20261110-094827\cmake-3.31.8-windows-x86_64\extracted\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
}
if($PublishDirectory){$argsForBuild.PublishDirectory=$PublishDirectory}
if($DisableUpdates){$argsForBuild.DisableUpdates=$true}
& (Join-Path $PSScriptRoot 'build-static.ps1') @argsForBuild
