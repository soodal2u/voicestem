// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 soodal - VoiceStem (https://github.com/soodal2u/voicestem)

#pragma once

#include <cstddef>

// C ABI of rust/src/lib.rs (DeepFilterNet3 via libDF's tract runtime).
extern "C"
{
    struct VsDf;

    VsDf* vsdf_create (bool lowLatency, float attenLimDb);
    size_t vsdf_hop_size (const VsDf*);
    size_t vsdf_sample_rate (const VsDf*);
    size_t vsdf_latency (const VsDf*);
    void vsdf_set_atten_lim (VsDf*, float limDb);
    float vsdf_process (VsDf*, const float* input, float* output);
    void vsdf_free (VsDf*);
}
