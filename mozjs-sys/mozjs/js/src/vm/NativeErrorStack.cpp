/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/NativeErrorStack.h"
#include <cstring>
#include "builtin/SelfHostingDefines.h"

#include "jsapi.h"
#include "js/CallArgs.h"
#include "js/CallAndConstruct.h"
#include "js/Conversions.h"
#include "js/Realm.h"
#include "js/Stack.h"
#include "js/String.h"
#include "js/Wrapper.h"
#include "util/StringBuilder.h"
#include "vm/GlobalObject.h"
#include "vm/ErrorObject.h"
#include "vm/JSFunction.h"
#include "vm/NativeObject.h"
#include "vm/EnvironmentObject.h"
#include "vm/FrameIter.h"
#include "vm/Interpreter.h"
#include "vm/JSScript.h"
#include "vm/NativeStackFrameData.h"
#include "vm/NativeInvocation.h"
#include "vm/NativePromiseStack.h"
#include "vm/GeneratorObject.h"
#include "vm/NativeCallSite.h"
#include "vm/NativeStackTraceDiagnostic.h"
#include "vm/SavedFrame.h"
#include "vm/SavedStacks.h"

#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/ObjectOperations-inl.h"
#include "vm/Realm-inl.h"
#include "vm/FrameIter-inl.h"

using namespace js;

const JSClass NativeStackFrameData::class_ = {
    "NativeStackFrameData",
    JSCLASS_HAS_RESERVED_SLOTS(NativeStackFrameData::SlotCount)};

const JSClass NativePromiseInvocationData::class_ = {
    "NativePromiseInvocationData",
    JSCLASS_HAS_RESERVED_SLOTS(NativePromiseInvocationData::SlotCount)};

// Shared by ordinary frame capture and async suspension: retain the actual
// invocation binding, including its already-initialized boxed sloppy receiver.
// No authored getter, source-name inspection, or guessed receiver is involved.
static bool CaptureNativeFrameReceiver(JSContext* cx, FrameIter& iter,
                                      MutableHandleValue receiver) {
  MOZ_ASSERT(iter.isFunctionFrame());
  AutoRealmUnchecked ar(cx, iter.realm());
  if (iter.isIon() && !iter.ensureHasRematerializedFrame(cx)) return false;
  RootedFunction callee(cx, iter.callee(cx));
  if (!callee) return false;
  MOZ_ASSERT(iter.hasUsableAbstractFramePtr());
  if (!callee->hasLexicalThis()) {
    return GetThisValueForDebuggerFrameMaybeOptimizedOut(
        cx, iter.abstractFramePtr(), iter.pc(), receiver);
  }
  // CallSite's arrow receiver is invocation state, not an evaluation of the
  // arrow's lexical this expression. Initialize the real slot once, preserving
  // boxed identity across suspension and repeated captures.
  receiver.set(iter.thisArgument(cx));
  if (!iter.script()->strict() && !receiver.isObject() && !receiver.isMagic()) {
    AbstractFramePtr frame = iter.abstractFramePtr();
    if (receiver.isNullOrUndefined()) {
      RootedObject environment(cx, frame.environmentChain());
      GetNonSyntacticGlobalThis(cx, environment, receiver);
    } else {
      RootedObject boxed(cx, BoxNonStrictThis(cx, receiver));
      if (!boxed) return false;
      receiver.setObject(*boxed);
    }
    frame.thisArgument() = receiver.get();
  }
  return true;
}

bool NativeStackFrameData::isEvalSource(ScriptSource* source) {
  if (!source || !source->hasIntroductionType()) return false;
  const char* kind = source->introductionType();
  return strcmp(kind, "eval") == 0 || strcmp(kind, "Function") == 0 ||
         strcmp(kind, "AsyncFunction") == 0 ||
         strcmp(kind, "GeneratorFunction") == 0 ||
         strcmp(kind, "AsyncGenerator") == 0;
}

bool NativeStackFrameData::capture(JSContext* cx, FrameIter& iter,
                                   MutableHandleObject result) {
  // Record identity must survive moving GC, and must not be shared merely
  // because a later invocation has the same source location and display name.
  SavedStacks::AutoReentrancyGuard guard(cx->realm()->savedStacks());
  RootedObject function(cx), source(cx);
  RootedValue receiver(cx, UndefinedValue());
  RootedValue scriptName(cx, UndefinedValue()), sourceURL(cx, UndefinedValue());
  uint32_t flags = 0;
  {
    AutoRealmUnchecked ar(cx, iter.realm());
    if (iter.isWasm()) {
      flags |= Wasm;
    } else {
      if (iter.isIon() && !iter.ensureHasRematerializedFrame(cx)) {
        return false;
      }
      source = iter.script()->sourceObject();
      if (iter.script()->strict()) {
        flags |= Strict;
      }
      if (iter.script()->isStackTransparent()) {
        flags |= StackTransparent;
      }
      ScriptSource* scriptSource = iter.script()->scriptSource();
      if (iter.isEvalFrame() || iter.script()->isForEval() ||
          isEvalSource(scriptSource)) {
        flags |= Eval;
      }
      // An introduced script has no original requested file name. Its derived
      // SpiderMonkey debugger location is not that name. Capture the distinct
      // compiler source-name and annotation fields while the source is live.
      if (!(flags & Eval) && scriptSource->filename()) {
        const char* filename = scriptSource->filename();
        RootedString name(cx, JS_NewStringCopyUTF8Z(
            cx, JS::ConstUTF8CharsZ(filename, std::strlen(filename))));
        if (!name) return false;
        scriptName.setString(name);
      }
      if (scriptSource->hasDisplayURL()) {
        RootedString url(cx, JS_NewUCStringCopyZ(cx, scriptSource->displayURL()));
        if (!url) return false;
        sourceURL.setString(url);
      }
      if (iter.isModuleFrame()) {
        flags |= Module;
      }
      if (iter.isFunctionFrame()) {
        // A template may belong to another invocation of a closure. Retain the
        // actual rematerialized callee rather than its display-name template.
        RootedFunction callee(cx, iter.callee(cx));
        if (!callee) {
          return false;
        }
        function = callee;
        flags |= HasFunction;
        if (callee->isSelfHostedBuiltin()) {
          flags |= SelfHosted;
          if (callee->isExtended()) {
            const Value& registered = callee->getExtendedSlot(PUBLIC_SELF_HOSTED_FUNCTION_SLOT);
            if (registered.isBoolean() && registered.toBoolean()) {
              flags |= PublicBuiltin;
            }
          }
        }
        if (iter.isConstructing()) {
          flags |= Constructing;
        }
        if (!CaptureNativeFrameReceiver(cx, iter, &receiver)) return false;
      } else if (!iter.isModuleFrame()) {
        MOZ_ASSERT(iter.hasUsableAbstractFramePtr());
        RootedObject environment(cx, iter.abstractFramePtr().environmentChain());
        GetNonSyntacticGlobalThis(cx, environment, &receiver);
      }
    }
  }
  // Keep genuinely unavailable/rematerialization values distinguishable from
  // a real undefined receiver. No public getter may return an engine MagicValue.
  flags |= receiverFlags(receiver);
  if (!cx->compartment()->wrap(cx, &function) ||
      !cx->compartment()->wrap(cx, &source) ||
      (!receiver.isMagic() && !cx->compartment()->wrap(cx, &receiver)) ||
      !cx->compartment()->wrap(cx, &scriptName) ||
      !cx->compartment()->wrap(cx, &sourceURL)) {
    return false;
  }
  Rooted<NativeStackFrameData*> record(
      cx, NewObjectWithGivenProto<NativeStackFrameData>(cx, nullptr));
  if (!record) {
    return false;
  }
  record->initReservedSlot(Function, ObjectOrNullValue(function));
  record->initReservedSlot(Receiver, receiver);
  record->initReservedSlot(SourceObject, ObjectOrNullValue(source));
  record->initReservedSlot(Flags, Int32Value(flags));
  record->initReservedSlot(ScriptName, scriptName);
  record->initReservedSlot(SourceURL, sourceURL);
  result.set(record);
  return true;
}

bool NativeStackFrameData::captureNative(JSContext* cx, const NativeInvocationSnapshot& invocation,
                                        MutableHandleObject result) {
  SavedStacks::AutoReentrancyGuard guard(cx->realm()->savedStacks());
  RootedObject function(cx, invocation.callee);
  RootedValue receiver(cx, invocation.receiver);
  uint32_t flags = HasFunction | PublicBuiltin | CppBuiltin | Strict;
  if (invocation.constructing) flags |= Constructing;
  if (receiver.isMagic()) flags |= ReceiverUnavailable;
  if (!cx->compartment()->wrap(cx, &function) ||
      (!receiver.isMagic() && !cx->compartment()->wrap(cx, &receiver))) return false;
  Rooted<NativeStackFrameData*> record(cx, NewObjectWithGivenProto<NativeStackFrameData>(cx, nullptr));
  if (!record) return false;
  record->initReservedSlot(Function, ObjectOrNullValue(function));
  record->initReservedSlot(Receiver, receiver);
  // An ordinary C++ builtin has no native-script source. Source provenance,
  // not its implementation language, determines CallSite.isNative().
  record->initReservedSlot(SourceObject, NullValue());
  record->initReservedSlot(Flags, Int32Value(flags));
  record->initReservedSlot(ScriptName, NullValue());
  record->initReservedSlot(SourceURL, NullValue());
  result.set(record);
  return true;
}

bool NativeStackFrameData::capturePromiseInvocation(JSContext* cx, FrameIter& iter,
                                                    MutableHandleObject result) {
  SavedStacks::AutoReentrancyGuard guard(cx->realm()->savedStacks());
  RootedValue receiver(cx, UndefinedValue());
  if (!CaptureNativeFrameReceiver(cx, iter, &receiver) ||
      (!receiver.isMagic() && !cx->compartment()->wrap(cx, &receiver))) return false;
  Rooted<NativePromiseInvocationData*> invocation(
      cx, NewObjectWithGivenProto<NativePromiseInvocationData>(cx, nullptr));
  if (!invocation) return false;
  invocation->initReservedSlot(NativePromiseInvocationData::Receiver, receiver);
  result.set(invocation);
  return true;
}

bool js::RecordNativePromiseGeneratorInvocation(JSContext* cx, HandleObject object) {
  if (!cx->realm()->creationOptions().v8ErrorStackLimits()) return true;
  MOZ_ASSERT(object->is<AbstractGeneratorObject>());
  // Synchronous generators never participate in the Promise continuation
  // graph. Their live frames still use ordinary capture when requested.
  RootedFunction expectedFunction(cx, &object->as<AbstractGeneratorObject>().callee());
  if (!expectedFunction->isAsync()) return true;
  FrameIter iter(cx);
  if (iter.done() || !iter.isFunctionFrame() || !iter.matchCallee(cx, expectedFunction)) return true;
  RootedObject invocation(cx);
  if (!NativeStackFrameData::capturePromiseInvocation(cx, iter, &invocation)) return false;
  // This private ephemeron retains the actual function invocation's receiver,
  // including an initialized boxed this. It does not create a generator-layout
  // slot or infer a receiver from names/globals after suspension.
  return ObjectRealm::get(object).putNativeBuiltinData(cx, object, invocation);
}

bool NativeStackFrameData::capturePromise(JSContext* cx,
    const NativePromiseStackSnapshot& snapshot, MutableHandleObject result) {
  SavedStacks::AutoReentrancyGuard guard(cx->realm()->savedStacks());
  RootedObject function(cx, snapshot.combinator), source(cx);
  RootedValue receiver(cx, snapshot.constructor ? ObjectValue(*snapshot.constructor) : UndefinedValue());
  RootedValue scriptName(cx, NullValue()), sourceURL(cx, NullValue());
  uint32_t flags = HasFunction | Async;
  if (snapshot.generator) {
    Rooted<AbstractGeneratorObject*> generator(cx, &snapshot.generator->as<AbstractGeneratorObject>());
    RootedScript script(cx);
    {
      AutoRealm ar(cx, generator);
      MOZ_ASSERT(generator->isSuspended());
      function = &generator->callee();
      script = generator->callee().nonLazyScript();
      source = script->sourceObject();
      if (script->strict()) flags |= Strict;
      if (script->isStackTransparent()) flags |= StackTransparent;
      ScriptSource* scriptSource = script->scriptSource();
      if (script->isForEval() || isEvalSource(scriptSource)) flags |= Eval;
      if (script->isModule()) flags |= Module;
      if (!(flags & Eval) && scriptSource->filename()) {
        const char* name = scriptSource->filename();
        RootedString value(cx, JS_NewStringCopyUTF8Z(cx, JS::ConstUTF8CharsZ(name, std::strlen(name))));
        if (!value) return false;
        scriptName.setString(value);
      }
      if (scriptSource->hasDisplayURL()) {
        RootedString value(cx, JS_NewUCStringCopyZ(cx, scriptSource->displayURL()));
        if (!value) return false;
        sourceURL.setString(value);
      }
      RootedObject invocation(cx, ObjectRealm::get(generator).getNativeBuiltinData(generator));
      if (invocation && invocation->is<NativePromiseInvocationData>()) {
        receiver = invocation->as<NativePromiseInvocationData>().getReservedSlot(
            NativePromiseInvocationData::Receiver);
      } else if (!GetThisValueForDebuggerSuspendedGeneratorMaybeOptimizedOut(cx, *generator, script, &receiver)) {
        return false;
      }
    }
  } else {
    MOZ_ASSERT(snapshot.kind != NativePromiseCombinatorKind::None);
    flags |= PromiseCombinator | PublicBuiltin;
  }
  if (receiver.isMagic()) flags |= ReceiverUnavailable;
  if (!cx->compartment()->wrap(cx, &function) || !cx->compartment()->wrap(cx, &source) ||
      (!receiver.isMagic() && !cx->compartment()->wrap(cx, &receiver)) ||
      !cx->compartment()->wrap(cx, &scriptName) || !cx->compartment()->wrap(cx, &sourceURL)) return false;
  Rooted<NativeStackFrameData*> record(cx, NewObjectWithGivenProto<NativeStackFrameData>(cx, nullptr));
  if (!record) return false;
  record->initReservedSlot(Function, ObjectOrNullValue(function));
  record->initReservedSlot(Receiver, receiver);
  record->initReservedSlot(SourceObject, ObjectOrNullValue(source));
  record->initReservedSlot(Flags, Int32Value(flags));
  record->initReservedSlot(ScriptName, scriptName);
  record->initReservedSlot(SourceURL, sourceURL);
  record->initReservedSlot(PromiseKind, Int32Value(uint32_t(snapshot.kind)));
  record->initReservedSlot(PromiseIndex, snapshot.generator ? NullValue() : NumberValue(snapshot.index));
  result.set(record);
  return true;
}

namespace {

class AutoNativeErrorFormatting {
  JSContext* context_;
  bool prior_;
 public:
  explicit AutoNativeErrorFormatting(JSContext* cx)
      : context_(cx), prior_(cx->nativeErrorStackFormatting) {
    cx->nativeErrorStackFormatting = true;
  }
  ~AutoNativeErrorFormatting() {
    context_->nativeErrorStackFormatting = prior_;
  }
};

class ErrorStackData : public NativeObject {
 public:
  static const JSClass class_;
  enum {
    Frames,
    CachedValue,
    Cached,
    OriginalMessage,
    PreferOriginalMessage,
    SlotCount
  };
};

const JSClass ErrorStackData::class_ = {
    "NativeErrorStackData", JSCLASS_HAS_RESERVED_SLOTS(ErrorStackData::SlotCount)};

ErrorStackData* LookupStackData(JSObject* obj) {
  JSObject* data = ObjectRealm::get(obj).getErrorStackData(obj);
  return data ? &data->as<ErrorStackData>() : nullptr;
}

// Use real property reads/conversions, retaining the captured original message
// for built-in errors. captureStackTrace targets instead read their message at
// formatting time. This must not invoke an overridden Error.prototype.toString.
bool ErrorHeader(JSContext* cx, HandleObject receiver,
                 Handle<ErrorStackData*> data, MutableHandleString result) {
  RootedValue value(cx);
  if (!GetProperty(cx, receiver, receiver, cx->names().name, &value)) {
    return false;
  }
  RootedString name(cx, value.isUndefined() ? cx->names().Error
                                           : ToString<CanGC>(cx, value));
  if (!name) {
    return false;
  }
  if (data && data->getReservedSlot(ErrorStackData::PreferOriginalMessage)
                  .toBoolean()) {
    value = data->getReservedSlot(ErrorStackData::OriginalMessage);
  } else if (!GetProperty(cx, receiver, receiver, cx->names().message, &value)) {
    return false;
  }
  RootedString message(cx, value.isUndefined() ? cx->runtime()->emptyString
                                              : ToString<CanGC>(cx, value));
  if (!message) {
    return false;
  }
  if (name->empty()) {
    result.set(message);
    return true;
  }
  if (message->empty()) {
    result.set(name);
    return true;
  }
  JSStringBuilder builder(cx);
  if (!builder.append(name) || !builder.append(": ") ||
      !builder.append(message)) {
    return false;
  }
  result.set(builder.finishString());
  return bool(result);
}

bool ErrorHeaderWithFallback(JSContext* cx, HandleObject receiver,
                             Handle<ErrorStackData*> data,
                             MutableHandleString result) {
  if (ErrorHeader(cx, receiver, data, result)) {
    return true;
  }
  // Termination/OOM must propagate. Only an authored header-conversion failure
  // follows the native default formatter's exception-string fallback.
  if (!cx->isExceptionPending() || cx->isThrowingOutOfMemory() ||
      cx->isThrowingOverRecursed()) {
    return false;
  }
  RootedValue exception(cx);
  if (!cx->getPendingException(&exception)) {
    return false;
  }
  cx->clearPendingException();
  RootedString exceptionString(cx);
  if (exception.isObject()) {
    RootedObject thrown(cx, &exception.toObject());
    Rooted<ErrorStackData*> thrownData(cx, LookupStackData(thrown));
    if (!ErrorHeader(cx, thrown, thrownData, &exceptionString)) {
      if (!cx->isExceptionPending() || cx->isThrowingOutOfMemory() ||
          cx->isThrowingOverRecursed()) {
        return false;
      }
      cx->clearPendingException();
    }
  }
  JSStringBuilder builder(cx);
  const bool started = exceptionString ? builder.append("<error: ")
                                        : builder.append("<error>");
  if (!started ||
      (exceptionString && (!builder.append(exceptionString) ||
                           !builder.append('>')))) {
    return false;
  }
  result.set(builder.finishString());
  return bool(result);
}

bool StackGetterImpl(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();
  if (!args.thisv().isObject()) {
    return true;
  }
  RootedObject receiver(cx, CheckedUnwrapStatic(&args.thisv().toObject()));
  if (!receiver) {
    ReportAccessDenied(cx);
    return false;
  }
  RootedValue value(cx);
  {
    JSAutoRealm ar(cx, receiver);
    Rooted<ErrorStackData*> data(cx, LookupStackData(receiver));
    if (!data) {
      return true;
    }
    if (!data->getReservedSlot(ErrorStackData::Cached).toBoolean()) {
      const bool useFormatter = !cx->nativeErrorStackFormatting;
      AutoNativeErrorFormatting restore(cx);
      RootedString header(cx), frames(cx);
      RootedObject stack(
          cx, data->getReservedSlot(ErrorStackData::Frames).toObjectOrNull());
      RootedObject constructor(cx, cx->global()->maybeGetConstructor(JSProto_Error));
      RootedValue formatter(cx);
      if (useFormatter && constructor &&
          !GetProperty(cx, constructor, constructor, cx->names().prepareStackTrace,
                       &formatter)) return false;
      // The V8 formatter hook dispatches an actual function, not an arbitrary
      // callable proxy. Inspect the real callable kind without page traps.
      RootedObject formatterKind(cx, formatter.isObject()
          ? CheckedUnwrapStatic(&formatter.toObject()) : nullptr);
      if (useFormatter && formatter.isObject() &&
          formatterKind && formatterKind->is<JSFunction>()) {
        RootedObject sites(cx);
        if (!BuildNativeCallSites(cx, stack, &sites)) return false;
        RootedValue thisValue(cx, ObjectValue(*constructor));
        JS::RootedValueArray<2> arguments(cx);
        arguments[0].setObject(*receiver);
        arguments[1].setObject(*sites);
        if (!JS::Call(cx, thisValue, formatter, arguments, &value)) {
          TraceNativeStackOutcome(18, false, UndefinedValue());
          return false;
        }
        TraceNativeStackOutcome(18, true, value);
      } else {
      if (!ErrorHeaderWithFallback(cx, receiver, data, &header) ||
          !FormatNativeErrorFrames(cx, stack, &frames)) {
        return false;
      }
      JSStringBuilder builder(cx);
      if (!builder.append(header) ||
          (!frames->empty() && (!builder.append('\n') ||
                                !builder.append(frames)))) {
        return false;
      }
      RootedString formatted(cx, builder.finishString());
      if (!formatted) {
        return false;
      }
      // Finish the in-flight read, even if an authored header getter assigned
      // .stack or recaptured frames. Commit to the receiver's current private
      // record, not a record that such a callback may have replaced.
      value.setString(formatted);
      TraceNativeStackOutcome(19, true, value);
      }
      Rooted<ErrorStackData*> current(cx, LookupStackData(receiver));
      if (current) {
        current->setReservedSlot(ErrorStackData::CachedValue, value);
        current->setReservedSlot(ErrorStackData::Cached, BooleanValue(true));
      }
    } else {
      value = data->getReservedSlot(ErrorStackData::CachedValue);
    }
  }
  if (!cx->compartment()->wrap(cx, &value)) {
    return false;
  }
  args.rval().set(value);
  return true;
}

bool StackGetter(JSContext* cx, unsigned argc, Value* vp) {
  bool success = StackGetterImpl(cx, argc, vp);
  CallArgs args = CallArgsFromVp(argc, vp);
  TraceNativeStackOutcome(17, success,
      success ? args.rval().get() : UndefinedValue());
  return success;
}

bool StackSetter(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();
  if (!args.thisv().isObject()) {
    return true;
  }
  RootedObject receiver(cx, CheckedUnwrapStatic(&args.thisv().toObject()));
  if (!receiver) {
    ReportAccessDenied(cx);
    return false;
  }
  RootedValue value(cx, args.get(0));
  JSAutoRealm ar(cx, receiver);
  if (!cx->compartment()->wrap(cx, &value)) {
    return false;
  }
  if (auto* data = LookupStackData(receiver)) {
    data->setReservedSlot(ErrorStackData::CachedValue, value);
    data->setReservedSlot(ErrorStackData::Cached, BooleanValue(true));
  }
  return true;
}

}  // namespace

bool js::ReadNativeErrorStackFrameValue(JSContext* cx, HandleObject receiver,
                                        uint32_t index, uint32_t field,
                                        MutableHandleValue result) {
  if (JS::GetObjectRealmOrNull(receiver) != cx->realm() ||
      field >= NativeStackFrameData::SlotCount) {
    return false;
  }
  Rooted<ErrorStackData*> data(cx, LookupStackData(receiver));
  if (!data) {
    return false;
  }
  RootedObject saved(cx, data->getReservedSlot(ErrorStackData::Frames)
                             .toObjectOrNull());
  if (!saved) {
    return false;
  }
  saved = CheckedUnwrapStatic(saved);
  if (!saved || !saved->is<SavedFrame>()) {
    return false;
  }
  Rooted<SavedFrame*> top(cx, &saved->as<SavedFrame>());
  for (Handle<SavedFrame*> frame : SavedFrame::RootedRange(cx, top)) {
    if (frame->getPrincipals() != cx->realm()->principals() ||
        frame->isSelfHosted(cx)) {
      continue;
    }
    if (index-- != 0) {
      continue;
    }
    RootedObject record(cx, frame->getNativeStackFrameData());
    if (!record || !(record = CheckedUnwrapStatic(record))) {
      return false;
    }
    {
      JSAutoRealm ar(cx, record);
      result.set(record->as<NativeStackFrameData>().getReservedSlot(field));
    }
    return !result.isMagic() && cx->compartment()->wrap(cx, result);
  }
  return false;
}

bool js::InstallNativeErrorStack(JSContext* cx, HandleObject receiver,
                                 HandleObject stack, bool enabled,
                                 HandleString originalMessage,
                                 bool preferOriginalMessage,
                                 bool initializingError) {
  // A same-origin cross-compartment wrapper is not the private-record key.
  // Enter the checked actual target's realm; its own shared accessors must
  // likewise belong to that realm. Ordinary JS proxies are not unwrapped.
  RootedObject target(cx, CheckedUnwrapStatic(receiver));
  if (!target) {
    ReportAccessDenied(cx);
    return false;
  }
  JSAutoRealm ar(cx, target);
  RootedObject savedFrames(cx, stack);
  RootedString message(cx, originalMessage);
  if (!cx->compartment()->wrap(cx, &savedFrames) ||
      (message && !cx->compartment()->wrap(cx, &message))) {
    return false;
  }
  // Recapturing frames must not overwrite the original built-in message.
  // Keep this lifetime independent of the current frame/cache generation.
  Rooted<ErrorStackData*> prior(cx, LookupStackData(target));
  if (!preferOriginalMessage && prior &&
      prior->getReservedSlot(ErrorStackData::PreferOriginalMessage).toBoolean()) {
    Value original = prior->getReservedSlot(ErrorStackData::OriginalMessage);
    message = original.isString() ? original.toString() : nullptr;
    preferOriginalMessage = true;
  }
  auto& globalData = cx->global()->data();
  if (!globalData.nativeErrorStackGetter) {
    RootedFunction getter(cx, NewNativeFunction(cx, StackGetter, 0, nullptr));
    RootedFunction setter(cx, NewNativeFunction(cx, StackSetter, 1, nullptr));
    if (!getter || !setter) {
      return false;
    }
    globalData.nativeErrorStackGetter = getter;
    globalData.nativeErrorStackSetter = setter;
  }
  Rooted<ErrorStackData*> data(
      cx, NewObjectWithGivenProto<ErrorStackData>(cx, nullptr));
  if (!data) {
    return false;
  }
  data->initReservedSlot(ErrorStackData::Frames, ObjectOrNullValue(savedFrames));
  data->initReservedSlot(ErrorStackData::CachedValue, UndefinedValue());
  data->initReservedSlot(ErrorStackData::Cached, BooleanValue(!enabled));
  data->initReservedSlot(ErrorStackData::OriginalMessage,
                          message ? StringValue(message) : UndefinedValue());
  data->initReservedSlot(ErrorStackData::PreferOriginalMessage,
                          BooleanValue(preferOriginalMessage));
  RootedObject getter(cx, globalData.nativeErrorStackGetter);
  RootedObject setter(cx, globalData.nativeErrorStackSetter);
  RootedId id(cx, NameToId(cx->names().stack));
  if (initializingError) {
    MOZ_ASSERT(target->is<ErrorObject>());
    Rooted<ErrorObject*> error(cx, &target->as<ErrorObject>());
    if (!ErrorObject::initStackAccessor(cx, error, getter, setter)) {
      return false;
    }
  } else {
    if (!DefineAccessorProperty(cx, target, id, getter, setter, 0)) {
      return false;
    }
  }
  return ObjectRealm::get(target).putErrorStackData(cx, target, data);
}
