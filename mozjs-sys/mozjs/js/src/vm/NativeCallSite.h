/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeCallSite_h
#define vm_NativeCallSite_h
#include "js/RootingAPI.h"
namespace js {
// Retain real saved frames and traced live capture data, not parsed strings.
bool BuildNativeCallSites(JSContext* cx, JS::HandleObject stack,
                          JS::MutableHandleObject result);
bool FormatNativeErrorFrames(JSContext* cx, JS::HandleObject stack,
                             JS::MutableHandleString result);
}
#endif
