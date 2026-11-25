//! Rust wrappers for mozjs's glue module

mod generated {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(unnecessary_transmutes)]
    include!(concat!(env!("OUT_DIR"), "/build/gluebindings.rs"));
}

use core::mem;

pub use generated::root::*;

pub type EncodedStringCallback = unsafe extern "C" fn(*const core::ffi::c_char);

/// Stable Galileo SpiderMonkey glue ABI carried by both the generated Rust
/// bindings and `libjsglue`.
pub const GALILEO_MOZJS_GLUE_ABI: u32 = 0x008C0C23;

// Keep an unconditional relocation to the versioned C symbol in every
// mozjs_sys build. A stale archive with newer generated bindings therefore
// fails at link time instead of reaching a latent ownership or JIT ABI bug.
#[used]
static GALILEO_MOZJS_GLUE_ABI_LINK_SENTINEL: unsafe extern "C" fn() -> u32 =
    GalileoMozjsGlueAbi_140_12_35;

/// Returns the ABI exported by the linked `libjsglue` archive.
#[inline]
pub fn galileo_mozjs_glue_abi() -> u32 {
    // SAFETY: the function takes no arguments and is supplied by the matching
    // `libjsglue` translation unit. The versioned symbol is the ABI contract.
    unsafe { GalileoMozjsGlueAbi_140_12_35() }
}

#[cfg(feature = "oom_simulator")]
unsafe extern "C" {
    #[link_name = "GalileoMozjsSimulateOomAfter"]
    fn galileo_mozjs_simulate_oom_after(checks: u64);
    #[link_name = "GalileoMozjsHadSimulatedOom"]
    fn galileo_mozjs_had_simulated_oom() -> bool;
    #[link_name = "GalileoMozjsStringCharsArenaOomWitness"]
    fn galileo_mozjs_string_chars_arena_oom_witness(
        cx: *mut crate::jsapi::JSContext,
        length: u32,
    ) -> bool;
    #[link_name = "GalileoMozjsArmStringCharsWipeObserver"]
    fn galileo_mozjs_arm_string_chars_wipe_observer(expected_bytes: u64);
    #[link_name = "GalileoMozjsStringCharsWipeObserverCount"]
    fn galileo_mozjs_string_chars_wipe_observer_count() -> u32;
    #[link_name = "GalileoMozjsStringCharsWipeObserverAllZero"]
    fn galileo_mozjs_string_chars_wipe_observer_all_zero() -> bool;
    #[link_name = "GalileoMozjsDisarmStringCharsWipeObserver"]
    fn galileo_mozjs_disarm_string_chars_wipe_observer();
    #[link_name = "GalileoMozjsResetOomSimulator"]
    fn galileo_mozjs_reset_oom_simulator();
}

/// Arm SpiderMonkey's native OOM simulator for a focused failure-path test.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_simulate_oom_after(checks: u64) {
    // SAFETY: the matching `libjsglue` translation unit owns this process-local
    // simulator and accepts any counter value.
    unsafe { galileo_mozjs_simulate_oom_after(checks) }
}

/// Report whether the simulator reached its configured failure point.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_had_simulated_oom() -> bool {
    // SAFETY: this is a read-only query of the matching native simulator.
    unsafe { galileo_mozjs_had_simulated_oom() }
}

/// Exercise StringChars' StringBufferArena allocation under the native OOM
/// simulator. Returns false when the allocation succeeds or the simulator is
/// unavailable in a non-test engine build.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_string_chars_arena_oom_witness(
    cx: *mut crate::jsapi::JSContext,
    length: u32,
) -> bool {
    // SAFETY: `cx` is the current live context supplied by the owning test
    // runtime, and the native helper does not retain it beyond this call.
    unsafe { galileo_mozjs_string_chars_arena_oom_witness(cx, length) }
}

/// Arm the test-only observer for a live temporary StringChars allocation.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_arm_string_chars_wipe_observer(expected_bytes: u64) {
    // SAFETY: this only updates process-local observer state.
    unsafe { galileo_mozjs_arm_string_chars_wipe_observer(expected_bytes) }
}

/// Return the number of matching live StringChars wipe callbacks observed.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_string_chars_wipe_observer_count() -> u32 {
    // SAFETY: this is a read-only query of process-local observer state.
    unsafe { galileo_mozjs_string_chars_wipe_observer_count() }
}

/// Return whether every matching observed allocation was zero at the callback.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_string_chars_wipe_observer_all_zero() -> bool {
    // SAFETY: this is a read-only query of process-local observer state.
    unsafe { galileo_mozjs_string_chars_wipe_observer_all_zero() }
}

/// Stop observing before the live native owner is released.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_disarm_string_chars_wipe_observer() {
    // SAFETY: this only updates process-local observer state.
    unsafe { galileo_mozjs_disarm_string_chars_wipe_observer() }
}

/// Reset the simulator before releasing Rust-side test owners.
#[cfg(feature = "oom_simulator")]
#[inline]
pub fn galileo_reset_oom_simulator() {
    // SAFETY: reset is required by SpiderMonkey's simulator contract.
    unsafe { galileo_mozjs_reset_oom_simulator() }
}

// manual glue stuff
unsafe impl Sync for ProxyTraps {}

impl Default for JobQueueTraps {
    fn default() -> JobQueueTraps {
        unsafe { mem::zeroed() }
    }
}

impl Default for ProxyTraps {
    fn default() -> ProxyTraps {
        unsafe { mem::zeroed() }
    }
}

impl Default for WrapperProxyHandler {
    fn default() -> WrapperProxyHandler {
        unsafe { mem::zeroed() }
    }
}

impl Default for ForwardingProxyHandler {
    fn default() -> ForwardingProxyHandler {
        unsafe { mem::zeroed() }
    }
}
