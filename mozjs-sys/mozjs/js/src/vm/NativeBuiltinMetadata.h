/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeBuiltinMetadata_h
#define vm_NativeBuiltinMetadata_h
#include "js/RootingAPI.h"

namespace js {
enum NativeBuiltinRole : uint32_t {
  EngineDefinition = 1,
  PublicNativeBuiltin = 2,
  TransparentNativeForwarder = 4,
  PrivatePromiseElement = 8,
};
enum class NativePromiseCombinatorKind : uint32_t {
  None = 0, All = 1, AllSettled = 2, Any = 3,
};
class MOZ_RAII AutoNativeBuiltinDefinition {
  JSContext* cx_;
 public:
  explicit AutoNativeBuiltinDefinition(JSContext* cx);
  ~AutoNativeBuiltinDefinition();
};
bool RegisterNativeBuiltinFunction(JSContext* cx, JS::HandleFunction function,
                                   bool publicDefinition);
uint32_t NativeBuiltinFunctionRole(JSObject* function);
// Immutable native factory provenance. The element closure remains private;
// these records do not add public properties or classify authored names.
bool RegisterNativePromiseElement(JSContext* cx, JS::HandleFunction element,
    JS::HandleObject combinator, JS::HandleObject constructor,
    JS::HandleObject capabilityPromise, uint32_t index,
    NativePromiseCombinatorKind kind);
bool ReadNativePromiseElement(JSContext* cx, JS::HandleObject element,
    JS::MutableHandleObject combinator, JS::MutableHandleObject constructor,
    JS::MutableHandleObject capabilityPromise, uint32_t* index,
    NativePromiseCombinatorKind* kind);

class MOZ_RAII AutoNativeBuiltinStackCapture {
  JSContext* cx_;
  bool prior_;
  const JS::Value* priorSkip_;
 public:
  explicit AutoNativeBuiltinStackCapture(JSContext* cx, const JS::Value* skip = nullptr);
  ~AutoNativeBuiltinStackCapture();
};
}  // namespace js
#endif
