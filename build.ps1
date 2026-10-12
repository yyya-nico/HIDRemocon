param(
    [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuildPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuildPath) { throw 'Visual Studio C++ build tools were not found.' }
& $msbuildPath (Join-Path $PSScriptRoot 'HIDRemocon.vcxproj') "/p:Platform=$Platform" "/p:Configuration=$Configuration" /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
