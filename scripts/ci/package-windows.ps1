# Packs the Windows build into MSIX packages for the Microsoft Store (ADR-022).
#
#   scripts/ci/package-windows.ps1 -Build build -Out dist
#
# Writes to OUT:
#   Omnidict-<version>-x64.msix       unsigned, for Partner Center, which signs it
#   Omnidict-<version>-x64-test.msix  signed with a certificate made for this run, to try it out
#   Omnidict-test.cer                 that certificate, to trust before installing the test package
#   INSTALL-TEST.txt                  how
# Needs the MSVC developer environment (makeappx, makepri, signtool, VCToolsRedistDir) and Qt's
# bin folder (windeployqt) on PATH.
param(
    [Parameter(Mandatory = $true)][string]$Build,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot/../.."

$cmakeLists = Get-Content "$root/app/CMakeLists.txt" -Raw
if ($cmakeLists -notmatch 'project\(\s*omnidict[\s\S]*?VERSION\s+(\d+\.\d+\.\d+)') {
    throw 'No project(VERSION) in app/CMakeLists.txt'
}
$version = $Matches[1]
New-Item -ItemType Directory -Force $Out | Out-Null
$stage = Join-Path $Out 'stage'
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

# The program, then Qt beside it.
cmake --install $Build --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed' }
$bin = Join-Path $stage 'bin'
windeployqt --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime `
    --dir $bin "$bin/omnidict.exe"
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# SQLite, ICU and zstd from vcpkg, and the C++ runtime, kept in the package.
$vcpkg = Join-Path $Build 'vcpkg_installed/x64-windows'
Copy-Item "$vcpkg/bin/*.dll" $bin
Copy-Item "$env:VCToolsRedistDir/x64/Microsoft.VC143.CRT/*.dll" $bin

Get-ChildItem $stage -Recurse -Include *.pdb, *.lib, *.exp, *.ilk | Remove-Item -Force

# The manifest and its logos. The Store wants the fourth number of the version to be 0.
Copy-Item -Recurse "$root/packaging/windows/Assets" "$stage/Assets"
(Get-Content "$root/packaging/windows/AppxManifest.xml.in" -Raw).Replace('@VERSION@', "$version.0") |
    Set-Content -Encoding utf8 "$stage/AppxManifest.xml"

function Find-SdkTool([string]$name) {
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    $candidate = Join-Path $env:WindowsSdkVerBinPath "x64/$name"
    if (Test-Path $candidate) { return $candidate }
    throw "$name not found"
}
$makeappx = Find-SdkTool 'makeappx.exe'
$makepri = Find-SdkTool 'makepri.exe'
$signtool = Find-SdkTool 'signtool.exe'

# The resource index, through which Windows picks a logo of the right scale and size.
$priConfig = Join-Path $env:RUNNER_TEMP 'priconfig.xml'
& $makepri createconfig /cf $priConfig /dq en-US /pv 10.0.0 /o
if ($LASTEXITCODE -ne 0) { throw 'makepri createconfig failed' }
& $makepri new /pr $stage /cf $priConfig /mn "$stage/AppxManifest.xml" /of "$stage/resources.pri" /o
if ($LASTEXITCODE -ne 0) { throw 'makepri new failed' }

$store = Join-Path $Out "Omnidict-$version-x64.msix"
& $makeappx pack /o /d $stage /p $store
if ($LASTEXITCODE -ne 0) { throw 'makeappx failed' }

# A copy signed for trying out: the certificate names the Store publisher, as Windows requires,
# and lives only for this run.
$test = Join-Path $Out "Omnidict-$version-x64-test.msix"
Copy-Item $store $test
$publisher = ([xml](Get-Content "$stage/AppxManifest.xml")).Package.Identity.Publisher
$cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature `
    -FriendlyName 'Omnidict test package' -CertStoreLocation 'Cert:\CurrentUser\My' `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$password = ConvertTo-SecureString -String ([guid]::NewGuid().ToString()) -Force -AsPlainText
$pfx = Join-Path $env:RUNNER_TEMP 'omnidict-test.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $password | Out-Null
& $signtool sign /fd SHA256 /f $pfx /p ([System.Net.NetworkCredential]::new('', $password).Password) $test
if ($LASTEXITCODE -ne 0) { throw 'signtool failed' }
Export-Certificate -Cert $cert -FilePath (Join-Path $Out 'Omnidict-test.cer') | Out-Null
Remove-Item $pfx

@"
Trying out Omnidict $version from this folder

1. Open Omnidict-test.cer, choose Install Certificate, Local Machine, then
   "Place all certificates in the following store", Browse, Trusted People. Finish.
2. Open Omnidict-$version-x64-test.msix and choose Install.

To remove it later: uninstall Omnidict from Start, and delete the certificate from Trusted People
(certlm.msc). Omnidict-$version-x64.msix is the package for Partner Center; it does not install
on its own.
"@ | Set-Content -Encoding utf8 (Join-Path $Out 'INSTALL-TEST.txt')

Remove-Item -Recurse -Force $stage
Get-ChildItem $Out | Format-Table Name, Length
