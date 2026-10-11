// Real V8 API entry for the native whole-Isolate integration target.
// Runtime and image admission remain unqualified until the complete link runs.
#include <memory>
#include <string.h>
#include "include/v8.h"
#include "include/libplatform/libplatform.h"

namespace {
int RunExpression(v8::Isolate* isolate, const char* source, const char* expected) {
    v8::HandleScope handles(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) return 1;
    v8::Context::Scope entered(context);
    v8::TryCatch caught(isolate);
    v8::Local<v8::String> text;
    if (!v8::String::NewFromUtf8(isolate, source).ToLocal(&text)) return 2;
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, text).ToLocal(&script)) return 3;
    v8::Local<v8::Value> value;
    if (!script->Run(context).ToLocal(&value)) return 4;
    v8::String::Utf8Value actual(isolate, value);
    if (!*actual || strcmp(*actual, expected) != 0) return 5;
    return 0;
}
}

extern "C" int NativeV8IsolateMain() {
    // Explicit deterministic bring-up seed is not a qualified entropy service.
    v8::V8::SetFlagsFromString("--jitless --single-threaded --no-freeze-flags-after-init --random-seed=42");
    std::unique_ptr<v8::Platform> platform = v8::platform::NewSingleThreadedDefaultPlatform();
    if (!platform) return 10;
    v8::V8::InitializePlatform(platform.get());
    if (!v8::V8::Initialize()) {
        v8::V8::DisposePlatform();
        return 11;
    }
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    int result = 12;
    if (allocator) {
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = allocator.get();
        params.allow_atomics_wait = false;
        v8::Isolate* isolate = v8::Isolate::New(params);
        if (isolate) {
            {
                v8::Isolate::Scope entered(isolate);
                result = RunExpression(isolate, "'GTOS' + (40 + 2)", "GTOS42");
                if (!result) result = RunExpression(isolate, "1.25 + 2.5", "3.75");
            }
            isolate->Dispose();
        }
    }
    if (!v8::V8::Dispose() && !result) result = 13;
    v8::V8::DisposePlatform();
    return result;
}
