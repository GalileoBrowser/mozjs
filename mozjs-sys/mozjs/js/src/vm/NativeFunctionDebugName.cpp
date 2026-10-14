/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/NativeFunctionDebugName.h"
#include "vm/JSFunction.h"
#include "vm/NativeObject.h"
#include "vm/Realm.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/Realm-inl.h"
using namespace js;
namespace {
// Ephemeron-backed compiler/property origin, not a public function property.
// The specified public "anonymous" name is distinct from the factory's
// empty implicit debugger name, and from any authored same-value string.
class DynamicFunctionNameData : public NativeObject {
 public:
  static const JSClass class_;
  enum { IntrinsicName, SlotCount };
};
const JSClass DynamicFunctionNameData::class_ = {
    "NativeDynamicFunctionNameData",
    JSCLASS_HAS_RESERVED_SLOTS(DynamicFunctionNameData::SlotCount)};
DynamicFunctionNameData* Lookup(JSObject* function) {
  JSObject* data = ObjectRealm::get(function).getDynamicFunctionNameData(function);
  return data ? &data->as<DynamicFunctionNameData>() : nullptr;
}
}  // namespace
bool js::RegisterNativeDynamicFunctionName(JSContext* cx, JS::HandleFunction function) {
  RootedObject key(cx, function.get());
  Rooted<DynamicFunctionNameData*> data(
      cx, NewObjectWithGivenProto<DynamicFunctionNameData>(cx, nullptr));
  if (!data) return false;
  data->initReservedSlot(DynamicFunctionNameData::IntrinsicName, BooleanValue(true));
  return ObjectRealm::get(function).putDynamicFunctionNameData(cx, key, data);
}
bool js::HasNativeDynamicFunctionName(JSObject* function) { return Lookup(function) != nullptr; }
bool js::HasNativeIntrinsicFunctionName(JSObject* function) {
  auto* data = Lookup(function);
  return data && data->getReservedSlot(DynamicFunctionNameData::IntrinsicName).toBoolean();
}
void js::NoteNativeFunctionNameMutation(JSObject* function) {
  if (auto* data = Lookup(function)) {
    data->setReservedSlot(DynamicFunctionNameData::IntrinsicName, BooleanValue(false));
  }
}
