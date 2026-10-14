/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativePromiseStack_h
#define vm_NativePromiseStack_h
#include "js/GCVector.h"
#include "js/RootingAPI.h"
#include "vm/NativeBuiltinMetadata.h"
#include "vm/NativeObject.h"

namespace js {
// Suspension only needs the real invocation receiver. Function/source/location
// already remain available from the generator's actual callee and script.
// Keep this private record small, rather than allocating a full stack capture
// and copying source-name strings for every async invocation.
class NativePromiseInvocationData : public NativeObject {
 public:
  static const JSClass class_;
  enum Slots { Receiver, SlotCount };
};

// Actual suspended generator or actual element-factory/capability state.
// This is not a public CallSite object or a reconstructed stack-name string.
struct NativePromiseStackSnapshot {
  JSObject* generator;
  JSObject* combinator;
  JSObject* constructor;
  uint32_t index;
  NativePromiseCombinatorKind kind;
  NativePromiseStackSnapshot(JSObject* generator_, JSObject* combinator_,
      JSObject* constructor_, uint32_t index_, NativePromiseCombinatorKind kind_)
      : generator(generator_), combinator(combinator_), constructor(constructor_),
        index(index_), kind(kind_) {}
  void trace(JSTracer* trc) {
    JS::GCPolicy<JSObject*>::trace(trc, &generator, "native async generator");
    JS::GCPolicy<JSObject*>::trace(trc, &combinator, "native async combinator");
    JS::GCPolicy<JSObject*>::trace(trc, &constructor, "native async constructor");
  }
};
using NativePromiseStackSnapshots = JS::GCVector<NativePromiseStackSnapshot, 8, SystemAllocPolicy>;
bool CaptureNativePromiseStackSnapshots(JSContext* cx,
    JS::MutableHandle<NativePromiseStackSnapshots> snapshots, uint32_t limit);
// Preserve actual invocation state before an unused this-binding is discarded
// by suspension. The same common generator factory serves all compiler tiers.
bool RecordNativePromiseGeneratorInvocation(JSContext* cx, JS::HandleObject generator);
}  // namespace js
#endif
