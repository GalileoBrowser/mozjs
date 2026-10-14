/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/NativeBuiltinMetadata.h"
#include "vm/JSContext.h"
#include "vm/JSFunction.h"
#include "vm/NativeObject.h"
#include "vm/Realm.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/Realm-inl.h"

using namespace js;
namespace {
class NativeBuiltinData : public NativeObject {
 public:
  static const JSClass class_;
  enum { Role, SlotCount };
};
const JSClass NativeBuiltinData::class_ = {
    "NativeBuiltinDefinitionData", JSCLASS_HAS_RESERVED_SLOTS(NativeBuiltinData::SlotCount)};
NativeBuiltinData* LookupNativeBuiltinDefinition(JSObject* function) {
  if (!function || !function->is<JSFunction>() || function->as<JSFunction>().isIntrinsic()) return nullptr;
  JSObject* data = ObjectRealm::get(function).getNativeBuiltinData(function);
  return data ? &data->as<NativeBuiltinData>() : nullptr;
}
}  // namespace

AutoNativeBuiltinDefinition::AutoNativeBuiltinDefinition(JSContext* cx) : cx_(cx) {
  ++cx_->nativeBuiltinDefinitionDepth;
}
AutoNativeBuiltinDefinition::~AutoNativeBuiltinDefinition() {
  MOZ_ASSERT(cx_->nativeBuiltinDefinitionDepth);
  --cx_->nativeBuiltinDefinitionDepth;
}

bool js::RegisterNativeBuiltinFunction(JSContext* cx, JS::HandleFunction function,
                                      bool publicDefinition) {
  if (!cx->nativeBuiltinDefinitionDepth || !function->isBuiltinNative() || function->isIntrinsic()) return true;
  uint32_t role = EngineDefinition | (publicDefinition ? PublicNativeBuiltin : 0);
  // Actual forwarding implementation identity, never the public name.
  if (function->native() == fun_call || function->native() == fun_apply) role |= TransparentNativeForwarder;
  if (auto* data = LookupNativeBuiltinDefinition(function)) {
    role |= data->getReservedSlot(NativeBuiltinData::Role).toInt32();
    data->setReservedSlot(NativeBuiltinData::Role, Int32Value(role));
    return true;
  }
  RootedObject key(cx, function.get());
  Rooted<NativeBuiltinData*> data(cx, NewObjectWithGivenProto<NativeBuiltinData>(cx, nullptr));
  if (!data) return false;
  data->initReservedSlot(NativeBuiltinData::Role, Int32Value(role));
  return ObjectRealm::get(function).putNativeBuiltinData(cx, key, data);
}
uint32_t js::NativeBuiltinFunctionRole(JSObject* function) {
  auto* data = LookupNativeBuiltinDefinition(function);
  return data ? data->getReservedSlot(NativeBuiltinData::Role).toInt32() : 0;
}

AutoNativeBuiltinStackCapture::AutoNativeBuiltinStackCapture(JSContext* cx, const JS::Value* skip)
    : cx_(cx), prior_(cx->nativeBuiltinStackCapture), priorSkip_(cx->nativeStackCaptureSkipInvocation) {
  cx_->nativeBuiltinStackCapture = cx->realm()->creationOptions().v8ErrorStackLimits();
  cx_->nativeStackCaptureSkipInvocation = skip;
}
AutoNativeBuiltinStackCapture::~AutoNativeBuiltinStackCapture() {
  cx_->nativeBuiltinStackCapture = prior_;
  cx_->nativeStackCaptureSkipInvocation = priorSkip_;
}
