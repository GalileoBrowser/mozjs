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
fn native_resource_management_supports_real_builtins_disposal_and_using_syntax() {
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
            let script = format!(
                r#"(() => {{
                const compact = {compact};
                const check = (value, label) => {{ if (!value) throw new Error(label); }};
                const throws = (run, constructor, label) => {{
                    try {{ run(); }} catch (error) {{
                        check(error instanceof constructor, label + ': wrong error');
                        return;
                    }}
                    throw new Error(label + ': no error');
                }};
                const source = value => Function.prototype.toString.call(value);
                const suffix = compact ? '() {{ [native code] }}' : '() {{\n    [native code]\n}}';
                for (const [ctor, name] of [[DisposableStack,'DisposableStack'],
                    [AsyncDisposableStack,'AsyncDisposableStack'], [SuppressedError,'SuppressedError']]) {{
                    check(source(ctor) === 'function ' + name + suffix, name + ' is native');
                    const descriptor = Object.getOwnPropertyDescriptor(ctor, 'prototype');
                    check(!descriptor.writable && !descriptor.configurable && !descriptor.enumerable,
                        name + ' prototype attributes');
                }}
                check(typeof Symbol.dispose === 'symbol' && typeof Symbol.asyncDispose === 'symbol',
                    'native disposal symbols');
                const disposed = Object.getOwnPropertyDescriptor(DisposableStack.prototype, 'disposed').get;
                check(disposed.name === 'get disposed', 'disposed accessor initial name');
                check(source(disposed) === 'function ' + (compact ? 'get disposed' : 'disposed') + suffix,
                    'native disposed getter representation');
                const exception = SuppressedError(1, 2, 'message');
                check(Error.isError(exception) && exception.error === 1 && exception.suppressed === 2 &&
                    exception.message === 'message', 'SuppressedError callable and native error data');
                check(Object.hasOwn(SuppressedError.prototype, 'message') &&
                    SuppressedError.prototype.message === '', 'SuppressedError prototype message');
                const events = [];
                {{
                    using first = {{ [Symbol.dispose]() {{ events.push('first'); }} }};
                    using second = {{ [Symbol.dispose]() {{ events.push('second'); }} }};
                    events.push('body');
                }}
                check(events.join(',') === 'body,second,first', 'using scope disposal order');
                const bodyError = {{}}, disposeError = {{}};
                try {{
                    using resource = {{ [Symbol.dispose]() {{ throw disposeError; }} }};
                    throw bodyError;
                }} catch (error) {{
                    check(error instanceof SuppressedError && error.error === disposeError &&
                        error.suppressed === bodyError, 'native suppressed disposal errors');
                }}
                const stack = new DisposableStack();
                const order = [];
                stack.defer(() => order.push(1));
                stack.defer(() => order.push(2));
                const moved = stack.move();
                check(stack.disposed && !moved.disposed, 'move transfers state');
                moved.dispose(); moved.dispose();
                check(moved.disposed && order.join(',') === '2,1', 'stack order and idempotence');
                check(DisposableStack.prototype[Symbol.dispose] === DisposableStack.prototype.dispose,
                    'sync disposal alias identity');
                check(AsyncDisposableStack.prototype[Symbol.asyncDispose] === AsyncDisposableStack.prototype.disposeAsync,
                    'async disposal alias identity');
                let returned = 0;
                const iterator = Iterator.from({{next() {{ return {{value:1,done:false}}; }},
                    return() {{ returned++; return {{done:true}}; }} }});
                iterator[Symbol.dispose]();
                check(returned === 1, 'iterator disposal calls return');
                new Function('for (using of = null;;) break;')();
                for (const prefix of ['using', 'await using']) {{
                    for (const pattern of ['[]', '{{}}']) {{
                        const body = prefix + ' x = null, ' + pattern + ' = null;';
                        throws(() => new Function(prefix === 'using' ? body :
                            'return async function(){{' + body + '}}'), SyntaxError,
                            'resource declarations reject destructuring');
                    }}
                }}
                throws(() => eval('using x = null;'), SyntaxError,
                    'using is forbidden at eval top level');
                for (const body of [
                    'using x = null; x = undefined;',
                    'for (using x = null; x === null; x = undefined) {{}}',
                    'for (using x of [null]) {{ x = undefined; }}',
                    'using x = null; (() => {{ x = undefined; }})();',
                    'using x = null; eval("x = undefined");',
                ]) {{
                    throws(new Function(body), TypeError, 'resource binding is immutable');
                }}
                // Compile the actual syntax in the native parser. Browser tests
                // additionally execute it with the embedding's real job queue.
                const asynchronous = async function() {{
                    await using resource = {{async [Symbol.asyncDispose]() {{}}}};
                }};
                check(typeof asynchronous === 'function', 'await using parser support');
                return true;
            }})()"#
            );
            rooted!(&in(context) let mut result = UndefinedValue());
            let options =
                CompileOptionsWrapper::new(context, c"native-resource-management.js".to_owned(), 0);
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
