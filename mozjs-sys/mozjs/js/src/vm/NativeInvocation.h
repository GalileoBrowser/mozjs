/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeInvocation_h
#define vm_NativeInvocation_h

#include "js/CallArgs.h"
#include "js/RootingAPI.h"
#include "js/GCVector.h"
#include "js/TracingAPI.h"

namespace js {
class Activation;

struct NativeInvocationCaller {
  Activation* activation = nullptr;
  void* physicalFrame = nullptr;
  unsigned inlineFrameNo = 0;
  uint32_t tier = 0;
};

enum class NativeInvocationPath { Interpreter, Getter, Setter };

// Record the real invocation before a native writes its callee/result slot.
// There is no heap allocation or authored property access on entry. Function
// and receiver are normal GC roots; only non-GC caller identity is stored raw.
class MOZ_RAII AutoNativeInvocation {
  JSContext* context_;
  AutoNativeInvocation* previous_ = nullptr;
  JS::RootedObject callee_;
  JS::RootedValue receiver_;
  const JS::Value* arguments_;
  NativeInvocationCaller caller_;
  NativeInvocationPath path_;
  bool constructing_;
  bool linked_ = false;
  bool delegated_ = false;

 public:
  AutoNativeInvocation(JSContext* cx, const JS::CallArgs& args,
                       NativeInvocationPath path = NativeInvocationPath::Interpreter);
  ~AutoNativeInvocation();
  AutoNativeInvocation(const AutoNativeInvocation&) = delete;
  AutoNativeInvocation& operator=(const AutoNativeInvocation&) = delete;

  AutoNativeInvocation* previous() const { return previous_; }
  JSObject* callee() const { return callee_; }
  const JS::Value& receiver() const { return receiver_.get(); }
  const JS::Value* arguments() const { return arguments_; }
  const NativeInvocationCaller& caller() const { return caller_; }
  NativeInvocationPath path() const { return path_; }
  bool constructing() const { return constructing_; }
  bool delegated() const { return delegated_; }
  void setDelegated(bool value) { delegated_ = value; }
};

class MOZ_RAII AutoNativeTrampolineInvocation {
  JSContext* cx_;
  AutoNativeTrampolineInvocation* previous_;
  AutoNativeInvocation* scope_ = nullptr;
  Activation* activation_;
  bool priorDelegated_ = false;
 public:
  AutoNativeTrampolineInvocation(JSContext* cx, const JS::CallArgs& args, Activation* activation);
  ~AutoNativeTrampolineInvocation();
  AutoNativeTrampolineInvocation* previous() const { return previous_; }
  AutoNativeInvocation* scope() const { return scope_; }
  Activation* activation() const { return activation_; }
};

struct NativeInvocationSnapshot {
  JSObject* callee;
  JS::Value receiver;
  const JS::Value* arguments;
  NativeInvocationCaller caller;
  bool constructing;
  bool consumed = false;
  NativeInvocationSnapshot(JSObject* function, const JS::Value& thisValue,
      const JS::Value* vp, NativeInvocationCaller key, bool constructing)
      : callee(function), receiver(thisValue), arguments(vp), caller(key), constructing(constructing) {}
  void trace(JSTracer* trc) {
    JS::GCPolicy<JSObject*>::trace(trc, &callee, "native invocation callee");
    if (!receiver.isMagic()) JS::GCPolicy<JS::Value>::trace(trc, &receiver, "native invocation receiver");
  }
};
using NativeInvocationSnapshots = JS::GCVector<NativeInvocationSnapshot, 8, js::SystemAllocPolicy>;
bool CaptureNativeInvocationSnapshots(JSContext* cx, JS::MutableHandle<NativeInvocationSnapshots> snapshots);

// Embedding-only numeric regression observer. No page API is installed.
uint32_t ProbeNativeInvocationState(JSContext* cx, JS::HandleObject expected,
                                   JS::HandleValue receiver);
}  // namespace js
#endif
