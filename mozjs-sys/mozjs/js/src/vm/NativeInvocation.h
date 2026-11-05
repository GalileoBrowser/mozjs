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
  // Execution boundary only; unlike nativeInvocationTop this is active for
  // every realm policy and does not expose or capture Chrome stack features.
  AutoNativeInvocation* previousBoundary_ = nullptr;
  JS::RootedObject callee_;
  JS::RootedValue receiver_;
  const JS::Value* arguments_;
  NativeInvocationCaller caller_;
  NativeInvocationPath path_;
  bool constructing_;
  bool linked_ = false;
  bool delegated_ = false;
  bool boundaryLinked_ = false;

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

// Cross-compartment forwarding is an implementation detail, not a new JS
// execution context. Its own native errors belong to the initiating operation.
// A real script/native callback starts a different operation, even when it
// happens to re-enter the same realm. Compare live execution boundaries,
// never authored function names, prototypes, or public constructor properties.
struct NativeOperationBoundary {
  Activation* activation = nullptr;
  const void* frame = nullptr;
  const void* native = nullptr;
  bool operator==(const NativeOperationBoundary& other) const;
};

class MOZ_RAII AutoNativeOperationRealm {
  JSContext* context_;
  AutoNativeOperationRealm* previous_;
  JS::RootedObject originGlobal_;
  NativeOperationBoundary boundary_;

 public:
  explicit AutoNativeOperationRealm(JSContext* cx);
  ~AutoNativeOperationRealm();
  void resetForFunctionEntry();
  AutoNativeOperationRealm(const AutoNativeOperationRealm&) = delete;
  AutoNativeOperationRealm& operator=(const AutoNativeOperationRealm&) = delete;

  friend JSObject* NativeOperationErrorGlobal(JSContext* cx);
};

// Only used while creating an engine-owned failure. This must not copy,
// replace, reformat or otherwise alter an exception thrown by authored code.
JSObject* NativeOperationErrorGlobal(JSContext* cx);

// Preserve the native caller context even when construction enters script
// through an implementation-only compartment switch. Firefox prepares `this`
// in the callee realm; Chrome retains the caller for pre-body failures.
class MOZ_RAII AutoNativeConstructorRealm {
  JSContext* context_;
  AutoNativeConstructorRealm* previous_;
  NativeOperationBoundary caller_;
  JS::RootedObject callerGlobal_;
  AutoNativeOperationRealm operation_;

 public:
  explicit AutoNativeConstructorRealm(JSContext* cx);
  ~AutoNativeConstructorRealm();
  void enterCallee();
  AutoNativeConstructorRealm(const AutoNativeConstructorRealm&) = delete;
  AutoNativeConstructorRealm& operator=(const AutoNativeConstructorRealm&) = delete;

  friend JSObject* NativeConstructorCallerGlobal(JSContext* cx);
};

JSObject* NativeConstructorCallerGlobal(JSContext* cx);
void ReportBadDerivedReturn(JSContext* cx, JS::HandleValue value);

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
