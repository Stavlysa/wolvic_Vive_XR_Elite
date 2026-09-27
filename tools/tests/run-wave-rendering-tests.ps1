param(
    [Parameter(Mandatory = $true)][string]$NdkRoot,
    [Parameter(Mandatory = $true)][string]$Adb,
    [Parameter(Mandatory = $true)][string]$Serial
)
$ErrorActionPreference = 'Stop'
$testRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testOutputDir = Join-Path $testRepo 'build/wave-rendering-tests'
New-Item -ItemType Directory -Path $testOutputDir -Force | Out-Null
$testBinary = Join-Path $testOutputDir 'wave-rendering-tests'
$testCompiler = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
Push-Location $testRepo
try {
    & $testCompiler '--target=aarch64-linux-android25' '--driver-mode=g++' '-std=c++17' '-static-libstdc++' `
        '-Iapp/src/test/native/stubs' '-Iapp/src/main/cpp' '-Iapp/src/wavevr/cpp' `
        '-Ithird_party/wavesdk/build/wvr_client-5.6.0/include' `
        'app/src/test/native/WaveRenderingTests.cpp' '-o' $testBinary
    if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed' }
    & $Adb -s $Serial push $testBinary /data/local/tmp/wolvic-wave-rendering-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB push failed' }
    & $Adb -s $Serial shell chmod 755 /data/local/tmp/wolvic-wave-rendering-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB chmod failed' }
    & $Adb -s $Serial shell /data/local/tmp/wolvic-wave-rendering-tests
    if ($LASTEXITCODE -ne 0) { throw 'Native rendering tests failed' }

    $alphaBinary = Join-Path $testOutputDir 'wave-alpha-blend-tests'
    & $testCompiler '--target=aarch64-linux-android25' '--driver-mode=g++' '-std=c++17' '-static-libstdc++' `
        '-Iapp/src/main/cpp' '-Iapp/src/wavevr/cpp' `
        'app/src/test/native/WaveAlphaBlendTests.cpp' '-lEGL' '-lGLESv3' '-o' $alphaBinary
    if ($LASTEXITCODE -ne 0) { throw 'GPU alpha test compilation failed' }
    & $Adb -s $Serial push $alphaBinary /data/local/tmp/wolvic-wave-alpha-blend-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB alpha test push failed' }
    & $Adb -s $Serial shell chmod 755 /data/local/tmp/wolvic-wave-alpha-blend-tests
    if ($LASTEXITCODE -ne 0) { throw 'ADB alpha test chmod failed' }
    & $Adb -s $Serial shell /data/local/tmp/wolvic-wave-alpha-blend-tests
    if ($LASTEXITCODE -ne 0) { throw 'GPU alpha blending tests failed' }
} finally {
    & $Adb -s $Serial shell rm -f /data/local/tmp/wolvic-wave-rendering-tests
    & $Adb -s $Serial shell rm -f /data/local/tmp/wolvic-wave-alpha-blend-tests
    Pop-Location
}
