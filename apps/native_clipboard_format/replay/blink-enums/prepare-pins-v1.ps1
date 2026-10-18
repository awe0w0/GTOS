$ErrorActionPreference='Stop'
$packet='F:/GTOS-Chromium/artifacts/native-blink-enums-20261018-a'
$prior='F:/GTOS-Chromium/artifacts/native-content-frontier-20261018-a'
$raw='F:/GTOS-Chromium/sources/chromium-full/src'
$graphRoot=$prior+'/gn-clipboard-01'
$graph=$graphRoot+'/build/toolchain.ninja'
$receipt=$graphRoot+'/status.json'
$runtime=$prior+'/mojo-pins-01.json'
$git='C:/Users/user/AppData/Local/Programs/PortableGit-2.54.0-64-bit/cmd/git.exe'
$revision='04bca2584cddb39e134fc7c16bb2886e02863d0e'
$snapshot=$packet+'/source-v1'
$pinsPath=$packet+'/pins-01.json'
$resultPath=$packet+'/preparation-result-01.json'
function Hash([string]$path) {(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Text-Hash([string]$value) {
 $hasher=[Security.Cryptography.SHA256]::Create()
 try {([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($value)))).Replace('-','').ToLowerInvariant()} finally {$hasher.Dispose()}
}
function Linux-Path([string]$path) {
 $value=[IO.Path]::GetFullPath($path).Replace('\','/')
 if(-not $value.StartsWith('F:/GTOS-Chromium/')){throw 'Only fixed workspace paths'}
 '/mnt/f/'+$value.Substring(3)
}
function Assert-Physical([string]$path) {
 $full=[IO.Path]::GetFullPath($path)
 if(-not $full.StartsWith('F:\GTOS-Chromium\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside workspace'}
 $current=$full
 while($current.Length -gt 'F:\GTOS-Chromium'.Length){
  if(Test-Path -LiteralPath $current){$item=Get-Item -LiteralPath $current -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse path forbidden: '+$current}}
  $current=[IO.Path]::GetDirectoryName($current)
 }
}
Assert-Physical $packet
if([IO.Path]::GetFullPath($PSCommandPath) -cne [IO.Path]::GetFullPath($packet+'/prepare-pins-v1.ps1')){throw 'Fixed preparation script only'}
foreach($path in @($snapshot,$pinsPath,$resultPath)){if(Test-Path -LiteralPath $path){throw 'Fresh preparation outputs required'}}
if((Hash $git) -cne '81ef35ae005ca9318018d18e3327578ce939fb99feaad6b2d7c8ab15f3de8db5'){throw 'Fixed read-only Git changed'}
if((Hash $graph) -cne 'a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175'){throw 'Actual graph changed'}
if((Hash $receipt) -cne 'c3a702764baf8fcc8779b1fc49a3f80c7eaea146326c020269a886a791e3642f'){throw 'GN receipt changed'}
if((Hash $runtime) -cne '84c80909f8c2ef503c0239bb342f0b6fc170dbf0c46edb616f10d50f03fd6d60'){throw 'Original reusable runtime pins changed'}
$accepted=Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
if(-not ($accepted.complete -and $accepted.typed_GN_dependency_qualification -and $accepted.bound_inputs_unchanged -and $accepted.aliases_unchanged)){throw 'Qualified actual GN graph required'}
$old=Get-Content -Raw -LiteralPath $runtime | ConvertFrom-Json
$scripts='third_party/blink/renderer/build/scripts/'
$jinja=@('__init__.py','_identifier.py','async_utils.py','bccache.py','compiler.py','constants.py','debug.py','defaults.py','environment.py','exceptions.py','ext.py','filters.py','idtracking.py','lexer.py','loaders.py','meta.py','nativetypes.py','nodes.py','optimizer.py','parser.py','runtime.py','sandbox.py','tests.py','utils.py','visitor.py')
$markup=@('__init__.py','_compat.py','_constants.py','_native.py')
$reused=@($jinja | ForEach-Object {'third_party/jinja2/'+$_})+@($markup | ForEach-Object {'third_party/markupsafe/'+$_})
$json5=@('__init__.py','lib.py','parser.py','version.py') | ForEach-Object {'third_party/pyjson5/src/json5/'+$_}
$commonScripts=@('run_with_pythonpath.py','blinkbuild/name_style_converter.py','json5_generator.py','license.py','name_utilities.py','template_expander.py','templates/macros.tmpl','core/css/field_alias_expander.py','core/css/properties/templates/style_builder_functions.tmpl','make_runtime_features_utilities.py','make_runtime_features.py') | ForEach-Object {$scripts+$_}
$configuration=@('third_party/blink/renderer/platform/runtime_enabled_features.json5','third_party/blink/renderer/platform/runtime_enabled_features.override.json5')
$common=@($reused)+@($json5)+@($commonScripts)+@($configuration)
if($common.Count -ne 46 -or $reused.Count -ne 29){throw 'Fixed common input inventory'}
$specs=@(
 [ordered]@{name='origin-trials';target='//third_party/blink/public/mojom/origin_trials:generate_origin_trial_feature';rule='__third_party_blink_public_mojom_origin_trials_generate_origin_trial_feature___build_toolchain_gtos_clang_x86__rule';entry=$scripts+'make_origin_trial_feature_mojom_defs.py';template=$scripts+'templates/origin_trial_feature.mojom.tmpl';output='gen/third_party/blink/public/mojom/origin_trials/origin_trial_feature.mojom';output_dir='gen/third_party/blink/public/mojom/origin_trials/'},
 [ordered]@{name='runtime-feature';target='//third_party/blink/public/mojom/runtime_feature_state:generate_runtime_feature';rule='__third_party_blink_public_mojom_runtime_feature_state_generate_runtime_feature___build_toolchain_gtos_clang_x86__rule';entry=$scripts+'make_runtime_feature_mojom_defs.py';template=$scripts+'templates/runtime_feature.mojom.tmpl';output='gen/third_party/blink/public/mojom/runtime_feature_state/runtime_feature.mojom';output_dir='gen/third_party/blink/public/mojom/runtime_feature_state/'}
)
$commands=@{};$rows=@{}
$reader=[IO.File]::OpenText($graph)
try {while(($line=$reader.ReadLine()) -ne $null){foreach($spec in $specs){
 if($line -ceq ('rule '+$spec.rule)){if($commands.ContainsKey($spec.name)){throw 'Duplicate rule'};$commands[$spec.name]=$reader.ReadLine()}
 if($line.StartsWith('build '+$spec.output+':')){if($rows.ContainsKey($spec.name)){throw 'Duplicate output edge'};$rows[$spec.name]=$line}
}}} finally {$reader.Dispose()}
$helper=(Linux-Path ($prior+'/content-helper-clipboard-v1.py'))
$actions=[ordered]@{}
foreach($spec in $specs){
 $typedArgs=@('-I','../source/third_party/blink/renderer/build/scripts','-I','../source/third_party','-I','../source/third_party/pyjson5/src','-I','../source/tools',('../source/'+$spec.entry),'../source/third_party/blink/renderer/platform/runtime_enabled_features.json5','--output_dir',$spec.output_dir)
 $expected=@($helper,('../source/'+$scripts+'run_with_pythonpath.py'))+@($typedArgs)
 if($commands[$spec.name] -cne ('  command = '+($expected -join ' '))){throw 'Actual command differs from fixed typed stage'}
 $prefix='build '+$spec.output+': '+$spec.rule+' | '
 if(-not $rows[$spec.name].StartsWith($prefix)){throw 'Fixed whole output edge required'}
 $inputs=@($rows[$spec.name].Substring($prefix.Length).Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
 $expectedInputs=@($common)+@($spec.entry,$spec.template)
 if($inputs.Count -ne 48 -or @($inputs | Sort-Object -Unique).Count -ne 48){throw 'Exact48 source-only edge inputs required'}
 $inputs=@($inputs | ForEach-Object {if(-not $_.StartsWith('../source/')){throw 'No generated/unreviewed input'};$_.Substring('../source/'.Length)})
 if(@(Compare-Object -ReferenceObject @($expectedInputs | Sort-Object) -DifferenceObject @($inputs | Sort-Object) -CaseSensitive).Count -ne 0){throw 'Actual producer input inventory changed'}
 $actions[$spec.name]=[ordered]@{target=$spec.target;rule=$spec.rule;entry=$spec.entry;template=$spec.template;output=$spec.output;output_dir=$spec.output_dir;command=$commands[$spec.name].Substring(12);typed_wrapper_arguments=$typedArgs;inputs=$inputs;build_row_sha256=(Text-Hash $rows[$spec.name])}
}
$production=@($common)+@($specs | ForEach-Object {$_.entry;$_.template})+@($scripts+'blinkbuild/__init__.py')
$production=@($production | Sort-Object -Unique)
$declarations=@('third_party/blink/public/mojom/origin_trials/BUILD.gn','third_party/blink/public/mojom/runtime_feature_state/BUILD.gn',($scripts+'scripts.gni'),'third_party/jinja2/jinja2.gni','third_party/markupsafe/markupsafe.gni')
$all=@($production)+@($declarations)
if($production.Count -ne 51 -or $all.Count -ne 56 -or @($all | Sort-Object -Unique).Count -ne 56){throw 'Exact51 production/import and five direct declarations required'}
$tree=@(& $git --no-optional-locks --no-lazy-fetch -C $raw ls-tree $revision -- @all)
if($LASTEXITCODE -ne 0 -or $tree.Count -ne 56){throw 'Original fixed Git tree lookup failed'}
$blobs=@{}
foreach($row in $tree){if($row -cnotmatch '^(100644|100755) blob ([0-9a-f]{40})\t(.+)$'){throw 'Regular original Git blob required'};$blobs[$Matches[3]]=@{mode=$Matches[1];blob=$Matches[2]}}
$actual=@(& $git --no-optional-locks --no-lazy-fetch -C $raw hash-object --no-filters -- @all)
if($LASTEXITCODE -ne 0 -or $actual.Count -ne 56){throw 'Read-only Git blob hashing failed'}
$files=[ordered]@{};$declarationPins=[ordered]@{};$copied=0
for($i=0;$i -lt $all.Count;$i++){
 $relative=$all[$i]
 if(-not $blobs.ContainsKey($relative) -or $actual[$i] -cne $blobs[$relative].blob){throw 'Source differs from fixed original Git: '+$relative}
 $origin=$raw+'/'+$relative;Assert-Physical $origin;$digest=Hash $origin
 $reuse=$reused -ccontains $relative
 if($reuse){
  $item=$old.files.$relative
  if($null -eq $item -or $item.kind -cne 'unchanged' -or $item.blob -cne $actual[$i] -or $item.mode -cne $blobs[$relative].mode -or $item.raw_sha256 -cne $digest -or $item.snapshot_sha256 -cne $digest){throw 'Existing reusable original runtime identity mismatch'}
  $dest=$prior+'/mojo-source-v1/'+$relative;Assert-Physical $dest
  if($item.snapshot -cne (Linux-Path $dest) -or (Hash $dest) -cne $digest){throw 'Existing runtime snapshot changed'}
 }else{
  $dest=$snapshot+'/'+$relative;Assert-Physical $dest
  if(Test-Path -LiteralPath $dest){throw 'Never overwrite a source snapshot'}
  [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));[IO.File]::Copy($origin,$dest,$false);$copied++
  if((Hash $dest) -cne $digest){throw 'New source snapshot mismatch'}
 }
 $record=[ordered]@{raw=(Linux-Path $origin);raw_sha256=$digest;blob=$actual[$i];mode=$blobs[$relative].mode;snapshot=(Linux-Path $dest);snapshot_sha256=$digest;kind='unchanged';reused_runtime=$reuse}
 if($production -ccontains $relative){$files[$relative]=$record}else{$declarationPins[$relative]=$record}
}
if($copied -ne 27 -or $files.Count -ne 51 -or $declarationPins.Count -ne 5){throw 'Exact nonredundant copies required'}
$proof=[ordered]@{revision=$revision;graph=(Linux-Path $graph);graph_sha256=(Hash $graph);GN_receipt=(Linux-Path $receipt);GN_receipt_sha256=(Hash $receipt);runtime_manifest=(Linux-Path $runtime);runtime_manifest_sha256=(Hash $runtime);files=$files;declarations=$declarationPins;actions=$actions;extra_implicit_import=$scripts+'blinkbuild/__init__.py';scope='Preparation only: two actual Blink enum-schema producers. All51 production/import files plus five direct GN/GNI declarations retain original04bca Git bodies;29 original Jinja/MarkupSafe files reused without copying;22 production plus five declaration files newly copied. No project script/GN/Ninja/network execution.'}
$proof | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $pinsPath -Encoding utf8NoBOM
$newFiles=@(Get-ChildItem -LiteralPath $snapshot -Recurse -File)
if($newFiles.Count -ne 27 -or @($newFiles | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count -ne 0){throw 'Exact new regular-file snapshot inventory'}
foreach($item in (@($files.Values)+@($declarationPins.Values))){
 $origin='F:/'+$item.raw.Substring('/mnt/f/'.Length);$dest='F:/'+$item.snapshot.Substring('/mnt/f/'.Length)
 if((Hash $origin) -cne $item.raw_sha256 -or (Hash $dest) -cne $item.snapshot_sha256){throw 'Bound original or snapshot input changed'}
}
if((Hash $graph) -cne $proof.graph_sha256 -or (Hash $receipt) -cne $proof.GN_receipt_sha256 -or (Hash $runtime) -cne $proof.runtime_manifest_sha256){throw 'Graph or reusable runtime manifest changed'}
$summary=[ordered]@{complete=$true;preparation_script_sha256=(Hash $PSCommandPath);pins_sha256=(Hash $pinsPath);revision=$revision;Git_original_blobs_verified=56;production_import_files=51;direct_declarations=5;reused_original_runtime_files=29;new_production_files=22;new_declaration_files=5;new_snapshot_files=27;actual_GN_producer_stages=2;inputs_unchanged=$true;executed_project_scripts=$false;network_operations=$false;old_packages_modified=$false}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding utf8NoBOM
$summary | ConvertTo-Json -Depth 4
