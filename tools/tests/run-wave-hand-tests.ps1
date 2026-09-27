param(
    [Parameter(Mandatory = $true)][string]$NdkRoot,
    [Parameter(Mandatory = $true)][string]$Adb,
    [Parameter(Mandatory = $true)][string]$Serial
)
$ErrorActionPreference = 'Stop'
$testRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testOutputDir = Join-Path $testRepo 'build/wave-hand-tests'
New-Item -ItemType Directory -Path $testOutputDir -Force | Out-Null
$testBinary = Join-Path $testOutputDir 'wave-hand-tests'
$testCompiler = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
Push-Location $testRepo
try {
    & $testCompiler '--target=aarch64-linux-android25' '--driver-mode=g++' '-std=c++17' '-static-libstdc++' `
        '-Iapp/src/wavevr/cpp' '-Ithird_party/wavesdk/build/wvr_client-5.6.0/include' `
        'app/src/test/native/WaveHandTrackingTests.cpp' '-o' $testBinary
    if ($LASTEXITCODE -ne 0) { throw 'Hand test compilation failed' }
    & $Adb -s $Serial push $testBinary /data/local/tmp/wolvic-wave-hand-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB hand test push failed' }
    & $Adb -s $Serial shell chmod 755 /data/local/tmp/wolvic-wave-hand-tests
    & $Adb -s $Serial shell /data/local/tmp/wolvic-wave-hand-tests
    if ($LASTEXITCODE -ne 0) { throw 'Hand input tests failed' }
} finally {
    & $Adb -s $Serial shell rm -f /data/local/tmp/wolvic-wave-hand-tests
    Pop-Location
}
