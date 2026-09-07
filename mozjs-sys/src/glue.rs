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
pub const GALILEO_MOZJS_GLUE_ABI: u32 = 0x008C0C02;

// Keep an unconditional relocation to the versioned C symbol in every
// mozjs_sys build. A stale archive with newer generated bindings therefore
// fails at link time instead of reaching a latent ownership or JIT ABI bug.
#[used]
static GALILEO_MOZJS_GLUE_ABI_LINK_SENTINEL: unsafe extern "C" fn() -> u32 =
    GalileoMozjsGlueAbi_140_12_2;

/// Returns the ABI exported by the linked `libjsglue` archive.
#[inline]
pub fn galileo_mozjs_glue_abi() -> u32 {
    // SAFETY: the function takes no arguments and is supplied by the matching
    // `libjsglue` translation unit. The versioned symbol is the ABI contract.
    unsafe { GalileoMozjsGlueAbi_140_12_2() }
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
