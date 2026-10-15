$ErrorActionPreference = 'Stop'
$packetPath = 'F:/GTOS-Chromium/artifacts/native-runtime-gap-20261015-a'
if (Test-Path -LiteralPath "$packetPath/pow-reference-v1.h") { throw 'Preserve prior reference' }
$cases = @(
    @{base='2.0';exponent='0.5';numerator=2;denominator=1;degree=2},
    @{base='3.0';exponent='0.5';numerator=3;denominator=1;degree=2},
    @{base='3.0';exponent='0.25';numerator=3;denominator=1;degree=4},
    @{base='7.0';exponent='0.25';numerator=7;denominator=1;degree=4},
    @{base='1.25';exponent='1.5';numerator=125;denominator=64;degree=2}
)
$unit = [Numerics.BigInteger]::Pow([Numerics.BigInteger]2,52)
$header = [Collections.Generic.List[string]]::new()
$header.Add('// Independent exact integer root brackets; no target math or host floating-point oracle.')
$header.Add('struct RuntimePowReference { double base,exponent;unsigned long long expected[4]; };')
$header.Add('static const RuntimePowReference runtimePowReferences[] = {')
$proofs = @()
foreach ($case in $cases) {
    $target = [Numerics.BigInteger]$case.numerator * [Numerics.BigInteger]::Pow([Numerics.BigInteger]2,52*$case.degree)
    $denominator = [Numerics.BigInteger]$case.denominator
    $low = $unit
    $high = $unit * 2
    if ([Numerics.BigInteger]::Pow($low,$case.degree)*$denominator -ge $target -or
        [Numerics.BigInteger]::Pow($high,$case.degree)*$denominator -le $target) { throw 'Root outside fixed [1,2) reference interval' }
    while ($high-$low -gt 1) {
        $middle = [Numerics.BigInteger]::Divide($low+$high,[Numerics.BigInteger]2)
        if ([Numerics.BigInteger]::Pow($middle,$case.degree)*$denominator -lt $target) { $low=$middle } else { $high=$middle }
    }
    if ([Numerics.BigInteger]::Pow($low,$case.degree)*$denominator -ge $target -or
        [Numerics.BigInteger]::Pow($high,$case.degree)*$denominator -le $target) { throw 'Exact adjacent representable bracket failed' }
    $midpointPower = [Numerics.BigInteger]::Pow(2*$low+1,$case.degree)*$denominator
    $midpointTarget = $target * [Numerics.BigInteger]::Pow([Numerics.BigInteger]2,$case.degree)
    $nearest = if ($midpointPower -lt $midpointTarget) { $high } elseif ($midpointPower -gt $midpointTarget -or $low.IsEven) { $low } else { $high }
    $biasedExponent = [UInt64]0x3ff0000000000000
    $expected = @(($biasedExponent+[UInt64]($nearest-$unit)),($biasedExponent+[UInt64]($low-$unit)),
        ($biasedExponent+[UInt64]($high-$unit)),($biasedExponent+[UInt64]($low-$unit)))
    $hex = @($expected | ForEach-Object { '0x'+$_.ToString('x16')+'ULL' })
    $header.Add('    {'+$case.base+','+$case.exponent+',{'+($hex -join ',')+'}},')
    $proofs += [ordered]@{base=$case.base;exponent=$case.exponent;numerator=$case.numerator;denominator=$case.denominator;
        root_degree=$case.degree;low_mantissa=$low.ToString();high_mantissa=$high.ToString();expected_bits=$hex;
        strict_exact_bracket_verified=$true;exact_midpoint_nearest_even_verified=$true}
}
$header.Add('};')
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText("$packetPath/pow-reference-v1.h",($header -join [char]10)+[char]10,$encoding)
$result = [ordered]@{independent_exact_integer_reference=$true;host_float_not_used=$true;target_not_called=$true;
    mode_order=@('nearest_even','down','up','toward_zero');cases=$proofs;
    header_sha256=(Get-FileHash -LiteralPath "$packetPath/pow-reference-v1.h" -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText("$packetPath/pow-reference-v1.json",(($result | ConvertTo-Json -Depth 12).Replace([string][char]13,'')+[char]10),$encoding)
Write-Output 'Five exact rational root references verified for four rounding modes'
