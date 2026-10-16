/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::ptr;

use mozjs::glue::Servo_UsesCompactNativeFunctionSource;
use mozjs::jsapi::OnNewGlobalHookOption;
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::JS_NewGlobalObject;
use mozjs::rust::{
    CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS, evaluate_script,
};

#[test]
fn native_source_policy_is_realm_local_and_preserves_real_source_and_metadata() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    for compact in [true, false, true, false] {
        let mut options = RealmOptions::default();
        options.creationOptions_.compactNativeFunctionSource_ = compact;
        // Source formatting must not accidentally enable private stack APIs.
        assert!(!options.creationOptions_.v8ErrorStackLimits_);
        unsafe {
            rooted!(&in(context) let global = JS_NewGlobalObject(
                context, &SIMPLE_GLOBAL_CLASS, ptr::null_mut(),
                OnNewGlobalHookOption::FireOnNewGlobalHook, &*options,
            ));
            assert!(!global.is_null());
            assert_eq!(Servo_UsesCompactNativeFunctionSource(global.get()), compact);
            assert!(!Servo_UsesCompactNativeFunctionSource(ptr::null_mut()));
            let mut realm = AutoRealm::new_from_handle(context, global.handle());
            let context = &mut realm;
            let script = format!(
                r#"(() => {{
                const compact = {compact};
                const source = f => Function.prototype.toString.call(f);
                const check = (condition, label) => {{
                    if (!condition) throw new Error('native source assertion: ' + label);
                }};
                const suffix = compact ? '() {{ [native code] }}' : '() {{\n    [native code]\n}}';
                for (const [f, name] of [[Object,'Object'], [Array,'Array'], [Function,'Function'],
                    [Promise,'Promise'], [JSON.parse,'parse'], [Math.sin,'sin'],
                    [Object.getOwnPropertyDescriptor,'getOwnPropertyDescriptor'], [Reflect.apply,'apply'],
                    [Map.prototype.get,'get'], [Set.prototype.has,'has'], [Function.prototype.bind,'bind']]) {{
                    check(source(f) === 'function ' + name + suffix, 'internal initial name ' + name);
                }}
                const getter = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype,'byteLength').get;
                check(source(getter) === 'function ' + (compact ? 'get byteLength' : 'byteLength') + suffix,
                    'native accessor initial prefix');
                const original = Object.getOwnPropertyDescriptor(JSON.parse,'name');
                const before = source(JSON.parse);
                let reads = 0;
                Object.defineProperty(JSON.parse,'name',{{
                    get() {{ reads++; throw new Error('public name must not be read'); }}, configurable:true,
                }});
                check(source(JSON.parse) === before && reads === 0, 'no mutable public name access');
                Object.defineProperty(JSON.parse,'name',original);
                const boundSource = compact ? 'function () {{ [native code] }}' : 'function() {{\n    [native code]\n}}';
                check(source(Object.bind(null)) === boundSource, 'bound native source hidden');
                const authored = function Authored( arg ) {{ /* preserve bytes */ return arg; }};
                check(source(authored) === 'function Authored( arg ) {{ /* preserve bytes */ return arg; }}',
                    'authored named source');
                check(source(authored.bind(null)) === boundSource, 'bound authored source hidden');
                const proxySource = compact ? 'function () {{ [native code] }}' : 'function () {{\n    [native code]\n}}';
                let traps = 0;
                const proxy = new Proxy(authored,{{get() {{ traps++; throw new Error('no trap'); }}}});
                check(source(proxy) === proxySource && traps === 0, 'callable proxy source without traps');
                const revoked = Proxy.revocable(authored,{{}}); revoked.revoke();
                check(source(revoked.proxy) === proxySource, 'revoked callable proxy source');
                const arrow = ( arg ) => {{ /* preserve arrow */ return arg; }};
                check(source(arrow) === '( arg ) => {{ /* preserve arrow */ return arg; }}', 'arrow source');
                const generator = function* NamedGenerator(arg) {{ yield arg; }};
                check(source(generator) === 'function* NamedGenerator(arg) {{ yield arg; }}', 'generator source');
                const asynchronous = async function NamedAsync(arg) {{ return arg; }};
                check(source(asynchronous) === 'async function NamedAsync(arg) {{ return arg; }}', 'async source');
                const klass = class NamedClass {{ method(arg) {{ return arg; }} }};
                check(source(klass) === 'class NamedClass {{ method(arg) {{ return arg; }} }}', 'class source');
                const accessors = {{ get value() {{ return 7; }}, set value(arg) {{ void arg; }} }};
                check(source(Object.getOwnPropertyDescriptor(accessors,'value').get) === 'get value() {{ return 7; }}',
                    'authored getter source');
                check(source(Object.getOwnPropertyDescriptor(accessors,'value').set) === 'set value(arg) {{ void arg; }}',
                    'authored setter source');
                let throws = 0;
                for (const value of [null, undefined, 1, 'x', {{}}, []]) {{
                    try {{ source(value); }} catch(error) {{ if (error instanceof TypeError) throws++; }}
                }}
                check(throws === 6, 'non-callable receiver validation');
                return true;
            }})()"#
            );
            rooted!(&in(context) let mut result = UndefinedValue());
            let compile_options =
                CompileOptionsWrapper::new(context, c"native-function-source.js".to_owned(), 0);
            assert!(
                evaluate_script(
                    context,
                    global.handle(),
                    &script,
                    result.handle_mut(),
                    compile_options
                )
                .is_ok()
            );
            assert!(result.is_boolean() && result.to_boolean());
        }
    }
}
