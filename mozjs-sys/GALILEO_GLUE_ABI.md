# Galileo SpiderMonkey glue ABI 140.12.2

This branch extends the exact `mozjs 0.20.0` / `mozjs_sys 140.12.0-2`
source state at commit `f5cbf8aa6` without changing SpiderMonkey or its public
Rust API version. It moves Galileo's ownership, proxy, isolated-realm, JIT
teardown, and WebAssembly preference helpers into the same `libjsglue` archive
and generated `gluebindings.rs` as the rest of rust-mozjs.

`GalileoMozjsGlueAbi_140_12_2` is the link contract. The generated Rust module
keeps an unconditional relocation to that symbol, and the integration test
calls it. These checks make all stale combinations fail deterministically:

- new bindings plus old `libjsglue` fail to link;
- old bindings fail to compile code that names the sentinel and Servo helpers;
- a prebuilt without the declaration is rejected before it becomes the active
  build directory;
- a missing or rejected automatic prebuilt falls back to this branch's exact
  vendored SpiderMonkey source.

## Authoritative release

Automatic downloads use the Galileo repository, never Servo's upstream release
assets:

```text
repository:  GalileoBrowser/mozjs
release tag: galileo-mozjs-glue-abi-140.12.2
asset name:  libmozjs-<Rust target>[-debugmozjs-O3].tar.gz
```

The standard mozjs archive creator already packages `libjsglue`/`jsglue.lib`
and `gluebindings.rs`; no sidecar library or copied private header is allowed.
`verify_galileo_glue_archives.py` verifies both the declaration and the actual
archive symbol before release.

The release must contain these platform assets:

| Platform | Release targets | Debug assets |
| --- | --- | --- |
| Linux | `x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu` | both, `-debugmozjs-O3` |
| Windows | `x86_64-pc-windows-msvc`, `aarch64-pc-windows-msvc` | both, `-debugmozjs-O3` |
| macOS | `x86_64-apple-darwin`, `aarch64-apple-darwin` | both, `-debugmozjs-O3` |
| Android | `armv7-linux-androideabi`, `aarch64-linux-android`, `x86_64-linux-android` | no release requirement |
| OpenHarmony | `aarch64-unknown-linux-ohos`, `x86_64-unknown-linux-ohos` | build/link check only |

The `Galileo glue ABI` workflow builds the existing cross-platform matrix from
source, runs `mozjs-sys/tests/galileo_glue_abi.rs` wherever the target can run
tests (and builds tests for OpenHarmony), and stages one verified artifact set.
After it succeeds:

1. Review and tag the tested commit as
   `galileo-mozjs-glue-abi-140.12.2`.
2. Create a GitHub release for that tag and attach every required archive from
   the staged `galileo-mozjs-glue-release-assets` workflow artifact.
3. Generate GitHub build-provenance attestations for all attached archives.
4. Run `verify_galileo_glue_archives.py --require-release-matrix` on the
   downloaded release assets.

Do not run the upstream `Publish` workflow for this branch: the crate versions
intentionally match the existing crates.io packages. Galileo consumes the fork
by immutable Git revision and the fork-specific GitHub release supplies its
native archives.

## GalileoEngine integration

Pin the reviewed commit through Cargo's registry patch so the registry
`mozjs 0.20.0` and direct `mozjs_sys` users resolve to one
`mozjs_sys 140.12.0-2` implementation:

```toml
[patch.crates-io]
mozjs_sys = { git = "https://github.com/GalileoBrowser/mozjs", rev = "<reviewed-commit>" }
```

Then regenerate `Cargo.lock`, remove `components/script_bindings/mozjs_dispatch.cpp`
and its target-directory-scanning build step, and import the helpers from
`js::glue`. Call `js::glue::galileo_mozjs_glue_abi()` during script-runtime
initialization and compare it with `js::glue::GALILEO_MOZJS_GLUE_ABI`; this is
an additional runtime assertion on top of the unconditional link sentinel.

## Frontend-only script preparation

ABI140.12.2 adds an opaque frontend context and stencil bridge. Create the
frontend while the engine is initialized; compile on a helper with that helper's
actual stack-size quota. Never pass a page JSContext to the compile entry point.
The ReadOnlyCompileOptions must have owning lifetime (including its filename)
through error conversion. The input source is borrowed during compilation only;
the produced stencil retains its own script source. No frontend/stencil may be
accessed concurrently.

Instantiate on the owning script thread in the correct realm. Convert native
frontend errors there, retain normal compile options, and root the resulting
JSScript immediately. Release the stencil and frontend on success, parser error,
navigation cancellation and shutdown. Retain an engine lifetime guard until all
native cleanup is finished, and join outstanding helpers before engine shutdown.

This branch does not publish a crate or release automatically. New platform
archives must satisfy the same matrix and ABI verifier before publication.
