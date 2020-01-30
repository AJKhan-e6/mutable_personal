#pragma once

#include "WebAssembly.hpp"
#include <libplatform/libplatform.h>
#include <v8.h>


namespace db {

struct V8Platform : WasmPlatform
{
    private:
    static std::unique_ptr<v8::Platform> PLATFORM_;
    v8::ArrayBuffer::Allocator *allocator_ = nullptr;
    v8::Isolate *isolate_ = nullptr;

    public:
    V8Platform();
    ~V8Platform();

    void execute(const WASMModule &module) override;

    private:
    /** Compile our `WASMModule` to a V8 `WasmModuleObject`. */
    v8::Local<v8::WasmModuleObject> compile_wasm_module(const WASMModule &module);
    /** Create an instance from a compiled V8 WebAssembly module and its imports. */
    v8::Local<v8::Object> create_wasm_instance(v8::Local<v8::WasmModuleObject> module, v8::Local<v8::Object> imports);
    /** Creates an V8 object that captures the entire environment.  TODO Only capture things relevant to the module. */
    v8::Local<v8::Object> create_env() const;
    /** Converts any value to JSON. */
    v8::Local<v8::String> to_json(v8::Local<v8::Value> val) const;
};

}
