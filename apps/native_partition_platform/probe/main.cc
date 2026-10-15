// Real V8 API entry for the native whole-Isolate integration target.
// Workloads run in the same real Isolate; no host execution or substitute engine.
#include <memory>
#include <string.h>
#include "include/v8.h"
#include "include/libplatform/libplatform.h"

extern "C" int GtosPartitionPlatformChecks();

extern "C" {
    volatile unsigned native_v8_workload_mask=0;
    volatile unsigned native_v8_gc_callbacks=0;
}

namespace {
int RunInContext(v8::Isolate* isolate, v8::Local<v8::Context> context,
                 const char* source, const char* expected) {
    v8::HandleScope handles(isolate);
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
int RunExpression(v8::Isolate* isolate, const char* source, const char* expected) {
    v8::HandleScope handles(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) return 1;
    return RunInContext(isolate, context, source, expected);
}
void CountGC(v8::Isolate*, v8::GCType, v8::GCCallbackFlags) {
    native_v8_gc_callbacks=native_v8_gc_callbacks+1;
}
int RunWorkloads(v8::Isolate* isolate) {
    v8::HandleScope handles(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) return 21;
    int result = RunInContext(isolate, context,
        "(() => { const a=Array.from({length:64},(_,i)=>({i,values:[i,i+1,i+2]}));"
        "const o=Object.create({kind:'GTOS'}); o.answer=a.reduce((s,v)=>s+v.i,0);"
        "return [a.length,o.kind,o.answer,a[7].values.join(':')].join('|'); })()",
        "64|GTOS|2016|7:8:9");
    if (result) return 22;
    native_v8_workload_mask=native_v8_workload_mask|4U;
    result = RunInContext(isolate, context,
        "globalThis.__buffer=new ArrayBuffer(64); globalThis.__bytes=new Uint8Array(__buffer);"
        "globalThis.__data=new DataView(__buffer);"
        "__bytes[0]=91; __bytes[63]=207; __data.setUint32(4,0x12345678,true);"
        "__data.setFloat64(16,3.75,true); __bytes.subarray(8,12)[0]=165;"
        "[__bytes.length,...__bytes.slice(4,8),__data.getFloat64(16,true),__bytes[8]].join('|')",
        "64|120|86|52|18|3.75|165");
    if (result) return 23;
    native_v8_workload_mask=native_v8_workload_mask|8U;
    isolate->AddGCPrologueCallback(CountGC, v8::kGCTypeMarkSweepCompact);
    isolate->LowMemoryNotification();
    isolate->RemoveGCPrologueCallback(CountGC);
    if (!native_v8_gc_callbacks) return 24;
    result = RunInContext(isolate, context,
        "[__buffer.byteLength,__bytes[0],__bytes[63],__data.getFloat64(16,true)].join('|')",
        "64|91|207|3.75");
    if (result) return 25;
    native_v8_workload_mask=native_v8_workload_mask|16U;
    result = RunInContext(isolate, context,
        "globalThis.__microtask=0; Promise.resolve(7).then(v=>{__microtask=v*6;}); __microtask", "0");
    if (result) return 26;
    isolate->PerformMicrotaskCheckpoint();
    if (RunInContext(isolate, context, "__microtask", "42")) return 27;
    native_v8_workload_mask=native_v8_workload_mask|32U;
    {
        v8::Context::Scope entered(context);
        v8::TryCatch caught(isolate);
        v8::Local<v8::String> text;
        if (!v8::String::NewFromUtf8(isolate, "throw new Error('GTOS expected')").ToLocal(&text)) return 28;
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, text).ToLocal(&script)) return 29;
        v8::Local<v8::Value> value;
        if (script->Run(context).ToLocal(&value) || !caught.HasCaught()) return 30;
        v8::String::Utf8Value actual(isolate, caught.Exception());
        if (!*actual || strcmp(*actual, "Error: GTOS expected") != 0) return 31;
    }
    if (RunInContext(isolate, context, "__microtask+__bytes[0]", "133")) return 32;
    native_v8_workload_mask=native_v8_workload_mask|64U;
    return 0;
}
}

extern "C" int NativeV8IsolateMain() {
    const int platform_result=GtosPartitionPlatformChecks();
    if (platform_result) return platform_result;
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
                isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
                result = RunExpression(isolate, "'GTOS' + (40 + 2)", "GTOS42");
                if (!result) {
                    native_v8_workload_mask=native_v8_workload_mask|1U;
                    result = RunExpression(isolate, "1.25 + 2.5", "3.75");
                }
                if (!result) {
                    native_v8_workload_mask=native_v8_workload_mask|2U;
                    result = RunWorkloads(isolate);
                }
            }
            isolate->Dispose();
        }
    }
    if (!v8::V8::Dispose() && !result) result = 13;
    v8::V8::DisposePlatform();
    return result;
}
