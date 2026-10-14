/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeFunctionDebugName_h
#define vm_NativeFunctionDebugName_h
#include "js/RootingAPI.h"
namespace js {
// Actual outer CreateDynamicFunction result, never a name-string heuristic.
bool RegisterNativeDynamicFunctionName(JSContext* cx, JS::HandleFunction function);
bool HasNativeDynamicFunctionName(JSObject* function);
bool HasNativeIntrinsicFunctionName(JSObject* function);
// Only successful non-resolving definitions/deletions replace provenance.
void NoteNativeFunctionNameMutation(JSObject* function);
}  // namespace js
#endif
