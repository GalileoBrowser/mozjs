/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef vm_NativeStackTraceDiagnostic_h
#define vm_NativeStackTraceDiagnostic_h

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "js/Value.h"

namespace js {

// Opt-in diagnostics for actual native entry paths. No strings, objects,
// arguments, URLs, source names, stack contents or numeric values are emitted.
// The first sixteen event IDs follow NativeCallSite's Method order, followed
// by stack read, authored formatter dispatch and default formatter completion.
inline void TraceNativeStackOutcome(unsigned event, bool success,
                                   const JS::Value& result) {
  static const bool enabled = [] {
    const char* flag = std::getenv("GALILEO_NATIVE_STACK_TRACE");
    return flag && std::strcmp(flag, "1") == 0;
  }();
  if (!enabled || event == 0 || event > 19) return;
  static std::atomic<unsigned> counts[20]{};
  unsigned seen = counts[event].load(std::memory_order_relaxed);
  while (seen < 5 && !counts[event].compare_exchange_weak(
      seen, seen + 1, std::memory_order_relaxed)) {}
  if (seen >= 5) return;
  // One saturation marker prevents treating a bounded sample as full coverage.
  // Saturating counters cannot wrap and accidentally re-emit private sessions.
  if (seen == 4) {
    std::fprintf(stderr, "GALILEO_NATIVE_STACK_V1 [%u,2,0,0]\n", event);
    return;
  }
  unsigned kind = result.isUndefined() ? 0 : result.isNull() ? 1 :
      result.isBoolean() ? 2 : result.isNumber() ? 3 : result.isString() ? 4 :
      result.isObject() ? 5 : result.isSymbol() ? 6 : result.isBigInt() ? 7 : 8;
  unsigned boolean = success && result.isBoolean() && result.toBoolean();
  std::fprintf(stderr, "GALILEO_NATIVE_STACK_V1 [%u,%u,%u,%u]\n",
               event, unsigned(success), kind, boolean);
}

}  // namespace js
#endif
