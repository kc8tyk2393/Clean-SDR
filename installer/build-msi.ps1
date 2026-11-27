#Requires -Version 5.1
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"
$Payload = Join-Path $Root "dist\payload"
$OutDir = Join-Path $Root "dist"
$Msi = Join-Path $OutDir "Brick2SDR-1.0.0.msi"

$Qt = "C:\Users\timda\QtSdk\6.8.3\mingw_64"
$Mingw = "C:\Users\timda\QtSdk\Tools\mingw1310_64"
$Cmake = "C:\Users\timda\AppData\Roaming\Python\Python313\site-packages\cmake\data\bin"
$Wix = Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe"
$Windeploy = Join-Path $Qt "bin\windeployqt.exe"

$env:Path = "$Cmake;$Mingw\bin;$Qt\bin;$(Join-Path $env:USERPROFILE '.dotnet\tools');$env:Path"

if (-not (Test-Path $Wix)) {
    throw "WiX is not installed. Run: dotnet tool install -g wix"
}
if (-not (Test-Path $Windeploy)) {
    throw "windeployqt not found at $Windeploy"
}

Write-Host "Building Brick2SDR..."
if (-not (Test-Path (Join-Path $Build "build.ninja"))) {
    cmake -S $Root -B $Build -G Ninja "-DCMAKE_PREFIX_PATH=$Qt" "-DCMAKE_BUILD_TYPE=$Configuration"
}
cmake --build $Build --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "Staging payload..."
if (Test-Path $Payload) { Remove-Item $Payload -Recurse -Force }
New-Item -ItemType Directory -Path $Payload -Force | Out-Null
Copy-Item (Join-Path $Build "Brick2SDR.exe") $Payload
Copy-Item (Join-Path $Build "wdsp.dll") $Payload
Copy-Item (Join-Path $Build "libfftw3-3.dll") $Payload

$runtime = @(
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "avcodec-61.dll",
    "avformat-61.dll",
    "avutil-59.dll",
    "swresample-5.dll",
    "swscale-8.dll",
    "D3Dcompiler_47.dll",
    "opengl32sw.dll"
)
foreach ($name in $runtime) {
    $src = Join-Path $Build $name
    if (Test-Path $src) { Copy-Item $src $Payload }
}

& $Windeploy --release --compiler-runtime --no-translations (Join-Path $Payload "Brick2SDR.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

foreach ($plugin in @("platforms", "imageformats", "iconengines", "styles", "tls", "networkinformation", "generic", "multimedia")) {
    $src = Join-Path $Build $plugin
    $dst = Join-Path $Payload $plugin
    if (Test-Path $src) {
        Copy-Item $src $dst -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$extDir = Join-Path $env:USERPROFILE ".wix\extensions\WixToolset.UI.wixext"
$uiExt = Get-ChildItem $extDir -Filter "WixToolset.UI.wixext.dll" -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $uiExt) {
    & $Wix extension add -g WixToolset.UI.wixext
    $uiExt = Get-ChildItem $extDir -Filter "WixToolset.UI.wixext.dll" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

Write-Host "Building MSI..."
$pkg = Join-Path $PSScriptRoot "Package.wxs"
$wixArgs = @(
    "build", $pkg,
    "-arch", "x64",
    "-ext", "WixToolset.UI.wixext",
    "-bindpath", $PSScriptRoot,
    "-bindpath", "Payload=$Payload",
    "-out", $Msi,
    "-pdbtype", "none"
)
& $Wix @wixArgs
if ($LASTEXITCODE -ne 0) { throw "WiX build failed" }

Write-Host "MSI written to $Msi"
Get-Item $Msi | Format-List FullName, Length, LastWriteTime
