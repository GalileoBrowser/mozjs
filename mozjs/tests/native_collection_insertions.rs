/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::JS_NewGlobalObject;
use mozjs::rust::{
    CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS, evaluate_script,
};

#[test]
fn native_collection_insertions_keep_semantics_and_realm_function_policy() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    for compact in [false, true] {
        let mut options = RealmOptions::default();
        options.creationOptions_.compactNativeFunctionSource_ = compact;
        unsafe {
            rooted!(&in(context) let global = JS_NewGlobalObject(
                context, &SIMPLE_GLOBAL_CLASS, ptr::null_mut(),
                OnNewGlobalHookOption::FireOnNewGlobalHook, &*options,
            ));
            assert!(!global.is_null());
            let mut realm = AutoRealm::new_from_handle(context, global.handle());
            let context = &mut realm;
            let script = format!("const compact = {compact};\n")
                + r#"
(() => {
    const check = (value, label) => { if (!value) throw new Error(label); };
    const throwsTypeError = run => {
        try { run(); } catch (error) { check(error instanceof TypeError, 'wrong exception'); return; }
        throw new Error('TypeError required');
    };
    const suffix = compact ? '() { [native code] }' : '() {\n    [native code]\n}';
    for (const Ctor of [Map, WeakMap]) {
        for (const name of ['getOrInsert', 'getOrInsertComputed']) {
            const d = Object.getOwnPropertyDescriptor(Ctor.prototype, name);
            check(d && d.writable && d.configurable && !d.enumerable, 'native method descriptor');
            const method = d.value;
            check(method.name === name && method.length === 2, 'method metadata');
            check(Function.prototype.toString.call(method) === 'function ' + name + suffix,
                'native function representation in owning realm');
            throwsTypeError(() => Reflect.construct(function(){}, [], method));
            throwsTypeError(() => method.call({}, {}, () => 1));
        }
    }
    const map = new Map();
    check(map.getOrInsert(NaN, 7) === 7 && map.getOrInsert(NaN, 9) === 7, 'SameValueZero key');
    map.set('undefined', undefined);
    check(map.getOrInsert('undefined', 9) === undefined, 'present undefined is not replaced');
    throwsTypeError(() => map.getOrInsertComputed('undefined', 9));
    let calls = 0;
    check(map.getOrInsertComputed('undefined', () => { calls++; }) === undefined && calls === 0,
        'present key does not invoke callback');
    check(map.getOrInsertComputed(-0, function(key) {
        'use strict';
        check(this === undefined && arguments.length === 1 && Object.is(key, +0),
            'canonical callback key and receiver');
        map.set(+0, 3);
        return 11;
    }) === 11 && map.get(+0) === 11, 'callback mutation is overwritten by returned value');
    const weak = new WeakMap(), key = {};
    check(weak.getOrInsert(key, 17) === 17 && weak.getOrInsert(key, 18) === 17,
        'weak insertion preserves existing value');
    // Symbol weak keys are a separate missing feature in the pinned engine.
    // The full browser Test262 run retains every symbols-as-weakmap-keys case;
    // this smoke test covers insertion algorithms over supported object keys.
    const computedKey = {};
    check(weak.getOrInsertComputed(computedKey, value => value === computedKey ? 19 : 0) === 19,
        'computed weak insertion passes its key to the callback');
    throwsTypeError(() => weak.getOrInsert(Symbol.for('registered-key'), 1));
    let invalidCallbackCalls = 0;
    for (const invalidKey of [42, null, Symbol.for('registered-computed-key')]) {
        throwsTypeError(() => weak.getOrInsertComputed(invalidKey, () => { invalidCallbackCalls++; return 1; }));
    }
    check(invalidCallbackCalls === 0, 'invalid weak key is rejected before callback execution');
    return true;
})()
"#;
            rooted!(&in(context) let mut result = UndefinedValue());
            let options = CompileOptionsWrapper::new(
                context,
                c"native-collection-insertions.js".to_owned(),
                0,
            );
            assert!(
                evaluate_script(
                    context,
                    global.handle(),
                    &script,
                    result.handle_mut(),
                    options
                )
                .is_ok()
            );
            assert!(result.is_boolean() && result.to_boolean());
        }
    }
}
