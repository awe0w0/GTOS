#!/usr/bin/env python3
"""Apply GTOS platform changes only to a previously composed private view."""
import argparse
import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('candidate', HERE.parent / 'port-candidate-r20/candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)

INPUTS = {
    'third_party/webrtc/rtc_base/BUILD.gn': 'be6bda98f1ffb36893fb741114525a692291e230f65830bf354ba7b4f5092cc4',
    'third_party/webrtc/rtc_base/checks.cc': '047a90b51b374d0f79c2066d5bdb387afde3df277c917a6217ccd9141e66cf90',
    'third_party/webrtc/rtc_base/platform_thread.cc': '37b5899dd0368145242ce251242ecd140bb37208a6bf00a6fcb9daea5d740e83',
    'third_party/webrtc/rtc_base/platform_thread.h': 'dfcdd03149d9481d924c0b47907b768c4878975d8ec430c7a3fcd3c4c82570e3',
    'third_party/webrtc/rtc_base/platform_thread_types.cc': 'c3f57298b272df5a1e130f0f7e29efe1df8109c50eab642f4066b2639f77b784',
    'third_party/webrtc/rtc_base/platform_thread_types.h': 'e4c32b5308c63e9866221d1a39e15fae4430c6431ca97c1327b9c48391959086',
    'third_party/webrtc/rtc_base/time_utils.cc': '1d1583c421ace18794a935d6e9eba61b55a346dc4be9c4aa91d28dba77be7f71',
    'third_party/libaom/BUILD.gn': '93f98c8f8e9fa67a89afb3d057882850396fb8b3f39528ecce9b833dc2796475',
    'third_party/libaom/source/libaom/aom_ports/aom_once.h': '085b60d0cb6c1e9058ba7dce15099f51c69ec2ea1deb8c70237890242ae6f601',
    'third_party/libvpx/BUILD.gn': '3f0f12f30428d469aaf0bc0a3df02b9c713058867ce1b0e97ba8cbae640d25b5',
    'third_party/libvpx/source/libvpx/vpx_ports/vpx_once.h': '483f46db1243752050eea0fd7868a6ce577276f3ed0df626848c03d7b3fc3de4',
    'content/shell/BUILD.gn': '8b952574b939aa81d0dba0113206efa708b3f1c3718ef874aadc2a7b2a4eb13e',
    'content/shell/app/shell_main.cc': 'b14da473f368a9a7bdd64e698573f67ef43fa791ee3e58e70eef98e71a4b66af',
    'net/socket/socket_descriptor.h': '586aa449a4980f398878279392c32d7cc80c1768e55a7faa19682578b3de4686',
    'ui/events/keycodes/keyboard_codes.h': '1a358e282acc6fd8bbb14bd68c159d8b4149b80cef8f18117c84e4b19419e508',
    'ui/gfx/gpu_fence_handle.h': '2a497f9a27c760499947d65fe1dd026b91b70efda58223652e79b8b1f4cc4231',
    'ui/gfx/gpu_fence_handle.cc': 'c35171057ec4b94e1cf57bd3bbf378f4e6efba7a8b76c48c48bc36846fa12133',
    'net/base/ip_address.h': '7f9aaa37992dc7efb49d1769bb00c111904458a928e45c3c0e8bd433b3eea704',
    'net/base/sockaddr_storage.h': 'f11db8dd752961a5f65d21e6477d9e3b1f29f526a5097bbc09528a87ab7ff192',
    'mojo/public/cpp/platform/platform_handle.h': '2f7d3a39ed4211ef714aa3ab99e320bc2631743ad78d293d52ba72d1f8dd7a70',
    'mojo/public/cpp/platform/platform_handle.cc': '739384bedeff27167d0ded8c0cda533ad5debd4da4ccc136af979b3634bf63eb',
    'mojo/public/c/system/platform_handle.h': '1a1fbb42eed7bb6119ff0a132acdd8beb133535e8189b57c78f69419ef764f1f',
    'ui/gfx/native_ui_types.h': '24a0b53bf57294c1703d09d5c56a8d4487098edcee496425b21387372747375d',
    'v8/tools/run.py': '76c9bd9c3bdae23ab13572056b9d320f3b2298e015bd3f5f1c7bf6265c0be67a',
    'third_party/dawn/tools/generate-sources-gn.py': '14f602dc2067f3fa987e69d6f3a11c2cd9c57292caa5086560e9865dae5972e5',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/os/mod.rs': 'cb29e8073290ba65979e7c7722d1eec3b96889e34f93a15c97b852f749576ee8',
    'base/files/file_path.rs': 'eee1e0a0b1ad69341a8c8cb5b4e9f8f497d733c137aa3f7dbde68fcac09a109b',
    'v8/src/base/platform/semaphore.h': '6aaab37e3ba74c5ecaa867b3403e2ad676457f5731bf0de4c2e41bd9d9ad49ba',
    'v8/src/base/platform/memory.h': '056e2e56ee4c1a93322f71031cd172e274ceb1456f21ef8bd1319a06ce3afbe4',
    'v8/BUILD.gn': '7d5f28ecf364619c6ae191c575ee8d4f07015be64c370aca8f091ee036167aed',
    'v8/tools/metagen/compile_flags.py': 'ee9851b7c526197be49c912bea9b42f7af27e107ad868087ee4af76b5a7de3a1',
    'v8/tools/metagen/metagen.py': '6eb54d03b2d438ec7c1c87475bcee603357064ee8d3a9b11a1bbde5c030c4578',
    'components/safe_browsing/content/common/proto/download_file_types.proto': 'ace4dacd90bb291ba4c22f23e6461275579ccedc0891f0aa82d4390d3ae61d6b',
    'components/safe_browsing/content/resources/gen_file_type_proto.py': '5c2687d818f10d1161feb273311dd053ee439ea6824e394c1a7169dbe5d35438',
    'components/safe_browsing/content/resources/BUILD.gn': 'e99503fa6d78fc547d5b1e8f0b7be1437c2d84979da6ed4d84be581dff3a70d8',
    'components/variations/proto/study.proto': 'bafeff8e3ba1a19752c39e50f15bae2b982e00a2780232c4d02c35758a4f6429',
    'tools/variations/fieldtrial_to_struct.py': 'c1ef0b90547489767453a76512ea8f959f1d93c3afbd895604a0f86a3463290d',
    'testing/variations/PRESUBMIT.py': '56efb93e5199111cdff6fbdc8cc42c3570aad63ef82ab98aecd888447fb25326',
    'components/variations/client_filterable_state.cc': 'ab33f0956fdbc3a2292e80fe031c1e67db96c9224cb8c57c7f97943c8f5368cc',
    'components/policy/tools/generate_policy_source.py': '5c98abe8c3109e0f87f3857b138f3b0b40b07e472e9e8c2aa0582a1773eed1c4',
    'build/rust/gni_impl/libclang_args.gni': '6afd3aae3f9873fff0e0944514739151b4856fc19624f06b9f693cb5500b982c',
    'third_party/rust/serde_json_lenient/v0_2/wrapper/BUILD.gn': '70823f6f3518b5d2c7505a59f09be7230bbe95c7c37a9949a00288fdac0eb075',
    'third_party/rust/serde_json_lenient/v0_2/wrapper/lib.rs': '0e00086fa7eed44990d09978fd481167db6044ac1c5d2c10da65c15e214c2e26',
    'base/threading/thread_local_storage.h': '1cc76c701f11524702983a8e75dc13b3d612d61c44484ec767e4d7eff562161a',
    'base/threading/thread_local_storage.cc': 'fe46907d1039701200449bef53eff25c69a268f9973d99e4b8e866440da31c5f',
    'base/message_loop/message_pump_for_ui.h': '5d7ed92b6ed0e6ccc983036abd015bbbd6810bdfaf216b0a62d6cfbf4c5269a0',
    'base/message_loop/message_pump_for_io.h': 'cab2ea5e73e0c88d5202d70359edff7983a7af86d94ad1780d34928abda0ef66',
    'base/message_loop/message_pump.cc': 'fce438071a0cd162eda5074ea76211c8c6d44994fdca9269fb8e7589dcc3ef15',
    'base/task/current_thread.h': '341f323a2c728c9e082d815b40c2f9d7e84edde74dda7ddaa432fa670be1b399',
    'base/task/current_thread.cc': '44d617d7421c8585b66c462ea09687005ba0e7692ef9aebdb7019267c9149e92',
    'base/process/process_metrics.h': 'bfd9dcd28d394d0969d970182c666cdc4b68eaeb7a143a91ef36e23fbb93f354',
    'base/memory/platform_shared_memory_handle.h': '0b1d68d9ddb4fb5f45e2dcf65f2d8dfd6719f2f7d59560a72d1965df438acd66',
    'base/memory/platform_shared_memory_region.h': '393d93629641f88d42ad0d6b444bbdbf33379ae9c3a469357e7b5cd41427feeb',
    'base/logging/logging_settings.h': '3aad1a35a2afbd5eb008575dcbb705582d21a85fc17b2f8bef7a6a96e03ce760',
    'base/check.cc': 'cead9bd6bad693693d767b42c9f144f369ee2af7ab064f9e5c9a010f76f627eb',
    'base/scoped_clear_last_error.h': '82b51b1001fe585c64a558c285307a071830f376bb89a633e73f64879f6d557d',
    'base/logging.cc': 'f8af905b1e6e07532f5ab45f037bec61a323dc77bbc8e5e1d222514371ee3013',
    'base/synchronization/waitable_event.h': '3cd281fe0a9a7d9d7865c974d8e6d2ca0e9c7ec27aa319d6ec7f2644b1011786',
    'base/synchronization/waitable_event.cc': '519fe044e87ed743f523f600bb82e5f1c2eef0d62ab6059a049520604579b880',
    'base/time/time.h': '3d856f595a15876885d1309e6831fd60e49e66cd2fbd583f1c6ddaac8bce72bf',
    'base/files/file_path.cc': '3543354e60c0618b5ceca6316b9d180e0c0342f940d8e9664f9d44f852f87e12',
    'base/environment.h': '24138d1be0637b3764d6ca36b0f5f03a1efddd55cd03fac66c90670ea8803f9d',
    'base/environment.cc': '4e2a46b4882a7eabbc791f6dda0130d6b5e6c068524e78e85d5b08662165a4d5',
    'base/files/platform_file.h': 'f3a069e7e394ff2cd6888e031c886823749456dcb81ee74035f72ddbc8b2d64f',
    'base/files/scoped_file.h': '4df827293317e03713e7350adf0dcefa19d38de24e21654a7f8ad22cf4528a9b',
    'base/files/scoped_file.cc': '40a8ce3f07aa4a8b1b2cf4f54b925766a0bbe13ec2ea0066368550ef7b06775b',
    'base/files/file.cc': 'c8f0f4a41efcd427c62d60c7a525d12d82582eb3c30e602bcc35d074b0de0283',
    'base/debug/task_trace.cc': '64b9cfcd66286c149d6c8be0cea1e0d61324311e1eb642d06948383078b98be0',
    'base/BUILD.gn': '0b2bf323531fe088b21c5f30d808d4637e2e9bb20787216d2b7d3176c8773994',
    'base/process/process_handle.h': '1f1b60073b3e6bbb8ae61adb11cdb2c06fd596f017878c0db062131efd43cb04',
    'base/process/process_handle.cc': '3906c5157d4fb97f7580ab1f1516a003a68bd820d1b4ac58c5e41ff6d77d8fb4',
    'base/threading/platform_thread_ref.h': '192874921e6a8bae9eaf8f3366df78edbf6f6f118ca6fd11553a7d4dd6fa6856',
    'base/threading/platform_thread.h': '971058048523875f592ea44c26beafbf91a1eec0a716f60be40fee3325833092',
    'base/synchronization/lock.h': '6e31288f216b837400847b1ce217dcd8088e97033234ca590bb5615f84b08ac2',
    'base/synchronization/lock_impl.h': 'e139551d67a0e02ba6bd739849b7b3141de01700f3ab9d3b428e739456711758',
    'base/strings/string_util.h': '99c7863b952f941cf98f60d6e93d9ea671000ff70b1b4f63fe86c9eb7e156122',
    'base/strings/string_util_posix.h': 'efb87b972f035030aab5d42b9dea120d88b7649c24726df80ce0bb60a5f4c9ac',
    'base/command_line.h': '973770ed8317caccc46dd194a5f705c0721a8b6a5fbc31149cc44ab7dc94645d',
    'base/command_line.cc': '05eb1d1afc5f24dfb1d3c2ee0f709a39f37643f86dd7a9cac3ff2a8d7699bb53',
    'base/files/file_path.h': 'c8e135db3b184e838d5e7413796d8f5f32aef492ca16e347da0e100783526b0c',
    'third_party/perfetto/src/tracing/core/shared_memory_abi.cc': '3a8ae2e14f8c7c9822b86cdd0b0a570d90187be2dacbcd457e3ce1e703fec258',
    'third_party/perfetto/src/tracing/console_interceptor.cc': 'c2b498443ee4517b8920204418ff0c5e96ec65dccc467b0e68b35b92523fc933',
    'base/logging/rust_logger/lib.rs': '031a4fa7b581969225e57815d577563eec8405fac48759444134c4f214525473',
    'base/logging/rust_logger/BUILD.gn': '5eabbef9a199f97ddada03efbf80eaba0565fe793c426a2aa5c10311e5b3be1f',
    'third_party/zlib/BUILD.gn': 'ca39854b5338812359318fae7524ed6efd7da9537cae0e66f8d407f5d3c92815',
    'third_party/perfetto/include/perfetto/base/proc_utils.h': 'a6bf8a3abfe2ef502c5cdd091ba67755d0adbae7e38c955150bf024ccc186626',
    'third_party/perfetto/src/base/clock_snapshots.cc': 'd83f631879ddf0671197963a09cfb9f068d48a310da842c62b72fc2777db758f',
    'third_party/perfetto/gn/perfetto.gni': 'fdf2c3a422206ae2dc20d8d08c20c6344cf719a96642d6a24f7ca441560321c9',
    'third_party/rust/log/v0_4/BUILD.gn': '5a8d0d9c09965fdc12d83af7bb4a45cb6ce72c24fd45651d208568bf4a04e1cd',
    'third_party/rust/chromium_crates_io/vendor/log-v0_4/src/lib.rs': 'f6633a72e77e5039ef767189116986ba12a57e43304b40eebb466959e03b9a92',
    'third_party/rust/cxx/v1/BUILD.gn': 'f8ad2dffb860b94111026a04d255cbbe3e0f86a4f12f8e1b5816923cba18985d',
    'third_party/rust/chromium_crates_io/vendor/cxx-v1/src/lib.rs': '689ed1c3482c683680ec644fc760601efaa944b78b703c26eafac30f646f63f1',
    'third_party/rust/serde_json_lenient/v0_2/BUILD.gn': 'b620aafb25480fc69405ceeb40668333f8c3611d98146b686200a1fe4aaa20ea',
    'third_party/rust/chromium_crates_io/vendor/serde_json_lenient-v0_2/src/lib.rs': '59323bf9fa9046e654104b6080f1e3622f8c7f2ead27813e1ac2bed4202aea9d',
    'third_party/boringssl/BUILD.gn': '63efa6ff4bc649e7b4cf161a72ac747719ff697a6ae933742efb82cbc06bc3f5',
    'third_party/boringssl/src/crypto/internal.h': '54cd27bea4b886a6844d5df6964eece3cd0c965201df965fd9850b89940ae1bf',
    'third_party/boringssl/src/crypto/rand/internal.h': '4bd3f23a37f25a5c6db6314f62b84fe61070ac614c66acb3efd77ca4b2322cda',
    'third_party/boringssl/src/ssl/ssl_lib.cc': '03aecd510a995c8e1f7557a9dc44b05f4a48f21f3cf210da19f140800cbb856c',
    'third_party/rust/chromium_crates_io/vendor/serde_core-v1/src/crate_root.rs': '0fa71194671383f30941480cd7de9b1d008ace0d05c3d1cf3c317ed5c9639f40',
    'third_party/rust/chromium_crates_io/vendor/serde-v1/src/core/crate_root.rs': '0fa71194671383f30941480cd7de9b1d008ace0d05c3d1cf3c317ed5c9639f40',
    'third_party/rust/serde_core/v1/BUILD.gn': '132bdf871494a41231173f20e582b35e5533e2a0f70c7e06c1c0e18bda2cd5b3',
    'third_party/rust/serde/v1/BUILD.gn': '2ab9bdc976b4b26a285462846bfd047037fe89ef4be519ce343dfe57186eb0b8',
    'third_party/rust/foldhash/v0_2/BUILD.gn': 'b21b557e7df96fb668a9327c53db2a45178792143cedac5d827cad7e1452ec24',
    'third_party/rust/memchr/v2/BUILD.gn': 'be74b1b18659b8fed358eea3939b58d08bfa606c8cfac43d15f7c94f9303cfb2',
    'third_party/rust/chromium_crates_io/vendor/foldhash-v0_2/src/lib.rs': '777319e7f2919bede856e13594169cfc4bdbaf2e541463da58c86e7e832dc45f',
    'third_party/rust/chromium_crates_io/vendor/memchr-v2/src/lib.rs': '614f778a41e88a29ea0ceb8e92c839dbb6b5a61c967f8bfd962975e18f932c71',
    'build/rust/chromium_prelude/chromium_prelude.rs': '602f75cfa69277bfa5fb24fd55ce18e17f6912a6b03af615d1b0dff73a8bab86',
    'base/logging/BUILD.gn': '46404ae6aaf11d348966b0f35473c4c72d77a9c20b2a23eeb2cb04ea0810258a',
    'base/logging.h': '85360d4e33f2645ed9c3b4e0a3302aec06f538631c42514e3ccd704fcfc1461f',
    'third_party/rust/chromium_crates_io/vendor/serde_core-v1/src/lib.rs': '67cb60b55dbb2b4ecdb813c871d7f89e675ff255641445f60d33d354a054f704',
    'third_party/rust/chromium_crates_io/vendor/serde-v1/src/lib.rs': '3ebe097aa377f547eb336e44a7a999718726b32804c98c81c9f124cfc796cf7c',
    'tools/protoc_wrapper/protoc-gen-ts_proto.py': '5c47d11dd1d162e0e4b2f1c2e9ffee92d01692467379c763f6bb340c01d557c1',
    'base/allocator/partition_allocator/src/partition_alloc/stack/stack.cc': 'e046a5e98d65df593b829ab4b1541f9732c6aa55e6934f70041f9227207526a4',
    'build_overrides/partition_alloc.gni': 'eda28a6578ab3ac16cc9806198ef935e69c50cd8fca27f29a19217549b181709',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/time/time.h': '98d6bf634c5fdaf6505c48df393dbe432939660f92e359286288f2d6cf450449',
    'base/allocator/partition_allocator/src/partition_alloc/address_space_randomization.h': 'e188fdb4cdb50f4e79d035d4e6312f52fe756a4c8b3b1a1ed02abb30a2beafff',
    'base/allocator/partition_allocator/src/partition_alloc/page_allocator.cc': '696169437e262abe80a6fec317fb3a95c7fbff95497d0fccb200fcfd695b9d27',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_config.h': 'a92c159fa2d3917ff2709215a2b74088da2b0a738fde626a62cf100f8345b31f',
    'base/allocator/partition_allocator/src/partition_alloc/partition_tls.h': '9630c42d6deb4fe26269c37d0799e412ebad33ca5be3247e1e309d008ecc63ff',
    'base/allocator/partition_allocator/src/partition_alloc/thread_cache.cc': '93b59c06ec9e04e638720ef7a622f97253fa63417f15fa1d6f737a61720804b9',
    'base/allocator/partition_allocator/src/partition_alloc/spinning_mutex.cc': '2f670566b168d90dfb273ee0dfac94ef7c27a8670d1d38cf877f784df917f362',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc': '498b80b48fe53f8812803873cdfddb5d3dee98a42c8b345fc47ca401de1b2bfd',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc': 'cc5e597f569b1f028e50daf143f936042600be7cfe32774953ef157cd21a4c6b',
    'third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.cc': 'aca7667bf07c59daeac823edbc7464550400cdcc95eeb2cc41a91319deb533b9',
    'third_party/abseil-cpp/absl/debugging/internal/elf_mem_image.h': '2b383d756add31277f5420db96474ea7944f0290fd33d6ba8482edc82be91fb2',
    'third_party/abseil-cpp/absl/debugging/failure_signal_handler.cc': '8b03e80cf92e2b66f54790f150ae2ef4ff791c18bd2129ad79191f03f9500040',
    'third_party/abseil-cpp/absl/synchronization/internal/pthread_waiter.h': 'b3ec00074fd91bcedfe3933f2e0de81f6e42f63b313bc5aee914b9cdf2f61a72',
    'third_party/abseil-cpp/absl/synchronization/mutex.cc': '4d8dbee70615d5761d5a6641436060ed3e7b1fbd8424f1565eef7009f9c0057b',
    'third_party/abseil-cpp/absl/log/internal/config.h': 'e5e06a681876c7d42d134f3d3e525313d32309bcce1f831a4705652b4ad98a52',
    'third_party/abseil-cpp/absl/base/config.h': 'd2cdcd88b00c6a64dd7a5a4bd1e0560bede511991b3126865f6bb863e7249391',
    'third_party/abseil-cpp/absl/base/internal/thread_identity.h': 'd200359ea92f63f53e7210de08d9cc53dd69769fbe8323abaff8c1a8f24e2d84',
    'third_party/abseil-cpp/absl/base/internal/spinlock_wait.cc': 'f8bb5948992bf0043e8b0a33aaf9b7e12231c28ad7790f33e7854927b5cd0188',
    'third_party/abseil-cpp/absl/base/internal/sysinfo.h': '004b91b692009ba4740f31ff5568c367d479b84637be92724a8baa72d95dec44',
    'third_party/abseil-cpp/absl/base/internal/sysinfo.cc': 'a6c067268aea7a5cf166c9ac0a6500674701f107d8a2293d7a86011ea4dee36d',
    'third_party/abseil-cpp/absl/base/internal/unscaledcycleclock_config.h': '8f779e52b7b4839ea707d447866a87f67a3aa840c91aab1499cbf88679a10814',
    'third_party/abseil-cpp/absl/base/BUILD.gn': 'f6c580632d447ed1a46ce3026e0182604381d5fe1c47d3b3a49ec8e55b04f114',
    'base/allocator/partition_allocator/src/partition_alloc/BUILD.gn': '72691b962697b3e958162707408a59bf426031159123a7c9421a7a63263446be',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/process/process_handle.h': '64ac91919d6a049f11b5ef5bb5063b0cd4a9a4fbf70794d45eb83b3a3787e49f',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/threading/platform_thread.h': '45e4fbeec450b72ea0633e63e7f17b8feaab6d73f574583d7f451915ce71b97e',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/threading/platform_thread_ref.h': 'a8a30eda1ad6fcf995ede35d5355d9b111aacd1b138814dc099fd1d53c89ea95',
    'build/rust/allocator/BUILD.gn': 'ba4af4728989a16c5080476537ee4da67e92d1ab301d53b0e811fa1c5a21e094',
    'build/rust/allocator/lib.rs': '03241a012540b8856a5de5eef365aa13cf28e0630730df3b3323ef88184e715a',
    'base/allocator/partition_allocator/src/partition_alloc/build_config.h': 'a0bc70bbb196d756c14b24d9da928301273a09dddcd84f2879cec475c5675811',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/scoped_clear_last_error.h': '6659b2ff66f9b814e6a5c3ee1f85cec4ebdc84ff313637b799210f5503ab96e4',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/log_message.h': '1152b52ddf3a7b5389794fd9ed0dfdbe627750309df5ed6e6a020f8681f85633',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/logging.h': 'cc20bbf50afc0242d0ec5ed40de06b5406f4e89dd4d5153060817876a3dfc9f6',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/check.cc': 'ae2037acbc14537fd893ebc7d8ea912c05aa2edf835844fb115c12c73a561b4c',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/log_message.cc': 'a2f26258b13278e238c8b2169fddf172fdd3655851f09f927f325b25531d6295',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/logging.cc': '21b5cd6712598a793198701d3d3cb475d98165fd88be4e12a5c6afefac820952',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/strings/safe_sprintf.h': '3b0bf924cfc2fcf2b30c928af0369a5f58f8919b8a33cced87276bdbd4c06c8e',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/strings/cstring_builder.cc': '4be8043d3702f4dfaa82b2c94ffb94fe93fa485afbf5898bea91a95e24ce2e66',
    'third_party/perfetto/src/base/utils.cc': 'f9d4a8b518bb0c5b78f8b0426791c7767a1bf856f17dedd0d57401327ec4e34a',
    'third_party/perfetto/src/base/time.cc': 'e0f427cb33997802685f58b813c2648cbd70ac009c7fcd21f81d8841ed37d0c6',
    'third_party/perfetto/src/base/lock_free_task_runner.cc': 'ee41b877884001efc183f9d821f6daa67bdde52bd1f178520be38d3cea6e8686',
    'third_party/perfetto/include/perfetto/ext/base/lock_free_task_runner.h': '0a4af2aaadcaeb2b4fee8a296f7657e528e77e96da6b6360b7484c8fd5f63574',
    'third_party/perfetto/src/base/unix_task_runner.cc': 'ad0b123c4ce28bc8d8ddf078247359158bad89e903f298a2a1fd9e23f54ab066',
    'third_party/perfetto/include/perfetto/ext/base/unix_task_runner.h': '2d6cd9c3a58702245af50403f431bb4450eeb5303c8679b154ab009f4ae89cab',
    'third_party/perfetto/src/base/file_utils.cc': 'ed52e1b02be69f57b0ed098fe8a294fb4b4798ab79f6962c267ac83a3ecdedc7',
    'third_party/perfetto/src/base/pipe.cc': '483438cf41402586b90f6940a696a84724276c4e8846a271d85eb8457332ca13',
    'third_party/perfetto/include/perfetto/ext/base/event_fd.h': '8f54d6aaf7af97af9a2cd738e543bfa34b956d6dda6fad619fef5dcd4d17e614',
    'third_party/perfetto/src/base/atomic_file.cc': 'd9016fb57418694f5c717e4aab93deabb8388bdb19de57ed75f685a3e0dff9ae',
    'third_party/perfetto/src/base/progress_reporter.cc': '4d46c39fd43736f95200c86ac8f707d9f16e718c34e57f081d33eff00ea668eb',
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
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/test/src/lib.rs': '3e2037f170b3e23813b9a24de1f2826d8cf01aa3c9c65c41d67cfadee4adb8f6',
    'build/rust/std/rules/BUILD.gn': 'aec704ffc0528f5c10b237fc531bf6bf6bbdc848b91beb84de2a03273ee6f795',
    'build/rust/std/BUILD.gn.hbs': 'ab89582c8238efdfc0d974e6bfbd61e384e3a3e3e7a5b4ca3376aeba0aeade05',
    'build/rust/std/gnrt_config.toml': 'caf9a486faf41fe422c774d155b31707ac2300a78fc823b0ae30e942fe73b0e3',
}

ADDITIONS = {
    'v8/tools/gtos_isolate_diagnostic/runtime/thread-core.cc': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/runtime/thread-core.cc',
    'build/config/gtos/include/process/environment_abi.h': '../../../include/process/environment_abi.h',
    'tools/gtos_native_sdk/environment/environment.cc': '../../native_environment/sdk/environment.cc',
    'tools/gtos_native_sdk/environment/environment.h': '../../native_environment/sdk/environment.h',
    'tools/gtos_native_sdk/environment/config/app.h': '../../native_environment/sdk/config/app.h',
    'tools/gtos_native_sdk/environment/config/gtos/app.h': '../../native_environment/sdk/config/gtos/app.h',
    'tools/gtos_native_sdk/native-thread.h': 'sdk/native-thread.h',
    'tools/gtos_native_sdk/native-thread.cc': 'sdk/native-thread.cc',
    'build/config/gtos/include/math.h': 'sdk/math.h',
    'build/config/gtos/runtime/tanh.cc': 'sdk/tanh.cc',
    'build/config/gtos/include/memory.h': 'sdk/memory.h',
    'third_party/libaom/source/config/gtos/generic/config/aom_config.c': 'libaom/aom_config.c',
    'third_party/libaom/source/config/gtos/generic/config/aom_config.h': 'libaom/aom_config.h',
    'third_party/libaom/source/config/gtos/generic/config/aom_dsp_rtcd.h': '//third_party/libaom/source/config/linux/generic/config/aom_dsp_rtcd.h',
    'third_party/libaom/source/config/gtos/generic/config/aom_scale_rtcd.h': '//third_party/libaom/source/config/linux/generic/config/aom_scale_rtcd.h',
    'third_party/libaom/source/config/gtos/generic/config/av1_rtcd.h': '//third_party/libaom/source/config/linux/generic/config/av1_rtcd.h',
    'build/config/gtos/include/setjmp.h': 'sdk/setjmp.h',
    'build/config/gtos/include/gtos/once.h': 'sdk/once.h',
    'build/config/gtos/runtime/BUILD.gn': 'sdk/codec-runtime.gn',
    'build/config/gtos/runtime/codec_once.cc': 'sdk/codec_once.cc',
    'build/config/gtos/runtime/setjmp_i386.S': 'sdk/setjmp_i386.S',
    'third_party/libvpx/source/config/gtos/generic/vpx_config.h': 'libvpx/vpx_config.h',
    'third_party/libvpx/source/config/gtos/generic/vpx_config.c': 'libvpx/vpx_config.c',
    'third_party/libvpx/source/config/gtos/generic/vp8_rtcd.h': '//third_party/libvpx/source/config/linux/generic/vp8_rtcd.h',
    'third_party/libvpx/source/config/gtos/generic/vp9_rtcd.h': '//third_party/libvpx/source/config/linux/generic/vp9_rtcd.h',
    'third_party/libvpx/source/config/gtos/generic/vpx_dsp_rtcd.h': '//third_party/libvpx/source/config/linux/generic/vpx_dsp_rtcd.h',
    'third_party/libvpx/source/config/gtos/generic/vpx_scale_rtcd.h': '//third_party/libvpx/source/config/linux/generic/vpx_scale_rtcd.h',
    'content/shell/app/entry_gtos.cc': 'content/entry_gtos.cc',
    'content/shell/app/native-start.s': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/native-start.s',
    'content/shell/app/native.ld': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/native.ld',
    'build/config/gtos/include/llvm-libc-macros/stdint-macros.h': '../../native_v8_integration/sdk/llvm-libc-macros/stdint-macros.h',
    'build/config/gtos/include/llvm-libc-types/socklen_t.h': '../../native_v8_integration/sdk/llvm-libc-types/socklen_t.h',
    'build/config/gtos/include/llvm-libc-types/sa_family_t.h': '../../native_v8_integration/sdk/llvm-libc-types/sa_family_t.h',
    'build/config/gtos/include/llvm-libc-types/in_addr_t.h': '../../native_v8_integration/sdk/llvm-libc-types/in_addr_t.h',
    'build/config/gtos/include/llvm-libc-types/in_port_t.h': '../../native_v8_integration/sdk/llvm-libc-types/in_port_t.h',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_sockaddr.h',
    'build/config/gtos/include/llvm-libc-types/struct_in_addr.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_in_addr.h',
    'build/config/gtos/include/llvm-libc-types/struct_in6_addr.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_in6_addr.h',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_in.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_sockaddr_in.h',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_in6.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_sockaddr_in6.h',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_storage.h': '../../native_v8_integration/sdk/llvm-libc-types/struct_sockaddr_storage.h',
    'build/config/gtos/include/net/address_types.h': '../../../include/net/address_types.h',
    'build/config/gtos/include/sys/socket.h': '../../../include/sys/socket.h',
    'build/config/gtos/include/netinet/in.h': '../../../include/netinet/in.h',
    'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/os/gtos.rs': 'rust/gtos_os_ffi.rs',
    'v8/src/base/platform/gtos-native-api/abi.h': '../../native_v8_integration/overlay/src/base/platform/gtos-native-api/abi.h',
    'v8/src/base/platform/gtos-native-api/clock_abi.h': '../../native_v8_integration/overlay/src/base/platform/gtos-native-api/clock_abi.h',
    'v8/src/base/platform/gtos-native-api/info_abi.h': '../../native_v8_integration/overlay/src/base/platform/gtos-native-api/info_abi.h',
    'v8/src/base/platform/gtos-native-api/realtime_abi.h': '../../native_v8_integration/overlay/src/base/platform/gtos-native-api/realtime_abi.h',
    'v8/src/base/platform/gtos-native-services.h': '../../native_v8_integration/overlay/src/base/platform/gtos-native-services.h',
    'v8/src/base/platform/gtos-clock-bridge.h': '../../native_v8_integration/overlay/src/base/platform/gtos-clock-bridge.h',
    'v8/src/base/platform/gtos-clock-bridge.cc': '../../native_v8_integration/overlay/src/base/platform/gtos-clock-bridge.cc',
    'v8/tools/gtos_isolate_diagnostic/runtime/gtos-semaphore.cc': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/runtime/gtos-semaphore.cc',
    'v8/tools/gtos_isolate_diagnostic/runtime/thread-context.cc': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/runtime/thread-context.cc',
    'v8/tools/gtos_isolate_diagnostic/runtime/thread-context.h': '../../native_v8_integration/overlay/tools/gtos_isolate_diagnostic/runtime/thread-context.h',
    'base/threading/thread_local_storage_gtos.cc': 'base/thread_local_storage_gtos.cc',
    'build/config/gtos/include/process/thread_slot_abi.h': '../../../include/process/thread_slot_abi.h',
    'base/process/process_metrics_gtos.cc': 'base/process_metrics_gtos.cc',
    'build/config/gtos/include/process/system_memory_abi.h': '../../../include/process/system_memory_abi.h',
    'base/memory/platform_shared_memory_gtos.cc': 'base/platform_shared_memory_gtos.cc',
    'build/config/gtos/include/process/shared_memory_abi.h': '../../../include/process/shared_memory_abi.h',
    'base/synchronization/waitable_event_gtos.cc': 'base/waitable_event_gtos.cc',
    'base/time/time_now_gtos.cc': 'base/time_now_gtos.cc',
    'base/synchronization/lock_impl_gtos.cc': 'base/lock_impl_gtos.cc',
    'base/threading/platform_thread_gtos.cc': 'base/platform_thread_gtos.cc',
    'third_party/boringssl/src/crypto/thread_gtos.cc': '../../native_boringssl/overlay/third_party/boringssl/src/crypto/thread_gtos.cc',
    'third_party/boringssl/src/crypto/rand/gtos.cc': 'runtime/boringssl_entropy.cc',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/rand_util_gtos.cc': 'partition_alloc/rand_util_gtos.cc',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/time/time_now_gtos.cc': 'partition_alloc/time_now_gtos.cc',
    'base/allocator/partition_allocator/src/partition_alloc/page_allocator_internals_gtos.h': 'partition_alloc/page_allocator_internals_gtos.h',
    'third_party/abseil-cpp/absl/base/internal/spinlock_gtos.inc': 'abseil/spinlock_gtos.inc',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/process/process_handle_gtos.cc': '../../native_partition_platform/overlay/base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/process/process_handle_gtos.cc',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/threading/platform_thread_gtos.cc': '../../native_partition_platform/overlay/base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/threading/platform_thread_gtos.cc',
    'tools/gtos_native_sdk/native-services.h': '../../native_partition_platform/overlay/tools/gtos_native_sdk/native-services.h',
    'build/config/gtos/include/process/abi.h': 'sdk/process/abi.h',
    'third_party/perfetto/include/perfetto/ext/base/native_wait.h': 'perfetto/native_wait.h',
    'third_party/perfetto/src/base/native_wait_gtos.cc': 'perfetto/native_wait_gtos.cc',
    'third_party/perfetto/src/base/event_fd_gtos.cc': 'perfetto/event_fd_gtos.cc',
    'build/config/gtos/include/process/event_abi.h': 'sdk/process/event_abi.h',
    'third_party/perfetto/src/base/system_info_gtos.cc': 'perfetto/system_info_gtos.cc',
    'build/config/gtos/include/process/info_abi.h': 'sdk/process/info_abi.h',
    'third_party/perfetto/src/base/cpu_time_gtos.cc': 'perfetto/cpu_time_gtos.cc',
    'build/config/gtos/include/process/cpu_clock_abi.h': 'sdk/process/cpu_clock_abi.h',
    'build/config/gtos/include/process/clock_abi.h': 'sdk/process/clock_abi.h',
    'third_party/perfetto/src/base/paged_memory_gtos.cc': 'perfetto/paged_memory_gtos.cc',
    'build/config/gtos/include/process/vm_abi.h': 'sdk/process/vm_abi.h',
    'build/config/gtos/include/time.h': 'sdk/time.h',
    'build/config/gtos/include/sys/time.h': 'sdk/sys/time.h',
    'build/config/gtos/include/unistd.h': 'sdk/unistd.h',
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

# These provider bytes are pinned by the BoringSSL and PartitionAlloc source locks.
ADDITION_SHA256 = {
    'third_party/libaom/source/config/gtos/generic/config/aom_config.c': '4652780bd3ba3c0c76f37c57ccb7c4be1a4c044e048a568774920bba47349473',
    'third_party/libaom/source/config/gtos/generic/config/aom_config.h': '32fdc8b766b308e99b914f7fae3761bfd12950c7a529b25758dc1bc64080d8da',
    'third_party/libaom/source/config/gtos/generic/config/aom_dsp_rtcd.h': 'efc25489dc85b1aca8122d9a980e85b45a1b68dcdb8a1e00c6893c96ecd73a1e',
    'third_party/libaom/source/config/gtos/generic/config/aom_scale_rtcd.h': '16c294bebc7c8e685323f7c8ad06ab20ad079e003cb0a6b18c34d1bd907e4f64',
    'third_party/libaom/source/config/gtos/generic/config/av1_rtcd.h': 'adaba557502c501f52c7efc030cec6ee1e34e0a00690ffe34e85e75e86b0a32f',
    'third_party/libvpx/source/config/gtos/generic/vpx_config.h': '13a4c6c78c53def6ba2892e098d8854b9e20afefe916bad503a3e1cae33d9ec4',
    'third_party/libvpx/source/config/gtos/generic/vpx_config.c': 'a36950eaf7075cd62a7fe015dcb66b005bd422fd9f7dbeca0b0c245f39f4d1a5',
    'third_party/libvpx/source/config/gtos/generic/vp8_rtcd.h': '6b883c287ccef9dea38ec4e6965ab0c60068fbbaac0ad5c853ed7d1c92ac1092',
    'third_party/libvpx/source/config/gtos/generic/vp9_rtcd.h': '0c63b0563777861be50eaf0ae9e2a30e64dadba5fe6cfb2b23792467b4643e73',
    'third_party/libvpx/source/config/gtos/generic/vpx_dsp_rtcd.h': '022ac54cbd13c2bf3db86bffb80ce6611f36500da3942a4bffaafcdc6a4a58d7',
    'third_party/libvpx/source/config/gtos/generic/vpx_scale_rtcd.h': '5bd74357a41aea413e4b7f84b695eb3af094d794c112bcfbd614e505148d2a5f',
    'content/shell/app/native-start.s': '7f56a50ec84155e45d8e5a3ef4531b7c3de426b308a89eed599d5d1e8fd5e98f',
    'content/shell/app/native.ld': '6364feb8b9a5182a61014d73196d4be28b62a5e17dfaac5f494a935ce2da5eca',
    'build/config/gtos/include/llvm-libc-macros/stdint-macros.h': '2de52e163a2bf0251a5ebde3777dfa7b4cc39a3baa94b9830f4bef81b7a53f77',
    'build/config/gtos/include/llvm-libc-types/socklen_t.h': 'b690e8848e4ef3bcd8e1428adee44014b33eac47992f07d22d62f7dd3eb5c162',
    'build/config/gtos/include/llvm-libc-types/sa_family_t.h': 'e4af7bd87fd16b03bb98c80da78efe99660ff937867594e54b883797d5911ecf',
    'build/config/gtos/include/llvm-libc-types/in_addr_t.h': '7e80c984d352ef90ffb49564b9a2a77393ee13591f96273ecd3f7c7e6fffc77a',
    'build/config/gtos/include/llvm-libc-types/in_port_t.h': 'fce4063553570a77c0f99dae2baada9128e975a2284248ed7bdaf655a6b3e5ee',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr.h': '0c4099661ac5943b18eef5b75f12264c5c7cd04d37262aa3e4a5d5484d5a779e',
    'build/config/gtos/include/llvm-libc-types/struct_in_addr.h': 'ceffd28e45fef6d3ae5eeb5eb49eb0a56a9980f27d5702d214d04d1aaa442a22',
    'build/config/gtos/include/llvm-libc-types/struct_in6_addr.h': '0ebc3ed093ee5bb490f92d4df694f7e12e7807d57aa3cd58893618178bb04a53',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_in.h': '965edf357542ea39f0885692038c2da4a308c99a7bd5cf977c385a0d62bf8cda',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_in6.h': '5377a9cbe49b2ad82b38b816ead821d5f68a3208acdf39bd650db70f59fddfea',
    'build/config/gtos/include/llvm-libc-types/struct_sockaddr_storage.h': '053f8bd884efecfbc555e21e9d1fb75b0a7bf8b6ea384c446da04ece5e6f3558',
    'third_party/boringssl/src/crypto/thread_gtos.cc': '38b3827787f1c29d32b072c1e64fb59c8f16e19397367231276a7ceddcb14099',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/process/process_handle_gtos.cc': '4b3a71a0ea7dd3f1c3215b889a0e727633a72db70bb553febe61a05c34dd7743',
    'base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/threading/platform_thread_gtos.cc': '24e11c6c201c1e81362da557ca2184f0ec3ae4f81479ba7ad497e351460c1cda',
    'tools/gtos_native_sdk/native-services.h': '2f3c8b2b534cedc98287141b98c74567917b4d57814c49f6672e914689ded6b8',
}

# Python resolves a symlinked entry script before populating sys.path[0]. Keep
# the unchanged TypeScript entry inside the private checkout so its validators
# and GN arguments agree on the source root. No dependency check is relaxed.
MATERIALIZED = {
    'base/threading/platform_thread.cc': 'a55c93de98faebc8fa1faa27ea56c1b576bbe9cb4bb5e6a62ee6d877248e8f1c',
    'tools/typescript/ts_library.py': '65386df09493d3a9cf7201321df9930b66c368459d54378afc917b2c24ac788b',
    # Keep Python's generator and imported modules under the same GN root.
    # Dawn's upstream exact expected-input validation stays enabled.
    'third_party/dawn/generator/dawn_gpu_info_generator.py': '60eac0bab246877e325e4472fb7fe363ad87b11ee9f9a4c05e9488e0f13b1e4b',
    'third_party/dawn/generator/dawn_json_generator.py': '74323b3a433c3daee42ffff5e7d0819514c9ecf9849d36c19786bb8386bc43f0',
    'third_party/dawn/generator/dawn_version_generator.py': '060ddf0f89ac1a18174f9465afb9accc89b5141a7606cb43daad590382a4f7ba',
    'third_party/dawn/generator/extract_json.py': '7e3dbd386dc08b94bc38e2d8be058484543699512d60a76b502731d1626405fb',
    'third_party/dawn/generator/generator_lib.py': '045f8b91eaa9793ddd004d610c605ff8dec60abc073de5acb378120870e8421c',
    'third_party/dawn/generator/opengl_loader_generator.py': '244cfb9b4716dee0e56211f5f0c083ff98b6f38b7fc808eb91e54a8236d96ebb',
    'third_party/dawn/generator/webgpu_docs_utility.py': 'c35eecff650c6b43f6fa188eed62390384506109c76a4bdc0d7bf09fabe5ecb1',
}

# BoringSSL traverses sibling directories with quoted relative includes.
# A linked directory can carry a subsequent ".." back into the shared tree.
RELATIVE_INCLUDE_TREES = {
    'third_party/boringssl/src/crypto/internal.h': 'third_party/boringssl/src',
}


def private_target(view, rel):
    parent = candidate.materialize_parents(view, rel)
    if parent.resolve() != view and view not in parent.resolve().parents:
        raise SystemExit('platform parent escaped the private view: ' + str(parent))
    return parent / Path(rel).name


def addition_source(view, target, source):
    if source.startswith('//'):
        relative = Path(source[2:])
        if relative.is_absolute() or '..' in relative.parts or target not in ADDITION_SHA256:
            raise SystemExit('expected a pinned addition from the composed source view: ' + source)
        return view / relative
    return HERE / source


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
    for rel, expected in MATERIALIZED.items():
        if candidate.digest(view / rel) != expected:
            raise SystemExit('materialized input changed: ' + rel)
    for rel, source in ADDITIONS.items():
        target = view / rel
        if target.exists() or target.is_symlink():
            raise SystemExit('refusing to overwrite platform addition: ' + rel)
        path = addition_source(view, rel, source)
        if not path.is_file():
            raise SystemExit('platform addition is missing: ' + source)
        expected = ADDITION_SHA256.get(rel)
        if expected is not None and candidate.digest(path) != expected:
            raise SystemExit('qualified addition changed: ' + rel)
    for rel in INPUTS:
        patch = HERE / 'patches' / candidate.patch_name(rel)
        subprocess.run(['/usr/bin/patch', '--dry-run', '--batch', '--fuzz=0',
                        '--input', str(patch), str((view / rel).resolve())], check=True)
    real_directories = []
    for header, root in RELATIVE_INCLUDE_TREES.items():
        if header not in INPUTS:
            continue
        origin = (view / root).resolve()
        for current, children, _ in os.walk(origin):
            current = Path(current)
            children[:] = sorted(name for name in children if not name.startswith('.'))
            if any((current / name).is_symlink() for name in children):
                raise SystemExit('relative include tree contains a directory link: ' + str(current))
            rel = (Path(root) / current.relative_to(origin)).as_posix()
            parent = private_target(view, rel + '/.gtos-layout').parent
            if parent.resolve() != parent:
                raise SystemExit('relative include tree escaped the view: ' + rel)
            real_directories.append(rel)
    applied = []
    for rel, expected in MATERIALIZED.items():
        target = private_target(view, rel)
        if target.is_symlink():
            origin = target.resolve()
            target.unlink()
            shutil.copyfile(origin, target)
        if candidate.digest(target) != expected:
            raise SystemExit('materialized input changed during copy: ' + rel)
        applied.append({'path': rel, 'origin_sha256': expected,
                        'sha256': expected, 'materialized': True})
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
        shutil.copyfile(addition_source(view, rel, source), target)
        digest = candidate.digest(target)
        expected = ADDITION_SHA256.get(rel)
        if expected is not None and digest != expected:
            raise SystemExit('qualified addition changed during copy: ' + rel)
        added.append({'path': rel, 'source': source, 'sha256': digest})
    with receipt.open('x') as handle:
        json.dump({'files': applied, 'additions': added,
                   'real_directories': real_directories}, handle, indent=2)
        handle.write('\n')
    print(json.dumps({'view': str(view), 'applied': applied, 'additions': added}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('view', type=Path)
    apply(parser.parse_args().view)
