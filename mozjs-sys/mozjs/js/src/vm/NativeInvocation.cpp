/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/NativeInvocation.h"

#include <algorithm>
#include "jit/JSJitFrameIter.h"
#include "jit/JitFrames.h"
#include "vm/EqualityOperations.h"
#include "vm/FrameIter.h"
#include "vm/JSContext.h"

#include "jit/JitFrames-inl.h"
#include "vm/FrameIter-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/Realm-inl.h"

using namespace js;

AutoNativeInvocation::AutoNativeInvocation(JSContext* cx, const JS::CallArgs& args,
                                         NativeInvocationPath path)
    : context_(cx), callee_(cx, &args.callee()), receiver_(cx, args.thisv()),
      arguments_(args.base()), path_(path), constructing_(args.isConstructing()) {
  if (!cx->realm()->creationOptions().v8ErrorStackLimits()) return;
  FrameIter caller(cx);
  if (!caller.done()) {
    caller_.activation = caller.activation();
    caller_.physicalFrame = caller.rawFramePtr();
    caller_.inlineFrameNo = caller.logicalInlineFrameNo();
    caller_.tier = caller.isInterp() ? 0 : caller.isWasm() ? 3 : caller.isIon() ? 2 : 1;
  }
  previous_ = cx->nativeInvocationTop;
  cx->nativeInvocationTop = this;
  linked_ = true;
}

AutoNativeInvocation::~AutoNativeInvocation() {
  if (linked_) {
    MOZ_ASSERT(context_->nativeInvocationTop == this);
    context_->nativeInvocationTop = previous_;
  }
}

AutoNativeTrampolineInvocation::AutoNativeTrampolineInvocation(JSContext* cx,
    const JS::CallArgs& args, Activation* activation)
    : cx_(cx), previous_(cx->nativeTrampolineInvocationTop), activation_(activation) {
  for (auto* scope = cx->nativeInvocationTop; scope; scope = scope->previous()) {
    if (scope->arguments() == args.base()) {
      scope_ = scope;
      priorDelegated_ = scope->delegated();
      scope->setDelegated(true);
      break;
    }
  }
  cx_->nativeTrampolineInvocationTop = this;
}
AutoNativeTrampolineInvocation::~AutoNativeTrampolineInvocation() {
  MOZ_ASSERT(cx_->nativeTrampolineInvocationTop == this);
  cx_->nativeTrampolineInvocationTop = previous_;
  if (scope_) scope_->setDelegated(priorDelegated_);
}

bool js::CaptureNativeInvocationSnapshots(JSContext* cx,
    JS::MutableHandle<NativeInvocationSnapshots> snapshots) {
  auto append = [&](JSObject* callee, const Value& receiver, const Value* vp,
                    NativeInvocationCaller caller, bool constructing) {
    if (!snapshots.get().emplaceBack(callee, receiver, vp, caller, constructing)) {
      ReportOutOfMemory(cx);
      return false;
    }
    return true;
  };
  // Scoped C++ re-entry can be younger than a native exit sharing its actual
  // logical caller. Keep the synchronous scope order, never sort by names.
  for (auto* scope = cx->nativeInvocationTop; scope; scope = scope->previous()) {
    if (!scope->delegated() && !append(scope->callee(), scope->receiver(),
          scope->arguments(), scope->caller(), scope->constructing())) return false;
  }
  for (ActivationIterator activation(cx); !activation.done(); ++activation) {
    if (!activation->isJit() || !activation->asJit()->hasJSExitFP()) continue;
    for (jit::JSJitFrameIter frame(activation->asJit()); !frame.done(); ++frame) {
      JSObject* callee = nullptr;
      Value receiver = UndefinedValue();
      const Value* vp = nullptr;
      bool constructing = false;
      if (frame.isExitFrameLayout<jit::NativeExitFrameLayout>()) {
        auto* native = frame.exitFrame()->as<jit::NativeExitFrameLayout>();
        const Value& function = native->invocationSnapshot()[0];
        if (!function.isObject()) continue;
        callee = &function.toObject(); receiver = native->invocationSnapshot()[1]; vp = native->vp();
        constructing = frame.isExitFrameLayout<jit::ConstructNativeExitFrameLayout>();
      } else if (frame.isExitFrameLayout<jit::IonOOLNativeExitFrameLayout>()) {
        auto* native = frame.exitFrame()->as<jit::IonOOLNativeExitFrameLayout>();
        const Value& function = native->invocationSnapshot()[0];
        if (!function.isObject()) continue;
        callee = &function.toObject(); receiver = native->invocationSnapshot()[1]; vp = native->vp();
      } else if (frame.isTrampolineNative()) {
        callee = frame.callee();
        receiver = reinterpret_cast<jit::JitFrameLayout*>(frame.fp())->thisv();
        constructing = frame.isConstructing();
      } else continue;

      RootedObject rootedCallee(cx, callee);
      RootedValue rootedReceiver(cx, receiver);

      NativeInvocationCaller key;
      auto caller = frame;
      do { ++caller; } while (!caller.done() && !caller.isScripted());
      if (!caller.done()) {
        key.activation = activation.activation(); key.physicalFrame = caller.fp();
        key.tier = caller.isIonScripted() ? 2 : 1;
        if (caller.isIonScripted()) {
          // Inline frame0 is the outermost, not the innermost caller.
          jit::InlineFrameIterator inlined(cx, &caller);
          key.inlineFrameNo = inlined.frameNo();
        }
      } else if (frame.isTrampolineNative()) {
        for (auto* delegate = cx->nativeTrampolineInvocationTop; delegate; delegate = delegate->previous()) {
          if (delegate->activation() == activation.activation() && delegate->scope()) {
            key = delegate->scope()->caller(); break;
          }
        }
      }
      if (!append(rootedCallee, rootedReceiver, vp, key, constructing)) return false;
    }
  }
  return true;
}

uint32_t js::ProbeNativeInvocationState(JSContext* cx, JS::HandleObject expected,
                                       JS::HandleValue receiver) {
  uint32_t flags = 0, count = 0;
  auto note = [&](JSObject* callee, const Value& actualReceiver,
                  const Value& currentResult, uint32_t origin, uint32_t tier,
                  bool constructing) {
    if (callee != expected) return;
    ++count;
    flags |= origin;
    if (!currentResult.isObject() || &currentResult.toObject() != expected) flags |= 8;
    if (!actualReceiver.isMagic()) {
      RootedValue actual(cx, actualReceiver);
      bool equal = false;
      if (js::StrictlyEqual(cx, actual, receiver, &equal) && equal) flags |= 16;
    }
    if (tier == 1) flags |= 32;
    if (tier == 2) flags |= 64;
    if (constructing) flags |= 128;
  };
  for (auto* scope = cx->nativeInvocationTop; scope; scope = scope->previous()) {
    uint32_t origin = 1;
    if (scope->path() == NativeInvocationPath::Getter) origin |= 1u << 16;
    if (scope->path() == NativeInvocationPath::Setter) origin |= 1u << 17;
    if (scope->caller().physicalFrame) origin |= 1u << 18;
    note(scope->callee(), scope->receiver(), scope->arguments()[0], origin,
         scope->caller().tier, scope->constructing());
  }
  for (ActivationIterator activation(cx); !activation.done(); ++activation) {
    if (!activation->isJit() || !activation->asJit()->hasJSExitFP()) continue;
    for (jit::JSJitFrameIter frame(activation->asJit()); !frame.done(); ++frame) {
      if (frame.isExitFrameLayout<jit::NativeExitFrameLayout>()) {
        auto* native = frame.exitFrame()->as<jit::NativeExitFrameLayout>();
        const Value& callee = native->invocationSnapshot()[0];
        if (!callee.isObject()) continue;
        auto caller = frame;
        do { ++caller; } while (!caller.done() && !caller.isScripted());
        uint32_t tier = caller.done() ? 0 : caller.isIonScripted() ? 2 : 1;
        note(&callee.toObject(), native->invocationSnapshot()[1], native->vp()[0],
             2, tier, frame.isExitFrameLayout<jit::ConstructNativeExitFrameLayout>());
      } else if (frame.isExitFrameLayout<jit::IonOOLNativeExitFrameLayout>()) {
        auto* native = frame.exitFrame()->as<jit::IonOOLNativeExitFrameLayout>();
        const Value& callee = native->invocationSnapshot()[0];
        if (!callee.isObject()) continue;
        auto caller = frame;
        do { ++caller; } while (!caller.done() && !caller.isScripted());
        uint32_t tier = caller.done() ? 0 : caller.isIonScripted() ? 2 : 1;
        uint32_t origin = 2 | (native->argc() == 0 ? 1u << 16 : 1u << 17);
        note(&callee.toObject(), native->invocationSnapshot()[1], native->vp()[0],
             origin, tier, false);
      } else if (frame.isTrampolineNative()) {
        auto* layout = reinterpret_cast<jit::JitFrameLayout*>(frame.fp());
        Value callee = ObjectValue(*frame.callee());
        auto caller = frame;
        do { ++caller; } while (!caller.done() && !caller.isScripted());
        uint32_t tier = caller.done() ? 0 : caller.isIonScripted() ? 2 : 1;
        note(frame.callee(), layout->thisv(), callee, 4, tier, frame.isConstructing());
      }
    }
  }
  return flags | (std::min(count, 255u) << 8);
}
