/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use mozjs_sys::glue::{
    GALILEO_MOZJS_GLUE_ABI, GalileoMozjsGlueAbi_140_12_2, Servo_CancelOffThreadCompilesForObject,
    Servo_CompileFrontendScript, Servo_DeleteFrontendContext, Servo_EnableWasmPromiseIntegration,
    Servo_FinishFrontendScript, Servo_InvokeProxyGetOwnPropertyDescriptor,
    Servo_NewFrontendContext, Servo_NukeRealmWrappers, Servo_PrepareObjectZoneForGC,
    Servo_ReleaseFailedDispatchable, Servo_ReleaseFrontendStencil, Servo_SetWasmJSTagEnumerable,
    galileo_mozjs_glue_abi,
};

#[test]
fn generated_bindings_expose_the_complete_galileo_surface() {
    // Inferred function items deliberately make this a compile-time check of
    // gluebindings.rs without restating any SpiderMonkey-private Rust types.
    let _ = GalileoMozjsGlueAbi_140_12_2;
    let _ = Servo_NewFrontendContext;
    let _ = Servo_DeleteFrontendContext;
    let _ = Servo_CompileFrontendScript;
    let _ = Servo_FinishFrontendScript;
    let _ = Servo_ReleaseFrontendStencil;
    let _ = Servo_ReleaseFailedDispatchable;
    let _ = Servo_EnableWasmPromiseIntegration;
    let _ = Servo_SetWasmJSTagEnumerable;
    let _ = Servo_InvokeProxyGetOwnPropertyDescriptor;
    let _ = Servo_NukeRealmWrappers;
    let _ = Servo_PrepareObjectZoneForGC;
    let _ = Servo_CancelOffThreadCompilesForObject;
}

#[test]
fn linked_archive_matches_the_generated_galileo_abi() {
    // Calling the versioned symbol makes a stale libjsglue/new-bindings pair a
    // deterministic link failure; the value catches an incorrectly aliased
    // or future ABI implementation.
    assert_eq!(galileo_mozjs_glue_abi(), GALILEO_MOZJS_GLUE_ABI);
}
