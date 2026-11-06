#!/usr/bin/env python3
"""Apply GTOS platform changes only to a previously composed private view."""
import argparse
import importlib.util
import json
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('candidate', HERE.parent / 'port-candidate-r20/candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)

INPUTS = {
    'third_party/perfetto/include/perfetto/ext/base/paged_memory.h': '7aa47838b604cd6f8fdd1417d8412718535d9226a07183c95097dd23c2d9b77c',
    'third_party/perfetto/include/perfetto/base/time.h': 'c0c1498a52e97f557b5bc27b0d60c2c4d51aee621fdac16a41ff940a295cf8ee',
    'third_party/perfetto/include/perfetto/base/thread_utils.h': 'a2a30174d7abacbb8c2a8fc179982b6af2e66609ddbff7ebf550a587f72fced7',
    'third_party/perfetto/include/perfetto/ext/base/thread_checker.h': 'c8664a4a3d9ca94a5e740ecb7c388049647edb66d181bbb1fa324c9423af3a77',
    'third_party/perfetto/src/base/thread_checker.cc': '9a550bb7d2b5b89fc5e6e031f952222077c6ae4bb513ff75c278905464853e35',
    'build/config/gtos/BUILD.gn': 'aacb5f51f70ea5938d172639d7c360f9817ab4799c1d6a42b56cadc9bbf6d0a5',
    'third_party/perfetto/include/perfetto/base/build_config.h': '01c8b46ffc3aeb50287c3821249ba6c3ce662f0fe1a2f17958e28cac33bfc62c',
    'third_party/perfetto/src/base/BUILD.gn': '718b055ac9e786276fb6263b76f761b7dc2a6e133199040e97f2f6ea00fb06f1',
    'third_party/devtools-frontend/src/scripts/build/esbuild.js': '52d417868256adbe0f95558d591b641f0d07e34e162d01451ae6dbaf84542a8f',
    'third_party/node/check_version.py': '9a50867752c5ffcaf6c3b7a3fce6d0a704dd87b78fe3283da4185fe674902f3f',
    'build/toolchain/gtos/BUILD.gn': '167bac3e52fb475a55050346979c08b1619f06053942ec984653e4e57a05800b',
    'build/config/clang/BUILD.gn': 'e30467f05002af51f125dfe764294844228ceef5adc3454e261256f803120289',
    'third_party/dawn/src/utils/platform.h': '28c73c24cc45e33b52aedba8deb9be6743be61237feedfdcdffe97fea664f28c',
    'third_party/dawn/src/dawn/common/BUILD.gn': '5ddfe58be80ec142acdcfc5a0b2828a95b6d8562b23aaf798dabd4cc20eeb968',
    'third_party/dawn/src/dawn/common/DynamicLib.h': '9f2cc9ee99bcf18a2f4d5b6541f825f72fb9d270a3d179ac6cabd7b24d7dfae1',
    'third_party/dawn/src/dawn/common/DynamicLib.cpp': '6405a92700b8b4fd6cb97a9deb8a7b8e953a5f1af4336cb5e0a2269977066315',
    'third_party/dawn/src/dawn/common/SystemUtils.cpp': '7574fcea83b3521c1d2ae789cae47952925bfcf77417ab3de5f7cdf874c6f2c7',
    'third_party/dawn/src/dawn/common/SystemHandle.h': '6ec71a7796ebedeedf69d9b51828162a4cb014882068ab473473cc0feab08985',
    'third_party/dawn/src/dawn/common/SystemHandle.cpp': '36a1dfbe024309acdfd0311b46f60b6ff1c8b4254b1777158315a48084fa9774',
    'ui/menus/BUILD.gn': '4085f2e495bb1e385918a2e01b652b30651a550e6af4fe3d9d957ba3f30bb472',
    'ui/base/unowned_user_data/BUILD.gn': '4b68d447db5ca38c6220c30533d6b1db547a6cd43b0fb50cf971f5dbea389895',
    'third_party/nasm/nasm_assemble.gni': 'f19475e0317263e15b6c028732f2638d1e024046931fdb55434ec73f7e73992d',
    'tools/grit/grit_args.gni': 'e3e7b4cf51aea6caa044d59ddacf6b164c56eefe279e637e0712aa9fec318900',
    'tools/grit/grit/node/base.py': '7aa20d2fe8e428d1a29cf47db321a157771a173f2f92ff6c09d44c2968157f59',
    '.gn': '4a0276377f72c90c6a77bad46e6badf7b4e1a123f87adccd8bc6e61d05f70ecd',
    'build/config/rust.gni': 'a9751813e83b350f8d4ffafc595bcb7b8170ee6696fec4ed2155790dde167fce',
    'build/config/compiler/BUILD.gn': '5fd8410dce79947067a656feb24d150bb838269f3f934f3028b00a62fc56b859',
    'build/rust/gni_impl/BUILD.gn': '1106bd62a03288e3220820b1f79bbeee19eee062d5e1c34bb5fa604df1a3c6a9',
    'build/rust/gni_impl/rustc_print_cfg.py': '1a08afbd6405fa3a5a01abf7f52472f1cfc481b4908eeee872ed11f9ee1df102',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/alloc/mod.rs': '605fba85756be94fdc13f40823daa82ec29b493c102564fbe36a34a3414314e1',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/io/error/mod.rs': '51d30b4298f3bdba90b02936a7b4bc9891266fe8248675fe300222e24ae915e6',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/thread_local/mod.rs': 'b8e407313093a89008fd0fbdd4805f2da8c7bb7711f0d36c03d858d6f0de5e4a',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/random/mod.rs': 'fcd278c7ca27edd3846098bb362423da1f013da49933159e66d0f77fd68cc540',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/futex/mod.rs': '8edeb158d1fa1e7e9a786309684f323275e2c13b22fe974345378809012fe0cc',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/mutex/mod.rs': 'a58c3f5605de2ec78f6b621fa1138042cc40088cf43e21694ab08129b47b1470',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/condvar/mod.rs': '4738e89e6ae37d1843dbf66e3ebbf51f74b375e8199af8965288aff3255f41ca',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/once/mod.rs': '03fa76d79a5c65651bd627ffb2ec41f00afaccd9e0f81a48fa76201e192ce670',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/rwlock/mod.rs': '800959dcab669de71206b5492b81deb2765a183d323e2807daefee35fa00ae19',
    'build/rust/std/rules/BUILD.gn': 'aec704ffc0528f5c10b237fc531bf6bf6bbdc848b91beb84de2a03273ee6f795',
    'build/rust/std/BUILD.gn.hbs': 'ab89582c8238efdfc0d974e6bfbd61e384e3a3e3e7a5b4ca3376aeba0aeade05',
    'build/rust/std/gnrt_config.toml': 'caf9a486faf41fe422c774d155b31707ac2300a78fc823b0ae30e942fe73b0e3',
}

ADDITIONS = {
    'third_party/perfetto/src/base/paged_memory_gtos.cc': 'perfetto/paged_memory_gtos.cc',
    'build/config/gtos/include/process/vm_abi.h': 'sdk/process/vm_abi.h',
    'build/config/gtos/include/time.h': 'sdk/time.h',
    'third_party/perfetto/src/base/ctrl_c_handler_gtos.cc': 'perfetto/ctrl_c_handler_gtos.cc',
    'build/config/gtos/include/__external_threading': 'sdk/__external_threading',
    'build/config/gtos/include/inttypes.h': 'sdk/inttypes.h',
    'build/config/gtos/include/llvm-libc-macros/inttypes-macros.h': 'sdk/llvm-libc-macros/inttypes-macros.h',
    'build/config/gtos/include/llvm-libc-types/imaxdiv_t.h': 'sdk/llvm-libc-types/imaxdiv_t.h',
    'build/config/gtos/include/sys/types.h': 'sdk/sys/types.h',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/alloc/gtos.rs': 'rust/alloc.rs',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/io/error/gtos.rs': 'rust/error.rs',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/thread_local/key/gtos.rs': 'rust/tls.rs',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/random/gtos.rs': 'rust/random.rs',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/futex/gtos.rs': 'rust/futex.rs',
}


def private_target(view, rel):
    parent = candidate.materialize_parents(view, rel)
    if parent.resolve() != view and view not in parent.resolve().parents:
        raise SystemExit('platform parent escaped the private view: ' + str(parent))
    return parent / Path(rel).name


def apply(view):
    view = view.resolve()
    if candidate.under_shared(view) or not (view / '.gn').is_file():
        raise SystemExit('expected an existing private composed GN view: ' + str(view))
    receipt = view / 'gtos-platform.json'
    if receipt.exists():
        raise SystemExit('refusing to overwrite this platform generation: ' + str(receipt))
    for rel, expected in INPUTS.items():
        if candidate.digest(view / rel) != expected:
            raise SystemExit('platform input changed: ' + rel)
    for rel, source in ADDITIONS.items():
        target = view / rel
        if target.exists() or target.is_symlink():
            raise SystemExit('refusing to overwrite platform addition: ' + rel)
        if not (HERE / source).is_file():
            raise SystemExit('platform addition is missing: ' + source)
    for rel in INPUTS:
        patch = HERE / 'patches' / candidate.patch_name(rel)
        subprocess.run(['/usr/bin/patch', '--dry-run', '--batch', '--fuzz=0',
                        '--input', str(patch), str((view / rel).resolve())], check=True)
    applied = []
    for rel, expected in INPUTS.items():
        target = private_target(view, rel)
        if target.is_symlink():
            origin = target.resolve()
            target.unlink()
            shutil.copyfile(origin, target)
        patch = HERE / 'patches' / candidate.patch_name(rel)
        subprocess.run(['/usr/bin/patch', '--batch', '--fuzz=0', '--input', str(patch), str(target)], check=True)
        applied.append({'path': rel, 'origin_sha256': expected,
                        'patch_sha256': candidate.digest(patch), 'sha256': candidate.digest(target)})
    added = []
    for rel, source in ADDITIONS.items():
        target = private_target(view, rel)
        shutil.copyfile(HERE / source, target)
        added.append({'path': rel, 'source': source, 'sha256': candidate.digest(target)})
    with receipt.open('x') as handle:
        json.dump({'files': applied, 'additions': added}, handle, indent=2)
        handle.write('\n')
    print(json.dumps({'view': str(view), 'applied': applied, 'additions': added}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('view', type=Path)
    apply(parser.parse_args().view)
