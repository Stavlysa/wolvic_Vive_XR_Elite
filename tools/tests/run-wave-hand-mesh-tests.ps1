param(
    [Parameter(Mandatory = $true)][string]$NdkRoot,
    [Parameter(Mandatory = $true)][string]$Adb,
    [Parameter(Mandatory = $true)][string]$Serial
)
$ErrorActionPreference = 'Stop'
$testRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testDir = Join-Path $testRepo 'build/wave-hand-mesh-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testBinary = Join-Path $testDir 'wave-hand-mesh-tests'
$compiler = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
Push-Location $testRepo
try {
    & $compiler '--target=aarch64-linux-android25' '--driver-mode=g++' '-std=c++17' '-DANDROID' '-static-libstdc++' `
        '-Iapp/src/wavevr/cpp' '-Iapp/src/main/cpp/vrb/include' `
        '-Ithird_party/wavesdk/build/wvr_client-5.6.0/include' `
        'app/src/test/native/WaveHandMeshTests.cpp' '-lEGL' '-lGLESv3' '-llog' '-o' $testBinary
    if ($LASTEXITCODE -ne 0) { throw 'Hand model GPU test compilation failed' }
    & $Adb -s $Serial push $testBinary /data/local/tmp/wolvic-wave-hand-mesh-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB test push failed' }
    & $Adb -s $Serial shell chmod 755 /data/local/tmp/wolvic-wave-hand-mesh-tests
    & $Adb -s $Serial shell /data/local/tmp/wolvic-wave-hand-mesh-tests
    if ($LASTEXITCODE -ne 0) { throw 'Hand model GPU test failed' }
} finally {
    & $Adb -s $Serial shell rm -f /data/local/tmp/wolvic-wave-hand-mesh-tests
    Pop-Location
}
