#!/usr/bin/env python3
"""Compile the real native headers with the formal Content C++ command."""

import argparse
from pathlib import Path
import shlex
import subprocess


parser = argparse.ArgumentParser()
for name in ('ninja', 'clang', 'build', 'out'):
    parser.add_argument('--' + name, required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(exist_ok=False)

edge = 'obj/content/shell/content_shell_app/shell_main_delegate.o'
command = shlex.split(subprocess.run(
    [str(args.ninja), '-t', 'commands', '-s', edge], cwd=args.build,
    check=True, capture_output=True, text=True).stdout)
assert command[0] == str(args.clang)
assert '--target=i686-unknown-none-elf' in command and '-nostdinc' in command
assert command.count('-c') == command.count('-o') == command.count('-MF') == 1

source = """
#include "mojo/public/cpp/platform/platform_handle.h"
#include "net/base/ip_endpoint.h"
#include "net/base/sockaddr_storage.h"
#include "net/socket/socket_descriptor.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/gpu_fence_handle.h"
#include "ui/gfx/native_ui_types.h"
#include <iterator>
#include <netinet/in.h>
#include <type_traits>

static_assert(BUILDFLAG(IS_GTOS) && !BUILDFLAG(IS_POSIX));
static_assert(std::is_same_v<socklen_t, unsigned long>);
static_assert(sizeof(sockaddr) == 4 && sizeof(sockaddr_storage) == 128);
static_assert(sizeof(sockaddr_in) == 16 && sizeof(sockaddr_in6) == 28);
static_assert(std::is_same_v<gfx::AcceleratedWidget, uint32_t>);
static_assert(gfx::kNullAcceleratedWidget == 0);
static_assert(std::is_same_v<net::SocketDescriptor, int>);
static_assert(net::kInvalidSocket == -1);
static_assert(ui::VKEY_UNKNOWN == 0 && ui::VKEY_RETURN == 13);
static_assert(!std::is_copy_constructible_v<gfx::GpuFenceHandle>);
static_assert(MOJO_PLATFORM_HANDLE_TYPE_GTOS_FILE !=
              MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR);

constexpr bool IterationMatchesSize(size_t size) {
  net::IPAddressBytes address;
  address.Resize(size);
  const auto& read_only = address;
  return std::distance(address.begin(), address.end()) ==
             static_cast<ptrdiff_t>(size) &&
         std::distance(read_only.begin(), read_only.end()) ==
             static_cast<ptrdiff_t>(size);
}
static_assert(IterationMatchesSize(0));
static_assert(IterationMatchesSize(4));
static_assert(IterationMatchesSize(16));

mojo::PlatformHandle UseNativeHandle(base::ScopedFD file) {
  mojo::PlatformHandle owner(std::move(file));
  return owner.Clone();
}
"""

for name, code, flags, diagnostic in [
    ('native_headers', source, [], None),
    ('host_identity_refused', '#include <net/address_types.h>\n',
     ['-D__linux__=1'], 'refuses a host OS identity'),
    ('socket_syscall_absent', source + '\nint socket_call() { return socket(2, 1, 0); }\n',
     [], "undeclared identifier 'socket'"),
    ('gpu_fence_adopt_absent', source + '\nvoid UseFence(gfx::GpuFenceHandle& fence) { fence.Adopt(1); }\n',
     [], "no member named 'Adopt'"),
]:
    input_file = args.out / (name + '.cc')
    output = args.out / (name + '.o')
    depfile = args.out / (name + '.d')
    input_file.write_text(code)
    argv = command[:]
    argv[argv.index('-c') + 1] = str(input_file)
    argv[argv.index('-o') + 1] = str(output)
    argv[argv.index('-MF') + 1] = str(depfile)
    result = subprocess.run(argv + flags, cwd=args.build,
                            capture_output=True, text=True)
    log = result.stdout + result.stderr
    (args.out / (name + '.log')).write_text(log)
    if diagnostic is None:
        assert result.returncode == 0 and output.read_bytes()[:5] == b'\x7fELF\x01', log
        dependencies = depfile.read_text()
        for header in ('include/net/address_types.h', 'include/sys/socket.h',
                       'include/llvm-libc-types/socklen_t.h'):
            assert 'build/config/gtos/' + header in dependencies
    else:
        assert result.returncode != 0 and diagnostic in log and not output.exists(), log
    print('PASS', name)
