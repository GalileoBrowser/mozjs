/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeErrorStack_h
#define vm_NativeErrorStack_h

#include "js/RootingAPI.h"

namespace js {

// Saved frames are collected by the existing native collector. This installs
// private GC-traced data and shared receiver-based accessors, not authored JS.
bool InstallNativeErrorStack(JSContext* cx, JS::HandleObject receiver,
                             JS::HandleObject stack, bool enabled,
                             JS::HandleString originalMessage = nullptr,
                             bool preferOriginalMessage = false);

// Trusted embedding inspection of a same-realm private snapshot. This is not a
// web-exposed CallSite getter: formatter privacy/branding remains separate.
// Reject foreign-principal frames and unavailable values rather than inventing
// a receiver or function from a saved name. Used by native GC regressions.
bool ReadNativeErrorStackFrameValue(JSContext* cx, JS::HandleObject receiver,
                                    uint32_t index, uint32_t field,
                                    JS::MutableHandleValue result);

}  // namespace js

#endif
