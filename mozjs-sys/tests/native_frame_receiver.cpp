/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Compile against the configured engine's public and private headers. This
// exercises the same classifier used by live capture, not a copied predicate.
#include "vm/NativeStackFrameData.h"

int main() {
  using js::NativeStackFrameData;
  if (NativeStackFrameData::receiverFlags(JS::UndefinedValue()) != 0) return 1;
  if (NativeStackFrameData::receiverFlags(JS::NullValue()) != 0) return 2;
  if (NativeStackFrameData::receiverFlags(JS::BooleanValue(false)) != 0) return 3;
  if (NativeStackFrameData::receiverFlags(JS::Int32Value(7)) != 0) return 4;
  if (NativeStackFrameData::receiverFlags(JS::MagicValue(JS_UNINITIALIZED_LEXICAL)) !=
      (NativeStackFrameData::ReceiverUnavailable | NativeStackFrameData::UninitializedReceiver)) return 5;
  if (NativeStackFrameData::receiverFlags(JS::MagicValue(JS_OPTIMIZED_OUT)) !=
      NativeStackFrameData::ReceiverUnavailable) return 6;
  if (NativeStackFrameData::receiverFlags(JS::DoubleValue(3.5)) != 0) return 7;
  for (unsigned kind = 0; kind < JS_WHY_MAGIC_COUNT; kind++) {
    const auto reason = static_cast<JSWhyMagic>(kind);
    uint32_t expected = NativeStackFrameData::ReceiverUnavailable;
    if (reason == JS_UNINITIALIZED_LEXICAL) {
      expected |= NativeStackFrameData::UninitializedReceiver;
    }
    if (NativeStackFrameData::receiverFlags(JS::MagicValue(reason)) != expected) return 8;
  }
  return 0;
}
