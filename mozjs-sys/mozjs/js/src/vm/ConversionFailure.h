/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef vm_ConversionFailure_h
#define vm_ConversionFailure_h

#include <stdint.h>

#include "js/Conversions.h"
#include "js/TypeDecls.h"

namespace js {

// These describe failures owned by the native conversion algorithm. An
// exception from a property getter, proxy, conversion callback or nested
// built-in is propagated without assigning a native-failure kind. The
// embedding can therefore add its Web IDL context without rewriting an
// arbitrary pending exception or running error-message decompilation first.
enum class PrimitiveConversionFailure : uint8_t {
  None,
  MethodNotCallable,
  MethodReturnedObject,
  OrdinaryConversionFailed,
};

enum class BigIntConversionFailure : uint8_t {
  None,
  InvalidPrimitive,
  InvalidString,
  MethodNotCallable,
  MethodReturnedObject,
  OrdinaryConversionFailed,
};

// The caller roots both value slots. A known native rejection returns false
// without creating an exception; failures originating in author code, OOM or
// interruption propagate the VM's already-reported state with kind None.
extern bool ToPrimitiveWithFailure(JSContext* cx, JSType hint,
                                   JS::MutableHandleValue value,
                                   PrimitiveConversionFailure* failure,
                                   JS::MutableHandleValue diagnostic);

extern JS::BigInt* ToBigIntWithFailure(
    JSContext* cx, JS::HandleValue value, BigIntConversionFailure* failure,
    JS::MutableHandleValue diagnostic);

}  // namespace js

#endif  // vm_ConversionFailure_h
