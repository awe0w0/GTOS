//
// Copyright 2026 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "common/system_utils.h"

#include <stdlib.h>

#include "base/debug/debugger.h"
#include "base/files/file_util.h"

namespace angle
{
bool IsDebuggerAttached()
{
    return base::debug::BeingDebugged();
}

void BreakDebugger()
{
    base::debug::BreakDebugger();
}

std::string GetExecutablePath()
{
    // The kernel launches native ELF images from modules, without a file path.
    return {};
}

char GetPathSeparator()
{
    return '/';
}

std::string GetRootDirectory()
{
    return "/";
}

std::string GetEnvironmentVar(const char *name)
{
    const char *value = getenv(name);
    return value ? value : "";
}

const char *GetPathSeparatorForEnvironmentVar()
{
    return ":";
}

bool SetEnvironmentVar(const char *name, const char *value)
{
    return setenv(name, value, 1) == 0;
}

Optional<std::string> GetTempDirectory()
{
    base::FilePath path;
    return base::GetTempDir(&path) ? Optional<std::string>(path.value())
                                   : Optional<std::string>::Invalid();
}

Optional<std::string> CreateTemporaryFileInDirectory(const std::string &directory)
{
    base::FilePath path;
    return base::CreateTemporaryFileInDir(base::FilePath(directory), &path)
               ? Optional<std::string>(path.value())
               : Optional<std::string>::Invalid();
}

const char *GetSharedLibraryExtension()
{
    return ".so";
}

void *OpenSystemLibraryWithExtensionAndGetError(const char *,
                                                 SearchType,
                                                 std::string *errorOut)
{
    if (errorOut)
    {
        *errorOut = "Shared libraries are unavailable on GTOS";
    }
    return nullptr;
}

}  // namespace angle
