// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace Shader {

/// EDEN_NX2_FRAG_PROBE=<shader hash>[:class|nan|inf|val:<ordinal>[:<shift>]|
/// flt:<ordinal>[:<exp>[:<nth>]]|fbt:...]: the fragment shader with that hash (frame dump hash)
/// writes a diagnosis to colour target 0 instead of its colour.
/// class: magenta NaN, yellow Inf, cyan negative, red zero, green otherwise (of the rgb it wrote).
/// nan, inf: the ordinal of the first scalar float instruction whose result was NaN or Inf.
/// val: bits (>> shift) of the instruction with that ordinal, last value it had.
/// flt: that value as a number times 2^exp: red when positive, green when negative, blue 1 when
/// NaN or Inf (integers and booleans are converted; fbt reads integers as float bits). With nth,
/// the value of the instruction's nth execution (1 = first) instead of its last.
/// Ordinals and values are three base-32 digits, r low, each stored as digit / 32; alpha is 1.
/// EDEN_NX2_FRAG_PROBE_DIR=<dir> gets frag_probe_<hash>.txt (ordinal, SPIR-V id, opcode) and .spv.
/// <dir>/probe.txt, when it exists, replaces the value while the game runs.
struct FragProbe {
    enum class Mode { Class, Nan, Inf, Val, Flt };

    u64 hash{};
    Mode mode{Mode::Class};
    u32 ordinal{};
    u32 shift{};
    s32 exponent{};
    u32 nth{};
    bool integer_bits{};
    std::string directory;
};

[[nodiscard]] inline std::optional<FragProbe> ParseFragProbe(const std::string& text) {
    FragProbe result;
    char* end{};
    result.hash = std::strtoull(text.c_str(), &end, 16);
    if (end - text.c_str() != 16) {
        return std::nullopt;
    }
    const std::string rest{end};
    if (rest.starts_with(":nan")) {
        result.mode = FragProbe::Mode::Nan;
    } else if (rest.starts_with(":inf")) {
        result.mode = FragProbe::Mode::Inf;
    } else if (rest.starts_with(":val:") || rest.starts_with(":flt:") ||
               rest.starts_with(":fbt:")) {
        const bool is_val{rest[1] == 'v'};
        result.mode = is_val ? FragProbe::Mode::Val : FragProbe::Mode::Flt;
        result.ordinal = static_cast<u32>(std::strtoul(rest.c_str() + 5, &end, 10));
        if (*end == ':' && is_val) {
            result.shift = static_cast<u32>(std::strtoul(end + 1, nullptr, 10)) & 31;
        } else if (*end == ':') {
            result.exponent = static_cast<s32>(std::strtol(end + 1, &end, 10));
            if (*end == ':') {
                result.nth = static_cast<u32>(std::strtoul(end + 1, nullptr, 10));
            }
        }
        result.integer_bits = rest[2] == 'b';
    }
    return result;
}

namespace Detail {
struct FragProbeState {
    FragProbeState() {
        const char* const text = std::getenv("EDEN_NX2_FRAG_PROBE");
        enabled = text && *text != '\0';
        if (!enabled) {
            return;
        }
        if (const char* const dir = std::getenv("EDEN_NX2_FRAG_PROBE_DIR")) {
            directory = dir;
        }
        Set(text);
    }

    void Set(const std::string& new_text) {
        text = new_text;
        probe = ParseFragProbe(text);
        if (probe) {
            probe->directory = directory;
        }
    }

    bool enabled{};
    std::mutex mutex;
    std::string directory;
    std::string text;
    std::optional<FragProbe> probe;
};

[[nodiscard]] inline FragProbeState& GetFragProbeState() {
    static FragProbeState state;
    return state;
}
} // namespace Detail

/// True when EDEN_NX2_FRAG_PROBE was set at startup.
[[nodiscard]] inline bool FragProbeEnabled() {
    return Detail::GetFragProbeState().enabled;
}

[[nodiscard]] inline std::optional<FragProbe> GetFragProbe() {
    auto& state{Detail::GetFragProbeState()};
    if (!state.enabled) {
        return std::nullopt;
    }
    std::scoped_lock lock{state.mutex};
    return state.probe;
}

/// Reads <dir>/probe.txt again. Returns the shader hashes whose pipelines have to be rebuilt.
[[nodiscard]] inline std::vector<u64> PollFragProbe() {
    auto& state{Detail::GetFragProbeState()};
    if (!state.enabled || state.directory.empty()) {
        return {};
    }
    std::ifstream file{state.directory + "/probe.txt"};
    std::string text;
    if (!file || !std::getline(file, text)) {
        return {};
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    std::scoped_lock lock{state.mutex};
    if (text.empty() || text == state.text) {
        return {};
    }
    std::vector<u64> hashes;
    if (state.probe) {
        hashes.push_back(state.probe->hash);
    }
    state.Set(text);
    if (state.probe) {
        hashes.push_back(state.probe->hash);
    }
    return hashes;
}

} // namespace Shader
