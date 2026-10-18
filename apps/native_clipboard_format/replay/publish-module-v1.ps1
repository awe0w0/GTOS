$ErrorActionPreference='Stop'
$W='F:/GTOS-Chromium'
$Q=$W+'/artifacts/native-clipboard-format-20261018-a'
$P=$W+'/artifacts/native-content-frontier-20261018-a'
$E=$W+'/artifacts/native-blink-enums-20261018-a'
$R=$W+'/sources/GTOS'
$Target=$R+'/apps/native_clipboard_format'
# Root fills these only after complete real acceptance and final static review.
# Any pending digest stops before mkdir or any write to the public repository.
$PhaseHashes=[ordered]@{
 'gn-probe-01'=@{status='4432e32ab77e8c01d1a5e8d919176efc5745d37960523ace5ca7461e336523f2';guard='3be8e259bab6139d1cc378534228bce0c8cde62262e97dc19ed5ca463ca6c2aa';launcher='78ae4afbf3624d3a6b733ac1fb11e3d082981f0eb71004d36c3df7e862c59adf'}
 'compile-01'=@{status='9dd79f17f71913ddbda48f794aad708bca2990c299b6086ee01385e0b043141c';guard='1a2477d3bc91d899145e7d50b40e3d30ea1d6e5fb84a9d531bad22a0a229fc7a';launcher='f4c8e1c77ec810e0a3aff0861519875e183befb0df6723d998b98d8349bfae61'}
 'provider-source-05'=@{status='49223f63db9194e80f1ab27d4a4f0b99bc11d9f4623a6a28f07864a44ee2f2cf';guard='d1697744d8b3dcf3bd2a9be9929dc89b381cc9a20db283cf518da017995aaeac';launcher='f2f725ba3b9e0387498cebd357ba597b237e796c7fe755175e485aa2e5350f67'}
 'link-02'=@{status='00a1404d4177975589c9072335e21fc578aeb11622380f35dc5699b6b7bf2bd0';guard='d42c16e602fa145c590a6017f52f85a2502eb22fc222e702f8486fc9c35ac66c';launcher='1c59a5e1ec5b482314887ed00e474de428de4ddd0cd2b0684e89c6954a712adf'}
 'isa-01'=@{status='39679642eae38c2880a9bed51adaff40747f1f5896542c61fe82f1ce10eb64ae';guard='790a02f21cb65c2079ee4a07b3393f1f31d6fe76a98786764eb1063e6dcc465a';launcher='ca9f57b3cdcddcd6cee9d31938ecb3c028b8da8b5b1756d2661471a3fec2d68f'}
 'guest-01'=@{status='dafe9243d58f70eaedfb199ffa49e2a4c5f21d4997d7308ab5e50f5b5e3702d0';guard='9d66b6874f5e195a53272d6de92ffbe40fd6ad3d579ba0ff13206b0a8298b1ce';launcher='36f328e2006f129dd2f3beabbd370e782d2f68ceaaaa0bba5391307705f2c9bb'}
}
$ReadmeSha='68ac7b12c1d3bfe28bdcb7c1ae466e35736219c142a76fc978ec52d9c528f2c0'
$Plan=[Collections.Generic.List[object]]::new()
$Bound=[ordered]@{}
$SourceCopies=[ordered]@{}
$EvidenceCopies=[ordered]@{}
$GeneratedHashes=[ordered]@{}

function Hash([string]$path) {(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Digest([string]$value) {if($value -cnotmatch '^[0-9a-f]{64}$'){throw 'Pending or malformed reviewed SHA256'}}
function Physical([string]$path,[bool]$file=$true) {
 $full=[IO.Path]::GetFullPath($path)
 if(-not $full.StartsWith('F:\GTOS-Chromium\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside fixed workspace'}
 $current=$full
 while($current.Length -gt 'F:\GTOS-Chromium'.Length){
  if(Test-Path -LiteralPath $current){$item=Get-Item -LiteralPath $current -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'No symbolic-link or junction source/target: '+$current}}
  $current=[IO.Path]::GetDirectoryName($current)
 }
 if($file){$item=Get-Item -LiteralPath $full -Force;if($item.PSIsContainer){throw 'Only individual physical files may be copied'}}
 $full
}
function Linux([string]$path) {'/mnt/f/'+([IO.Path]::GetFullPath($path).Replace('\','/')).Substring(3)}
function Bind([string]$path,[string]$digest) {
 Digest $digest;$actual=Physical $path
 if((Hash $actual) -cne $digest){throw 'Changed reviewed source: '+$actual}
 if($Bound.Contains($actual) -and $Bound[$actual] -cne $digest){throw 'Source identity conflict'}
 $Bound[$actual]=$digest
}
function Read-Locked([string]$path,[string]$digest) {Bind $path $digest;Get-Content -Raw -LiteralPath $path | ConvertFrom-Json}
function True-Fields($record,[string[]]$names) {foreach($name in $names){if($record.$name -cne $true){throw 'Missing successful actual field: '+$name}};if($record.PSObject.Properties.Name -ccontains 'failure'){throw 'Failed prerequisite'}}
function Input-Hash($record,[string]$path) {$value=$record.inputs.(Linux $path);Digest $value;$value}
function Queue([string]$source,[string]$relative,[string]$digest,[string]$kind) {
 if($relative -cnotmatch '^[A-Za-z0-9_./+-]+$' -or @($relative.Split('/') | Where-Object {$_ -eq '..' -or $_ -eq ''}).Count -ne 0){throw 'Exact safe package-relative filename required'}
 if([IO.Path]::GetExtension($relative) -cin @('.o','.a','.elf','.bin','.iso','.img','.exe','.dll','.pyc')){throw 'No binary publication'}
 if(@($Plan | Where-Object {$_.relative -ceq $relative}).Count -ne 0){throw 'Duplicate package filename'}
 Bind $source $digest;$Plan.Add([PSCustomObject]@{source=$source;relative=$relative;sha256=$digest;kind=$kind})
}
function CopyExact($item) {
 Bind $item.source $item.sha256;$dest=$Target+'/'+$item.relative;[void](Physical $dest $false)
 if(Test-Path -LiteralPath $dest){throw 'Never overwrite public module files'}
 [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
 [IO.File]::Copy($item.source,$dest,$false)
 if((Hash $item.source) -cne $item.sha256 -or (Hash $dest) -cne $item.sha256){throw 'Copy bytes changed'}
 $record=[ordered]@{source=(Linux $item.source);sha256=$item.sha256}
 if($item.kind -ceq 'source'){$SourceCopies[$item.relative]=$record}else{$EvidenceCopies[$item.relative]=$record}
}
function Json-File([string]$relative,$value) {
 $dest=$Target+'/'+$relative;[void](Physical $dest $false)
 if(Test-Path -LiteralPath $dest){throw 'Fresh generated manifest only'}
 $value | ConvertTo-Json -Depth 35 | Set-Content -LiteralPath $dest -Encoding utf8NoBOM
 $GeneratedHashes[$relative]=Hash $dest
}

if([IO.Path]::GetFullPath($PSCommandPath) -cne [IO.Path]::GetFullPath($Q+'/publish-module-v1.ps1')){throw 'Fixed publisher only'}
foreach($name in $PhaseHashes.Keys){foreach($digest in $PhaseHashes[$name].Values){Digest $digest}}
Digest $ReadmeSha
[void](Physical $Target $false)
if(Test-Path -LiteralPath $Target){throw 'Public module must not already exist'}
$Phases=@{};$Guards=@{};$Launchers=@{}
$drivers=@{'gn-probe-01'=@('gn-probe-v1.py','launch-gn-probe-v1.py');'compile-01'=@('compile-probe-v1.py','launch-compile-probe-v1.py');'provider-source-05'=@('provider-source-v5.py','launch-provider-source-v5.py');'link-02'=@('link-probe-v2.py','launch-link-probe-v2.py');'isa-01'=@('isa-review-v1.py','launch-isa-review-v1.py');'guest-01'=@('guest-probe-v1.py','launch-guest-probe-v1.py')}
foreach($name in $PhaseHashes.Keys){
 $hashes=$PhaseHashes[$name]
 $record=Read-Locked ($Q+'/'+$name+'/status.json') $hashes.status
 $guard=Read-Locked ($Q+'/'+$name+'/guard.json') $hashes.guard
 $launcher=Read-Locked ($Q+'/'+$name+'/launcher-result.json') $hashes.launcher
 True-Fields $record @('complete','bound_inputs_unchanged','aliases_unchanged')
 True-Fields $guard @('landlock_applied','network_disabled','workspace_read_only','source_write_denied')
 if($guard.directory_execute_permissions -cne $false -or $guard.writable_output -cne (Linux ($Q+'/'+$name)) -or $launcher.returncode -ne 0 -or $launcher.guard_sha256 -cne $hashes.guard -or $launcher.child_sha256 -cne $guard.child_sha256){throw 'Exact successful phase/guard/launcher chain required'}
 Queue ($Q+'/'+$name+'/status.json') ('receipts/'+$name+'-status.json') $hashes.status 'evidence'
 Queue ($Q+'/'+$name+'/guard.json') ('receipts/'+$name+'-guard.json') $hashes.guard 'evidence'
 Queue ($Q+'/'+$name+'/launcher-result.json') ('receipts/'+$name+'-launcher-result.json') $hashes.launcher 'evidence'
 Queue ($Q+'/'+$drivers[$name][0]) ('replay/'+$drivers[$name][0]) $guard.child_sha256 'evidence'
 Queue ($Q+'/'+$drivers[$name][1]) ('replay/'+$drivers[$name][1]) $guard.launcher_sha256 'evidence'
 $Phases[$name]=$record;$Guards[$name]=$guard;$Launchers[$name]=$launcher
}
$gn=$Phases['gn-probe-01'];$compile=$Phases['compile-01'];$provider=$Phases['provider-source-05'];$link=$Phases['link-02'];$isa=$Phases['isa-01'];$guest=$Phases['guest-01']
True-Fields $gn @('gn_generation_pass','typed_GN_dependency_qualification','formal_clipboard_fixture_targets','prior_owners_byte_identical','whole_toolchain_and_generation_edges_preserved')
True-Fields $compile @('diagnostic_complete','formal_CXX_ASM_pass','full_CXX_M_MD_source_qualification','old_cache_metadata_unchanged','outputs_unchanged','original_format_object_reused_without_recompile')
True-Fields $provider @('provider_source_qualification_pass','old_cache_metadata_unchanged','outputs_unchanged','exit_readonly_preflight_before_Clang','exit_new_M_qualified','exit_materialized_physical_sources_only')
True-Fields $link @('clipboard_link_pass','old_cache_metadata_unchanged','outputs_unchanged','heap_new_M_qualified','exit_locked_M_MD_requalified','exit_new_M_qualified','exit_materialized_physical_sources_only')
if($provider.exit_legacy_source_view_consumed -cne $false -or $link.exit_legacy_source_view_consumed -cne $false -or $provider.new_Clang_M_commands -ne 2){throw 'Only locked physical exit sources and two new complete M actions'}
if($provider.exit_locked_M_MD_count -ne 113 -or $provider.exit_unique_dependency_count -ne 111){throw 'All 113 raw exit M/MD entries and 111 unique sources required'}
True-Fields $isa @('ISA_review_pass','outputs_unchanged','all_indexed_executable_bytes_decoded_once_and_matched_to_object','IA32_i686_x87_SSE2_only')
True-Fields $guest @('native_clipboard_format_pass','ASCII_CHECK_abort_exit_pass','old_cache_metadata_unchanged','outputs_unchanged')
foreach($flag in @('desktop_clipboard_service_pass','native_Content_pass','browser_pass','production_image_changed')){if($guest.$flag -cne $false){throw 'Published scope must match actual guest scope'}}
if(@($compile.objects.PSObject.Properties).Count -ne 4 -or @($isa.objects.PSObject.Properties).Count -ne 5 -or @($guest.guests).Count -ne 3){throw 'Exact accepted stage scopes required'}
$core=Read-Locked ($R+'/apps/native_environment/source-lock.json') '21ac0f2b88dc8e05310b1976b2b610863abea3d1a52d8f9cf16dcc5fc8041d34'
$canonical=@('include/process/native_runtime.h','src/process/native_runtime.cpp','include/process/environment_abi.h','include/process/native_environment.h','include/multitasking.h','src/multitasking.cpp','include/process/cpu_clock_abi.h','include/process/native_cpu_clock.h')
if(@($core.CoreFiles.PSObject.Properties).Count -ne 8){throw 'Exactly eight canonical kernel files'}
foreach($relative in $canonical){$path=$R+'/'+$relative;$digest=$core.CoreFiles.$relative;Bind $path $digest;if($guest.inputs.(Linux $path) -cne $digest){throw 'Actual native guest did not bind current canonical kernel bytes'}}
Queue ($R+'/apps/native_environment/source-lock.json') 'provenance/canonical-kernel-source-lock.json' '21ac0f2b88dc8e05310b1976b2b610863abea3d1a52d8f9cf16dcc5fc8041d34' 'evidence'
$patch=Read-Locked ($P+'/clipboard-source-lock-01.json') '9520e54015c69f1f06cd7db6974be013279585fcab18585a4415f1f6d73c1d94'
foreach($leaf in @('clipboard_format_type.h','clipboard_format_type_aura.cc','BUILD.gn')){
 $relative='ui/base/clipboard/'+$leaf
 Queue ($P+'/clipboard-source-v1/'+$relative) ('patches/'+$relative) $patch.files.$relative.snapshot_sha256 'source'
}
Queue ($P+'/clipboard-source-lock-01.json') 'provenance/clipboard-source-lock.json' '9520e54015c69f1f06cd7db6974be013279585fcab18585a4415f1f6d73c1d94' 'evidence'
$pins=Read-Locked ($Q+'/probe-source-pins-01.json') '2cdcd9bfaa37bde63741ec2cd25994492ba1e3333ccd72082058104274d70749'
foreach($leaf in @('BUILD.gn','api.h','checks.cc','entry.cc','start.s')){
 $source=if($leaf -ceq 'BUILD.gn'){$Q+'/gn-probe-source-v1/tools/gtos_clipboard_format_probe/BUILD.gn'}else{$P+'/clipboard-probe-v1/'+$leaf}
 $item=$pins.files.('tools/gtos_clipboard_format_probe/'+$leaf)
 if($item.path -cne (Linux $source)){throw 'Exact authored fixture source path'}
 Queue $source ('probe/'+$leaf) $item.sha256 'source'
}
Queue ($Q+'/probe-source-pins-01.json') 'provenance/probe-source-pins.json' '2cdcd9bfaa37bde63741ec2cd25994492ba1e3333ccd72082058104274d70749' 'evidence'
Queue ($Q+'/clipboard-v1.ld') 'probe/clipboard.ld' (Input-Hash $link ($Q+'/clipboard-v1.ld')) 'source'
Queue ($Q+'/clipboard-kernel-v1.cpp') 'probe/kernel.cpp' (Input-Hash $guest ($Q+'/clipboard-kernel-v1.cpp')) 'source'
foreach($leaf in @('content-helper-v1.py','content-dotfile-v1.gn')){Queue ($Q+'/'+$leaf) ('replay/'+$leaf) (Input-Hash $gn ($Q+'/'+$leaf)) 'evidence'}
Queue ($P+'/clipboard-format-compile-02/status.json') 'receipts/original-format-compile-status.json' '0b7e704222a57503715e3d09d979e714d80665db43ebc22ef40ffaab33e58d11' 'evidence'
foreach($name in @('positive-32M-smp1','positive-64M-smp4','negative-32M-smp1')){
 $rows=@($guest.guests | Where-Object {$_.name -ceq $name})
 if($rows.Count -ne 1){throw 'Exact three native guest logs'}
 Queue ($Q+'/guest-01/'+$name+'-guest.log') ('logs/'+$name+'.log') $rows[0].log_sha256 'evidence'
}
foreach($label in @('positive','negative')){
 $rows=@($guest.commands | Where-Object {$_.name -ceq ($label+'-ISA')})
 if($rows.Count -ne 1 -or $rows[0].returncode -ne 0 -or -not $guest.kernels.$label.full_instruction_audit_pass){throw 'Actual complete kernel ISA audit logs'}
 Queue ($Q+'/guest-01/'+$label+'-ISA.log') ('receipts/kernel-'+$label+'-ISA.log') $rows[0].log_sha256 'evidence'
}
foreach($name in @('checks','positive-entry','negative-entry','start','original-format')){
 $path=$Q+'/isa-01/'+$name+'.disassembly.txt'
 Queue $path ('receipts/user-'+$name+'.disassembly.txt') $isa.outputs.(Linux $path) 'evidence'
}
Queue ($W+'/sources/chromium-full/src/LICENSE') 'licenses/Chromium-LICENSE.txt' '368cca1106be99d39ecd32a38d8305585d802a475effb66380b91ffc9bcf709b' 'evidence'

# Separate successful original schema production; this is not Mojo C++ evidence.
$enums=Read-Locked ($E+'/run-01/status.json') '59b508a4fe90b8dfa7cc08b666bcda6e112320b9bb444ed13b5d02007c7d432f'
True-Fields $enums @('complete','generation_pass','inputs_unchanged','aliases_unchanged','staged_inputs_unchanged','evidence_unchanged','outputs_unchanged','all_workers_terminal')
if($enums.qualified_schema_outputs -ne 2 -or $enums.native_Content_pass -cne $false -or $enums.browser_pass -cne $false){throw 'Only two original enum schemas are qualified'}
Queue ($E+'/run-01/status.json') 'provenance/blink-enums/status.json' '59b508a4fe90b8dfa7cc08b666bcda6e112320b9bb444ed13b5d02007c7d432f' 'evidence'
Queue ($E+'/run-01/guard.json') 'provenance/blink-enums/guard.json' '215f99dc52314289e2a1bc8d2630d3451007e8b9523f4a6573a7ef3410d40708' 'evidence'
Queue ($E+'/run-01/launcher-result.json') 'provenance/blink-enums/launcher-result.json' '4ac238ab13201ce16d84b757be1865a83adddc321f00b703168b8406220bf4b6' 'evidence'
Queue ($E+'/pins-01.json') 'provenance/blink-enums/producer-source-pins.json' 'ff3288a5b034217d344f74869d9cd652f03e13e1498ab2a27c6412715661c70e' 'evidence'
Queue ($E+'/preparation-result-01.json') 'provenance/blink-enums/preparation-result.json' '4b6500f61518afa92a67b5669764bd4ff52e6f5a1b31011f50da1f0a2b6a6478' 'evidence'
foreach($leaf in @('generate-enums-v1.py','enum-worker-v1.py','launch-enums-v1.py','prepare-pins-v1.ps1')){Queue ($E+'/'+$leaf) ('replay/blink-enums/'+$leaf) (Input-Hash $enums ($E+'/'+$leaf)) 'evidence'}
foreach($spec in @(@('origin-trials','origin_trials','origin_trial_feature'),@('runtime-feature','runtime_feature_state','runtime_feature'))){
 $stage=$spec[0];$directory=$spec[1];$stem=$spec[2]
 $path=$E+'/run-01/'+$stage+'/build/gen/third_party/blink/public/mojom/'+$directory+'/'+$stem+'.mojom'
 Queue $path ('provenance/blink-enums/generated/'+$stem+'.mojom') $enums.outputs.(Linux $path) 'evidence'
 $rows=@($enums.stages | Where-Object {$_.stage -ceq $stage})
 if($rows.Count -ne 1){throw 'Exact original enum worker evidence'}
 Queue ($E+'/run-01/'+$stage+'/worker-status.json') ('provenance/blink-enums/'+$stage+'-status.json') $rows[0].worker_status_sha256 'evidence'
 Queue ($E+'/run-01/'+$stage+'/worker-job.json') ('provenance/blink-enums/'+$stage+'-job.json') $rows[0].worker_job_sha256 'evidence'
}
Queue ($Q+'/execution-review-v1.md') 'provenance/execution-review.md' '20790bc2a52616a3e1f676676904bff31bda08e7412ca9b706bf1764ee089bab' 'evidence'
Queue ($Q+'/qualification-review-v1.md') 'provenance/qualification-review.md' 'b171549e34554bef3d121e8449b80765aef70bc62ec82d653c925fabd6431deb' 'evidence'
Queue ($Q+'/public-README-draft.md') 'README.md' $ReadmeSha 'source'
Queue ($Q+'/publish-module-v1.ps1') 'replay/publish-module-v1.ps1' (Hash $PSCommandPath) 'evidence'

# All source/evidence gates and exact individual files are bound before mkdir.
foreach($path in $Bound.Keys){if((Hash $path) -cne $Bound[$path]){throw 'Bound input changed before publication'}}
if(Test-Path -LiteralPath $Target){throw 'Never merge into an existing public module'}
[void][IO.Directory]::CreateDirectory($Target)
foreach($item in $Plan){CopyExact $item}
Json-File 'source-copies.json' $SourceCopies
Json-File 'evidence-copies.json' $EvidenceCopies
$public=[ordered]@{}
foreach($item in $Plan){$public[$item.relative]=$item.sha256}
foreach($leaf in @('source-copies.json','evidence-copies.json')){$public[$leaf]=Hash ($Target+'/'+$leaf)}
# acceptance.json is deliberately excluded to avoid a SHA cycle.
$lock=[ordered]@{Module='GTOS native ClipboardFormatType';ChromiumRevision=$patch.revision;CoreFiles=$core.CoreFiles;PublicCopies=$public;Scope='Complete original MIME string-format class with minimal GTOS selection; no clipboard data service or Content/browser runtime.'}
Json-File 'source-lock.json' $lock
$acceptance=[ordered]@{Module='GTOS native ClipboardFormatType';Complete=$true;SourceLockSHA256=(Hash ($Target+'/source-lock.json'));ReceiptSHA256=$PhaseHashes;GN=[ordered]@{Targets=$gn.GN_targets;Files=$gn.GN_files};Compile=[ordered]@{Objects=@($compile.objects.PSObject.Properties).Count;OriginalFormatObject=$compile.reused_format_object};Link=$link.outputs;UserISA=[ordered]@{Objects=@($isa.objects.PSObject.Properties).Count;DecodedBytes=$isa.decoded_bytes;WholeExecutableCoverage=$isa.all_indexed_executable_bytes_decoded_once_and_matched_to_object};Native=[ordered]@{PositiveContexts=$guest.positive_native_contexts;NegativeContexts=$guest.negative_native_contexts;ASCII_CHECK_abort_exit_pass=$guest.ASCII_CHECK_abort_exit_pass;Guests=$guest.guests;Kernels=$guest.kernels};BlinkEnums=[ordered]@{GeneratedSchemas=$enums.qualified_schema_outputs;Stages=@($enums.stages | Select-Object stage,enum_name,enum_members);MojoCXXPass=$false};DesktopClipboardServicePass=$guest.desktop_clipboard_service_pass;NativeContentPass=$guest.native_Content_pass;BrowserPass=$guest.browser_pass;ProductionImageChanged=$guest.production_image_changed;ColdBuildClaim=$false}
$acceptance['ProviderSourceQualification']=[ordered]@{NewMCommands=$provider.new_Clang_M_commands;HeapFullMInputs=$provider.new_full_M_heap_count;ExitRawEntries=$provider.new_full_M_exit_raw_count;ExitPhysicalFiles=$provider.new_full_M_exit_count;ExitUsesPhysicalSources=$provider.exit_materialized_physical_sources_only;ExitUsesLegacySourceView=$provider.exit_legacy_source_view_consumed;ProviderObjectsRecompiled=$provider.recompiled_provider_objects}
Json-File 'acceptance.json' $acceptance
foreach($path in $Bound.Keys){if((Hash $path) -cne $Bound[$path]){throw 'Bound input changed after publication'}}
foreach($relative in $public.Keys){if((Hash ($Target+'/'+$relative)) -cne $public[$relative]){throw 'Public copy or manifest changed'}}
foreach($relative in $GeneratedHashes.Keys){if((Hash ($Target+'/'+$relative)) -cne $GeneratedHashes[$relative]){throw 'Generated public manifest changed after writing'}}
foreach($relative in $canonical){if((Hash ($R+'/'+$relative)) -cne $core.CoreFiles.$relative){throw 'Canonical kernel ABI changed'}}
[PSCustomObject]@{module=$Target;source_copies=$SourceCopies.Count;evidence_copies=$EvidenceCopies.Count;canonical_kernel_files=8;source_lock_sha256=(Hash ($Target+'/source-lock.json'));acceptance_sha256=(Hash ($Target+'/acceptance.json'));git_operations=$false;production_image_changed=$false} | ConvertTo-Json
