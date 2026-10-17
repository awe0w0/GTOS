$ErrorActionPreference = 'Stop'
$packet = 'F:/GTOS-Chromium/artifacts/native-events-20261017-a'
$out = Join-Path $packet 'source-pins-02.json'
if (Test-Path -LiteralPath $out) { throw 'Preserve previous source pins' }
$baseline = Get-Content -LiteralPath (Join-Path $packet 'baseline-01.json') -Raw | ConvertFrom-Json
foreach ($item in $baseline.files.PSObject.Properties) {
    $actual = (Get-FileHash -LiteralPath (Join-Path 'F:/GTOS-Chromium/sources/GTOS' $item.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $item.Value) { throw 'Canonical baseline changed' }
}
$names = @('candidate-v2/include/process/event_abi.h','candidate-v2/include/process/native_event_endpoint.h',
    'candidate-v2/include/process/native_events.h','candidate-v2/include/process/native_runtime.h',
    'candidate-v2/src/process/native_events.cpp','candidate-v2/src/process/native_runtime.cpp',
    'probe-v2/record.h','probe-v2/user.cpp','probe-v2/kernel.cpp','probe-v2/user.ld')
$files = [ordered]@{}
foreach ($name in $names) {
    $path = Join-Path $packet $name
    $item = Get-Item -LiteralPath $path
    if ($item.LinkType -or $item.PSIsContainer) { throw 'Regular reviewed source required' }
    $files['/mnt/f/GTOS-Chromium/artifacts/native-events-20261017-a/' + $name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
$value = [ordered]@{ canonical_dev=$baseline.canonical_dev; scope='Authored private event service/runtime integration and independent CPL3 guest fixtures; not acceptance'; files=$files }
[IO.File]::WriteAllText($out,($value | ConvertTo-Json -Depth 8) + "`n",[Text.UTF8Encoding]::new($false))
Get-FileHash -LiteralPath $out -Algorithm SHA256 | Select-Object Hash
