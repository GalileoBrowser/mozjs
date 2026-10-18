/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

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
fn native_serialization_uses_metadata_and_preserves_authored_source_per_realm() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();

    // Revisit both policies in one engine. The common compact policy must not
    // expose private stack APIs or leak into the following Firefox realm.
    for compact in [true, false, true, false] {
        let mut options = RealmOptions::default();
        options.creationOptions_.compactNativeFunctionSource_ = compact;
        assert!(!options.creationOptions_.v8ErrorStackLimits_);
        unsafe {
            rooted!(&in(context) let global = JS_NewGlobalObject(
                context, &SIMPLE_GLOBAL_CLASS, ptr::null_mut(),
                OnNewGlobalHookOption::FireOnNewGlobalHook, &*options,
            ));
            assert!(!global.is_null());
            let mut realm = AutoRealm::new_from_handle(context, global.handle());
            let context = &mut realm;
            let script = r#"(() => {
                const compact = COMPACT_POLICY;
                let assertion = 0, failed = 0;
                const check = condition => { assertion++; if (!condition && !failed) failed=assertion; };
                const intrinsic=Function.prototype.toString;
                const source=fn=>Reflect.apply(intrinsic,fn,[]);
                const native=name=>'function '+name+'() { [native code] }';
                const legacy=name=>'function'+(name?' '+name:'')+'() {\n    [native code]\n}';
                check(source(Math.max)===(compact?native('max'):legacy('max')));
                // Firefox distinguishes its explicitly empty-named intrinsic
                // from a bound function with no native name metadata.
                check(source(Function.prototype)===(compact?native(''):'function () {\n    [native code]\n}'));
                const getter=Object.getOwnPropertyDescriptor(Map.prototype,'size').get;
                check(source(getter)===(compact?native('get size'):legacy('size')));
                const species=Object.getOwnPropertyDescriptor(Array,Symbol.species).get;
                check(source(species)===(compact?native('get [Symbol.species]'):legacy('[Symbol.species]')));
                for (const fn of [Math.max,function authored(a){return a;}]) {
                    const bound=fn.bind(null);
                    check(source(bound)===(compact?native(''):legacy('')));
                    let traps=0;
                    const proxy=new Proxy(fn,{get(){traps++;throw new Error('no proxy reads');}});
                    check(source(proxy)===(compact?native(''):'function () {\n    [native code]\n}'));
                    check(traps===0);
                }
                const revoked=Proxy.revocable(function authored(){},{});revoked.revoke();
                check(source(revoked.proxy)===(compact?native(''):'function () {\n    [native code]\n}'));
                for (const fn of [Math.max,getter]) {
                    const original=Object.getOwnPropertyDescriptor(fn,'name'),before=source(fn);
                    Object.defineProperty(fn,'name',{value:'replacement',configurable:true});
                    check(source(fn)===before);
                    let reads=0;
                    Object.defineProperty(fn,'name',{get(){reads++;throw new Error('no name reads');},configurable:true});
                    check(source(fn)===before && reads===0);
                    delete fn.name;
                    check(source(fn)===before);
                    Object.defineProperty(fn,'name',original);
                }
                check(source(function  authored /* keep */ ( a ) { return a; })===
                    'function  authored /* keep */ ( a ) { return a; }');
                check(source(( a ) => /* keep */ a+1)==='( a ) => /* keep */ a+1');
                const object={ method /* keep */ (a) { return a; },get value /* keep */ () {return 1;} };
                check(source(object.method)==='method /* keep */ (a) { return a; }');
                check(source(Object.getOwnPropertyDescriptor(object,'value').get)==='get value /* keep */ () {return 1;}');
                check(source(class  Authored /* keep */ { })==='class  Authored /* keep */ { }');
                check(source(async function authored (a) { return a; })==='async function authored (a) { return a; }');
                check(source(function * authored (a) { yield a; })==='function * authored (a) { yield a; }');
                check(source(async function * authored (a) { yield a; })==='async function * authored (a) { yield a; }');
                for (const receiver of [undefined,null,{},1,'x']) {
                    let name;try { source(receiver); }catch(error){name=error.name;}
                    check(name==='TypeError');
                }
                return failed || (assertion===30 ? 0 : 1000+assertion);
            })()"#
            .replace("COMPACT_POLICY", if compact { "true" } else { "false" });
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
                .is_ok(),
                "native-source evaluation failed in compact={compact} realm"
            );
            assert!(result.is_int32());
            assert_eq!(
                result.to_int32(),
                0,
                "failed source assertion in compact={compact} realm"
            );
        }
    }
}
