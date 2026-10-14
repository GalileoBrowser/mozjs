/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeStackFrameData_h
#define vm_NativeStackFrameData_h

#include "vm/NativeObject.h"

namespace js {

class FrameIter;
class ScriptSource;
struct NativeInvocationSnapshot;
struct NativePromiseStackSnapshot;

// Private, per-capture native records. These are not CallSite-shaped JS objects
// or reconstructed stack strings. The live function, receiver and source object
// are GC-traced values, captured before unwinding or asynchronous suspension.
// SavedFrame's public/structured-clone accessors deliberately do not expose them.
class NativeStackFrameData : public NativeObject {
 public:
  static const JSClass class_;
  enum Slots {
    Function, Receiver, SourceObject, Flags, ScriptName, SourceURL,
    PromiseKind, PromiseIndex, SlotCount
  };
  enum Flag : uint32_t {
    HasFunction = 1 << 0,
    Strict = 1 << 1,
    Constructing = 1 << 2,
    Eval = 1 << 3,
    Module = 1 << 4,
    SelfHosted = 1 << 5,
    Wasm = 1 << 6,
    ReceiverUnavailable = 1 << 7,
    UninitializedReceiver = 1 << 8,
    PublicBuiltin = 1 << 9,
    StackTransparent = 1 << 10,
    CppBuiltin = 1 << 11,
    Async = 1 << 12,
    PromiseCombinator = 1 << 13,
  };

  static bool capture(JSContext* cx, FrameIter& iter,
                      MutableHandleObject result);
  static bool isEvalSource(ScriptSource* source);
  static bool captureNative(JSContext* cx, const NativeInvocationSnapshot& invocation,
                            MutableHandleObject result);
  static bool capturePromise(JSContext* cx, const NativePromiseStackSnapshot& snapshot,
                            MutableHandleObject result);
  static bool capturePromiseInvocation(JSContext* cx, FrameIter& iter,
                                       MutableHandleObject result);

  bool hasFlag(Flag flag) const {
    return getReservedSlot(Flags).toInt32() & flag;
  }
};

}  // namespace js
#endif
