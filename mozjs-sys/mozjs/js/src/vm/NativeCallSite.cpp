/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/NativeCallSite.h"
#include <cstring>
#include "jsapi.h"
#include "builtin/Array.h"
#include "js/CallArgs.h"
#include "js/Conversions.h"
#include "js/PropertySpec.h"
#include "js/String.h"
#include "js/Wrapper.h"
#include "js/friend/StackLimits.h"
#include "js/friend/WindowProxy.h"
#include "proxy/Proxy.h"
#include "util/StringBuilder.h"
#include "vm/GlobalObject.h"
#include "vm/ArrayObject.h"
#include "vm/JSFunction.h"
#include "vm/JSScript.h"
#include "vm/NativeObject.h"
#include "vm/Compartment.h"
#include "vm/NativeStackFrameData.h"
#include "vm/NativeFunctionDebugName.h"
#include "vm/NativeBuiltinMetadata.h"
#include "vm/NativeStackTraceDiagnostic.h"
#include "vm/PlainObject.h"
#include "vm/SavedFrame.h"
#include "vm/SavedStacks.h"
#include "vm/Shape.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/JSScript-inl.h"
#include "vm/ObjectOperations-inl.h"
using namespace js;

namespace {
class NativeCallSite : public NativeObject {
 public:
  static const JSClass class_;
  enum { Frame, HideObjects, SlotCount };
};
const JSClass NativeCallSite::class_ = {
    "CallSite", JSCLASS_HAS_RESERVED_SLOTS(NativeCallSite::SlotCount)};
enum class Method {
  This, Function, FunctionName, TypeName, MethodName, FileName, ScriptName,
  Line, Column, EvalOrigin, Toplevel, Eval, Native, Constructor, Async,
  PromiseAll, PromiseIndex, String
};

// Native pure reads never invoke authored name getters or proxy traps.
bool FunctionName(JSContext* cx, HandleObject function, MutableHandleValue out,
                  bool stackFrame = false) {
  out.setNull();
  if (!function) return true;
  RootedObject actual(cx, CheckedUnwrapStatic(function));
  if (!actual || !actual->is<JSFunction>()) return true;
  RootedValue name(cx);
  {
    JSAutoRealm ar(cx, actual);
    bool dynamic = stackFrame && HasNativeDynamicFunctionName(actual);
    bool intrinsic = dynamic && HasNativeIntrinsicFunctionName(actual);
    if (intrinsic) {
      // A constructor-generated public name is not an authored debugger name.
      // Reads/materialization do not alter this actual property provenance.
      name.setNull();
    } else if (!GetPropertyPure(cx, actual, NameToId(cx->names().name), name.address()) ||
        !name.isString()) {
      if (dynamic) {
        // Pure access skips authored getters. Non-string data and absent data
        // fall back to the actual factory's empty implicit debugger name.
        name.setNull();
      } else {
        Rooted<JSAtom*> atom(cx);
        if (!actual->as<JSFunction>().getDisplayAtom(cx, &atom)) return false;
        name = atom && !atom->empty() ? StringValue(atom) : NullValue();
      }
    }
    if (name.isString() && name.toString()->empty()) name.setNull();
  }
  if (!cx->compartment()->wrap(cx, &name)) return false;
  out.set(name);
  return true;
}
bool IsGlobalReceiver(HandleValue receiver) {
  if (!receiver.isObject()) return false;
  JSObject* actual = CheckedUnwrapStatic(&receiver.toObject());
  return actual && (actual->is<GlobalObject>() || js::IsWindowProxy(actual));
}
bool TypeName(JSContext* cx, HandleValue receiver, MutableHandleValue out) {
  out.setNull();
  if (receiver.isNullOrUndefined() || receiver.isMagic() ||
      IsGlobalReceiver(receiver)) return true;
  RootedObject object(cx, ToObject(cx, receiver));
  if (!object) return false;
  RootedObject actual(cx, CheckedUnwrapStatic(object));
  if (!actual) return true;
  if (actual->is<ProxyObject>()) {
    RootedString name(cx, JS_NewStringCopyZ(cx, "Proxy"));
    if (!name) return false;
    out.setString(name);
    return true;
  }
  if (actual->is<JSFunction>()) {
    if (!FunctionName(cx, actual, out)) return false;
    if (out.isString()) return true;
  }
  RootedValue constructor(cx);
  {
    JSAutoRealm ar(cx, actual);
    if (!GetPropertyPure(cx, actual, NameToId(cx->names().constructor),
                         constructor.address())) constructor.setUndefined();
  }
  if (!cx->compartment()->wrap(cx, &constructor)) return false;
  if (constructor.isObject()) {
    RootedObject function(cx, &constructor.toObject());
    if (!FunctionName(cx, function, out)) return false;
    if (out.isString()) return true;
  }
  RootedString name(cx, JS_NewStringCopyZ(cx, actual->getClass()->name));
  if (!name) return false;
  out.setString(name);
  return true;
}
bool SameFunction(JSObject* candidate, JSObject* function) {
  return candidate && CheckedUnwrapStatic(candidate) == function;
}
bool MethodName(JSContext* cx, HandleValue receiver, HandleObject function,
                MutableHandleValue out) {
  out.setNull();
  if (!function || receiver.isNullOrUndefined() || receiver.isMagic()) return true;
  RootedObject object(cx, ToObject(cx, receiver));
  RootedObject actualFunction(cx, CheckedUnwrapStatic(function));
  if (!object) return false;
  if (!actualFunction) return true;
  object = CheckedUnwrapStatic(object);
  if (!object || !object->is<NativeObject>()) return true;
  Rooted<JSAtom*> exactName(cx, actualFunction->is<JSFunction>()
      ? actualFunction->as<JSFunction>().maybePartialDisplayAtom() : nullptr);
  RootedString inferred(cx);
  bool ambiguous = false;
  {
    JS::AutoCheckCannotGC nogc;
    // Raw shape/prototype inspection stops at proxies and cannot run page code.
    for (JSObject* current = object; current && current->is<NativeObject>();
         current = current->staticPrototype()) {
      auto& native = current->as<NativeObject>();
      for (ShapePropertyIter<NoGC> it(native.shape()); !it.done(); it++) {
        auto property = it.get();
        jsid id = property.key();
        if (!id.isString()) continue;
        bool match = false;
        if (property.isAccessorProperty()) {
          match = SameFunction(native.getGetter(property), actualFunction) ||
                  SameFunction(native.getSetter(property), actualFunction);
        } else if (property.isDataProperty()) {
          const Value& value = native.getSlot(property.slot());
          match = value.isObject() && SameFunction(&value.toObject(), actualFunction);
        }
        if (!match) continue;
        if (exactName && id.toAtom() == exactName) {
          out.setString(exactName);
          return true;
        }
        if (!property.enumerable()) continue;
        JSString* name = id.toString();
        if (inferred && inferred != name) ambiguous = true;
        else inferred = name;
      }
    }
  }
  if (inferred && !ambiguous) out.setString(inferred);
  return cx->compartment()->wrap(cx, out);
}
bool AppendNumber(JSContext* cx, JSStringBuilder& builder, uint32_t number) {
  RootedValue value(cx, NumberValue(number));
  RootedString text(cx, ToString<CanGC>(cx, value));
  return text && builder.append(text);
}
bool EvalOrigin(JSContext* cx, HandleObject source, MutableHandleValue out) {
  out.setUndefined();
  if (!source) return true;
  RootedObject actual(cx, CheckedUnwrapStatic(source));
  if (!actual || !actual->is<ScriptSourceObject>()) return true;
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) return false;
  RootedString origin(cx);
  {
    JSAutoRealm ar(cx, actual);
    auto& object = actual->as<ScriptSourceObject>();
    ScriptSource* scriptSource = object.source();
    if (scriptSource->hasDisplayURL()) {
      origin = JS_NewUCStringCopyZ(cx, scriptSource->displayURL());
      if (!origin) return false;
    } else {
      Rooted<BaseScript*> intro(cx, object.unwrappedIntroductionScript());
      Rooted<JSAtom*> name(cx);
      if (intro && intro->function() &&
          !intro->function()->getDisplayAtom(cx, &name)) return false;
      JSStringBuilder builder(cx);
      if (!builder.append("eval at ") ||
          !(name ? builder.append(name) : builder.append("<anonymous>"))) return false;
      if (intro) {
        if (!builder.append(" (")) return false;
        if (NativeStackFrameData::isEvalSource(intro->scriptSource())) {
          RootedObject parentSource(cx, intro->sourceObject());
          RootedValue parentOrigin(cx);
          if (!EvalOrigin(cx, parentSource, &parentOrigin)) return false;
          if (!(parentOrigin.isString() ? builder.append(parentOrigin.toString())
                                       : builder.append("unknown source"))) return false;
        } else {
          const char* filename = intro->scriptSource()->filename();
          RootedString file(cx, filename ? JS_NewStringCopyUTF8Z(
              cx, JS::ConstUTF8CharsZ(filename, std::strlen(filename))) : nullptr);
          if (filename && !file) return false;
          if (!(file ? builder.append(file) : builder.append("unknown source"))) return false;
          if (intro->hasBytecode() && scriptSource->hasIntroductionOffset()) {
            JSScript* script = intro->asJSScript();
            uint32_t offset = scriptSource->introductionOffset();
            if (offset < script->length()) {
              JS::LimitedColumnNumberOneOrigin column;
              uint32_t line = PCToLineNumber(script, script->offsetToPC(offset), &column);
              if (!builder.append(':') || !AppendNumber(cx, builder, line) ||
                  !builder.append(':') ||
                  !AppendNumber(cx, builder, column.oneOriginValue())) return false;
            }
          }
        }
        if (!builder.append(')')) return false;
      }
      origin = builder.finishString();
      if (!origin) return false;
    }
  }
  out.setString(origin);
  return cx->compartment()->wrap(cx, out);
}

bool Read(JSContext* cx, Handle<NativeCallSite*> site, Method method,
          MutableHandleValue out) {
  RootedObject frameObject(cx, CheckedUnwrapStatic(
      &site->getReservedSlot(NativeCallSite::Frame).toObject()));
  if (!frameObject || !frameObject->is<SavedFrame>()) {
    ReportAccessDenied(cx); return false;
  }
  Rooted<SavedFrame*> frame(cx, &frameObject->as<SavedFrame>());
  out.setNull();
  if (!CanAccessNativeErrorStackFrame(cx, cx->realm()->principals(), frame)) {
    if (method == Method::This || method == Method::Function) out.setUndefined();
    return true;
  }
  RootedObject record(cx, frame->getNativeStackFrameData()), function(cx), source(cx);
  RootedValue receiver(cx, UndefinedValue());
  RootedValue scriptName(cx, UndefinedValue()), sourceURL(cx, UndefinedValue());
  RootedValue promiseIndex(cx, NullValue());
  NativePromiseCombinatorKind promiseKind = NativePromiseCombinatorKind::None;
  bool hasRecord = false;
  uint32_t flags = 0;
  if (record && (record = CheckedUnwrapStatic(record))) {
    JSAutoRealm ar(cx, record);
    auto& data = record->as<NativeStackFrameData>();
    hasRecord = true;
    function = data.getReservedSlot(NativeStackFrameData::Function).toObjectOrNull();
    source = data.getReservedSlot(NativeStackFrameData::SourceObject).toObjectOrNull();
    receiver = data.getReservedSlot(NativeStackFrameData::Receiver);
    flags = data.getReservedSlot(NativeStackFrameData::Flags).toInt32();
    scriptName = data.getReservedSlot(NativeStackFrameData::ScriptName);
    sourceURL = data.getReservedSlot(NativeStackFrameData::SourceURL);
    if (flags & NativeStackFrameData::PromiseCombinator) {
      promiseKind = NativePromiseCombinatorKind(data.getReservedSlot(NativeStackFrameData::PromiseKind).toInt32());
      promiseIndex = data.getReservedSlot(NativeStackFrameData::PromiseIndex);
    }
  }
  if (!cx->compartment()->wrap(cx, &function) || !cx->compartment()->wrap(cx, &source) ||
      (!receiver.isMagic() && !cx->compartment()->wrap(cx, &receiver)) ||
      !cx->compartment()->wrap(cx, &scriptName) ||
      !cx->compartment()->wrap(cx, &sourceURL)) return false;
  bool async = frame->getAsyncCause() != nullptr || (flags & NativeStackFrameData::Async);
  // Async describes where the frame was captured, not a strict-mode or
  // principals privacy boundary. Keep the existing checked access and the
  // younger-strict-frame boundary without hiding real sloppy async identities.
  bool hide = site->getReservedSlot(NativeCallSite::HideObjects).toBoolean();
  bool eval = flags & NativeStackFrameData::Eval;
  bool constructing = flags & NativeStackFrameData::Constructing;
  switch (method) {
    case Method::This:
      out.set(hide || receiver.isMagic() ? UndefinedValue() : receiver.get()); return true;
    case Method::Function:
      out.set(!hide && function ? ObjectValue(*function) : UndefinedValue()); return true;
    case Method::FunctionName:
      if (!FunctionName(cx, function, out, true)) return false;
      if (!function && out.isNull() && frame->getFunctionDisplayName())
        out.setString(frame->getFunctionDisplayName());
      if (out.isNull() && eval) out.setString(cx->names().eval);
      return true;
    case Method::TypeName:
      // Constructor frames are not method calls. Keep their real receiver for
      // getThis; do not expose its class through method-call type metadata.
      if (constructing) return true;
      if ((flags & NativeStackFrameData::CppBuiltin) && receiver.isObject()) {
        RootedObject object(cx, CheckedUnwrapStatic(&receiver.toObject()));
        RootedValue tag(cx);
        if (object && object->is<NativeObject>()) {
          JSAutoRealm ar(cx, object);
          jsid key = PropertyKey::Symbol(cx->wellKnownSymbols().toStringTag);
          if (GetPropertyPure(cx, object, key, tag.address()) && tag.isString()) out.set(tag);
        }
        if (out.isString()) return cx->compartment()->wrap(cx, out);
      }
      return TypeName(cx, receiver, out);
    case Method::MethodName: return MethodName(cx, receiver, function, out);
    case Method::FileName:
      if (flags & NativeStackFrameData::PublicBuiltin) return true;
      if (hasRecord && !(flags & NativeStackFrameData::Wasm)) {
        out.set(scriptName); return true;
      }
      if (!eval && frame->getSource() && !frame->getSource()->empty())
        out.setString(frame->getSource());
      return true;
    case Method::ScriptName:
      if (flags & NativeStackFrameData::PublicBuiltin) return true;
      if (hasRecord && !(flags & NativeStackFrameData::Wasm)) {
        out.set(sourceURL.isString() ? sourceURL.get() : scriptName.get()); return true;
      }
      if (frame->getSource() && !frame->getSource()->empty()) out.setString(frame->getSource());
      return true;
    case Method::Line:
      if (flags & NativeStackFrameData::PublicBuiltin) return true;
      if (frame->getLine()) out.setNumber(frame->getLine()); return true;
    case Method::Column:
      if (flags & NativeStackFrameData::PublicBuiltin) return true;
      if (!frame->isWasm()) {
        uint32_t column = frame->getColumn().toLimitedColumnNumber().oneOriginValue();
        if (column) out.setNumber(column);
      }
      return true;
    case Method::EvalOrigin:
      return eval ? EvalOrigin(cx, source, out) : (out.setUndefined(), true);
    case Method::Toplevel:
      out.setBoolean(receiver.isNullOrUndefined() ||
          (flags & NativeStackFrameData::UninitializedReceiver) ||
          (!receiver.isMagic() && IsGlobalReceiver(receiver))); return true;
    case Method::Eval: out.setBoolean(eval); return true;
    case Method::Native:
      if (flags & NativeStackFrameData::PromiseCombinator) {
        out.setBoolean(source != nullptr); return true;
      }
      if (flags & NativeStackFrameData::CppBuiltin) {
        // No native-script source is attached to an ordinary C++ builtin.
        out.setBoolean(source != nullptr);
        return true;
      }
      if (function) {
        RootedObject actual(cx, CheckedUnwrapStatic(function));
        out.setBoolean(actual && actual->is<JSFunction>() &&
                       actual->as<JSFunction>().isNativeFun());
      } else out.setBoolean(false);
      return true;
    case Method::Constructor: out.setBoolean(constructing); return true;
    case Method::Async: out.setBoolean(async); return true;
    case Method::PromiseAll:
      out.setBoolean(async && promiseKind == NativePromiseCombinatorKind::All); return true;
    case Method::PromiseIndex:
      if (async && (flags & NativeStackFrameData::PromiseCombinator)) out.set(promiseIndex);
      return true;
    case Method::String: break;
  }
  RootedValue name(cx), file(cx), line(cx), column(cx), type(cx), methodName(cx),
      topLevel(cx), origin(cx);
  if (!Read(cx, site, Method::FunctionName, &name) ||
      !Read(cx, site, Method::ScriptName, &file) || !Read(cx, site, Method::Line, &line) ||
      !Read(cx, site, Method::Column, &column) ||
      !Read(cx, site, Method::TypeName, &type) ||
      !Read(cx, site, Method::MethodName, &methodName) ||
      !Read(cx, site, Method::Toplevel, &topLevel) ||
      !Read(cx, site, Method::EvalOrigin, &origin)) return false;
  JSStringBuilder builder(cx);
  bool named = name.isString() && !name.toString()->empty();
  bool methodCall = !constructing && !topLevel.toBoolean();
  bool label = constructing || methodCall || named;
  if (async && !builder.append("async ")) return false;
  if (async && (flags & NativeStackFrameData::PromiseCombinator)) {
    // Formatting consumes the real retained factory/index record, not a
    // guessed function name or an invented Promise boundary.
    if (!builder.append("Promise.") ||
        !(named ? builder.append(name.toString()) : builder.append("<anonymous>")) ||
        !builder.append(" (index ") || !AppendNumber(cx, builder, uint32_t(promiseIndex.toNumber())) ||
        !builder.append(')')) return false;
    out.setString(builder.finishString());
    return out.isString();
  }
  if (constructing && !builder.append("new ")) return false;
  if (methodCall && type.isString() &&
      (!builder.append(type.toString()) || !builder.append('.'))) return false;
  if (label) {
    if (named) {
      if (!builder.append(name.toString())) return false;
      if (methodCall && methodName.isString()) {
        RootedString a(cx, name.toString()), b(cx, methodName.toString());
        bool equal;
        if (!EqualStrings(cx, a, b, &equal)) return false;
        if (!equal && (!builder.append(" [as ") || !builder.append(b) ||
                       !builder.append(']'))) return false;
      }
    } else if (!(methodName.isString() ? builder.append(methodName.toString())
                                      : builder.append("<anonymous>"))) return false;
    if (!builder.append(" (")) return false;
  }
  // The origin comes from the captured source's introduction metadata, not
  // from a guessed URL or an authored function display-name string.
  if (eval && origin.isString() &&
      (!builder.append(origin.toString()) || !builder.append(", "))) return false;
  if (!(file.isString() ? builder.append(file.toString())
                       : builder.append("<anonymous>"))) return false;
  if (line.isNumber() && (!builder.append(':') ||
      !AppendNumber(cx, builder, uint32_t(line.toNumber())))) return false;
  if (column.isNumber() && (!builder.append(':') ||
      !AppendNumber(cx, builder, uint32_t(column.toNumber())))) return false;
  if (label && !builder.append(')')) return false;
  RootedString text(cx, builder.finishString());
  if (!text) return false;
  out.setString(text);
  return true;
}
bool DispatchImpl(JSContext* cx, unsigned argc, Value* vp, Method method) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject object(cx, args.thisv().isObject()
      ? CheckedUnwrapStatic(&args.thisv().toObject()) : nullptr);
  if (!object || !object->is<NativeCallSite>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
        JSMSG_INCOMPATIBLE_PROTO, "CallSite", "method", "receiver");
    return false;
  }
  Rooted<NativeCallSite*> site(cx, &object->as<NativeCallSite>());
  RootedValue result(cx);
  if (!Read(cx, site, method, &result) ||
      !cx->compartment()->wrap(cx, &result)) return false;
  args.rval().set(result);
  return true;
}
bool Dispatch(JSContext* cx, unsigned argc, Value* vp, Method method) {
  bool success = DispatchImpl(cx, argc, vp, method);
  CallArgs args = CallArgsFromVp(argc, vp);
  TraceNativeStackOutcome(unsigned(method) + 1, success,
      success ? args.rval().get() : UndefinedValue());
  return success;
}
#define CALLSITE_METHOD(name, kind) \
  bool name(JSContext* cx, unsigned argc, Value* vp) { \
    return Dispatch(cx, argc, vp, Method::kind); \
  }
CALLSITE_METHOD(getThis, This)
CALLSITE_METHOD(getFunction, Function)
CALLSITE_METHOD(getFunctionName, FunctionName)
CALLSITE_METHOD(getTypeName, TypeName)
CALLSITE_METHOD(getMethodName, MethodName)
CALLSITE_METHOD(getFileName, FileName)
CALLSITE_METHOD(getScriptNameOrSourceURL, ScriptName)
CALLSITE_METHOD(getLineNumber, Line)
CALLSITE_METHOD(getColumnNumber, Column)
CALLSITE_METHOD(getEvalOrigin, EvalOrigin)
CALLSITE_METHOD(isToplevel, Toplevel)
CALLSITE_METHOD(isEval, Eval)
CALLSITE_METHOD(isNative, Native)
CALLSITE_METHOD(isConstructor, Constructor)
CALLSITE_METHOD(isAsync, Async)
CALLSITE_METHOD(isPromiseAll, PromiseAll)
CALLSITE_METHOD(getPromiseIndex, PromiseIndex)
CALLSITE_METHOD(toString, String)
#undef CALLSITE_METHOD
const JSFunctionSpec methods[] = {
    JS_FN("getThis", getThis, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getFunction", getFunction, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getFunctionName", getFunctionName, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getTypeName", getTypeName, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getMethodName", getMethodName, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getFileName", getFileName, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getScriptNameOrSourceURL", getScriptNameOrSourceURL, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getLineNumber", getLineNumber, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getColumnNumber", getColumnNumber, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getEvalOrigin", getEvalOrigin, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isToplevel", isToplevel, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isEval", isEval, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isNative", isNative, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isConstructor", isConstructor, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isAsync", isAsync, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("isPromiseAll", isPromiseAll, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("getPromiseIndex", getPromiseIndex, 0, JSPROP_READONLY | JSPROP_PERMANENT),
    JS_FN("toString", toString, 0, JSPROP_READONLY | JSPROP_PERMANENT), JS_FS_END};
}

bool js::BuildNativeCallSites(JSContext* cx, HandleObject stack, MutableHandleObject out) {
  RootedValueVector values(cx);
  auto& global = cx->global()->data();
  if (!global.nativeCallSitePrototype) {
    Rooted<PlainObject*> prototype(cx, NewPlainObject(cx));
    if (!prototype || !JS_DefineFunctions(cx, prototype, methods)) return false;
    global.nativeCallSitePrototype = prototype;
  }
  RootedObject prototype(cx, global.nativeCallSitePrototype);
  RootedObject actual(cx, stack ? CheckedUnwrapStatic(stack) : nullptr);
  if (stack && !actual) { ReportAccessDenied(cx); return false; }
  Rooted<SavedFrame*> top(cx, actual ? &actual->as<SavedFrame>() : nullptr);
  bool hideObjects = false;
  if (top) for (Handle<SavedFrame*> frame : SavedFrame::RootedRange(cx, top)) {
    if (!CanAccessNativeErrorStackFrame(cx, cx->realm()->principals(), frame)) continue;
    RootedObject record(cx, frame->getNativeStackFrameData());
    bool promiseCombinator = false;
    if (record && (record = CheckedUnwrapStatic(record))) {
      JSAutoRealm ar(cx, record);
      hideObjects |= record->as<NativeStackFrameData>().hasFlag(NativeStackFrameData::Strict);
      promiseCombinator = record->as<NativeStackFrameData>().hasFlag(NativeStackFrameData::PromiseCombinator);
    }
    RootedObject wrappedFrame(cx, frame);
    if (!cx->compartment()->wrap(cx, &wrappedFrame)) return false;
    Rooted<NativeCallSite*> site(cx, NewObjectWithGivenProto<NativeCallSite>(cx, prototype));
    if (!site) return false;
    site->initReservedSlot(NativeCallSite::Frame, ObjectValue(*wrappedFrame));
    // Combinator logical frames retain native constructor/function identity;
    // unlike generator frames their own flags do not inherit strictness.
    site->initReservedSlot(NativeCallSite::HideObjects, BooleanValue(hideObjects && !promiseCombinator));
    if (!values.append(ObjectValue(*site))) return false;
  }
  out.set(NewDenseCopiedArray(cx, values.length(), values.begin()));
  return bool(out);
}

bool js::FormatNativeErrorFrames(JSContext* cx, HandleObject stack,
                                 MutableHandleString out) {
  RootedObject sites(cx);
  if (!BuildNativeCallSites(cx, stack, &sites)) return false;
  Rooted<ArrayObject*> array(cx, &sites->as<ArrayObject>());
  JSStringBuilder builder(cx);
  for (uint32_t index = 0; index < array->length(); index++) {
    Rooted<NativeCallSite*> site(cx,
        &array->getDenseElement(index).toObject().as<NativeCallSite>());
    RootedValue text(cx);
    // Call the native renderer directly: page changes to a retained CallSite
    // prototype must not redirect the engine's default formatting path.
    if (!Read(cx, site, Method::String, &text) ||
        (index && !builder.append('\n')) || !builder.append("    at ") ||
        !builder.append(text.toString())) return false;
  }
  out.set(builder.finishString());
  return bool(out);
}
