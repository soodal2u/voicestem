// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 soodal - VoiceStem (https://github.com/soodal2u/voicestem)

//! Minimal C ABI around DeepFilterNet3 (libDF tract runtime) with the model embedded.
//!
//! Unlike libDF's own capi this does not need a model file on disk and never unwinds
//! a panic into the host: failures are reported through return values instead.

use std::panic::{catch_unwind, AssertUnwindSafe};

use df::tract::{DfParams, DfTract, ReduceMask, RuntimeParams};
use ndarray::{ArrayView2, ArrayViewMut2};

// Model archives are fetched by CMake (see CMakeLists.txt) into VOICESTEM_MODEL_DIR.
static MODEL_STD: &[u8] =
    include_bytes!(concat!(env!("VOICESTEM_MODEL_DIR"), "/DeepFilterNet3_onnx.tar.gz"));
static MODEL_LL: &[u8] =
    include_bytes!(concat!(env!("VOICESTEM_MODEL_DIR"), "/DeepFilterNet3_ll_onnx.tar.gz"));

pub struct VsDf {
    m: DfTract,
}

/// Creates a mono DeepFilterNet3 instance. `low_latency` selects the DF3-LL model
/// (no look-ahead, bigger network). Returns null on failure.
#[no_mangle]
pub extern "C" fn vsdf_create(low_latency: bool, atten_lim_db: f32) -> *mut VsDf {
    let r = catch_unwind(|| {
        let rp = RuntimeParams::default_with_ch(1)
            .with_atten_lim(atten_lim_db)
            .with_thresholds(-15.0, 35.0, 35.0)
            .with_post_filter(0.0)
            .with_mask_reduce(ReduceMask::MAX);
        let params = DfParams::from_bytes(if low_latency { MODEL_LL } else { MODEL_STD }).ok()?;
        DfTract::new(params, &rp).ok()
    });
    match r {
        Ok(Some(m)) => Box::into_raw(Box::new(VsDf { m })),
        _ => std::ptr::null_mut(),
    }
}

/// Samples per call to `vsdf_process` (480 at 48 kHz).
#[no_mangle]
pub unsafe extern "C" fn vsdf_hop_size(st: *const VsDf) -> usize {
    st.as_ref().map_or(0, |s| s.m.hop_size)
}

/// Model sample rate (48000).
#[no_mangle]
pub unsafe extern "C" fn vsdf_sample_rate(st: *const VsDf) -> usize {
    st.as_ref().map_or(0, |s| s.m.sr)
}

/// Delay in samples between an input sample and its enhanced counterpart:
/// STFT overlap (fft - hop) plus the model's look-ahead frames.
#[no_mangle]
pub unsafe extern "C" fn vsdf_latency(st: *const VsDf) -> usize {
    st.as_ref().map_or(0, |s| {
        s.m.fft_size - s.m.hop_size + s.m.conv_lookahead * s.m.hop_size
    })
}

#[no_mangle]
pub unsafe extern "C" fn vsdf_set_atten_lim(st: *mut VsDf, lim_db: f32) {
    if let Some(s) = st.as_mut() {
        s.m.set_atten_lim(lim_db);
    }
}

/// Processes one hop of samples. Returns the local SNR estimate in dB,
/// or NaN if the frame failed (output is then zero-filled).
#[no_mangle]
pub unsafe extern "C" fn vsdf_process(st: *mut VsDf, input: *const f32, output: *mut f32) -> f32 {
    let Some(s) = st.as_mut() else { return f32::NAN };
    let n = s.m.hop_size;
    let r = catch_unwind(AssertUnwindSafe(|| {
        let i = ArrayView2::from_shape_ptr((1, n), input);
        let o = ArrayViewMut2::from_shape_ptr((1, n), output);
        s.m.process(i, o).ok()
    }));
    match r {
        Ok(Some(lsnr)) => lsnr,
        _ => {
            std::ptr::write_bytes(output, 0, n);
            f32::NAN
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn vsdf_free(st: *mut VsDf) {
    if !st.is_null() {
        drop(Box::from_raw(st));
    }
}
