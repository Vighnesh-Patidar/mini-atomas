#pragma once

// Shared test helpers. Include from test_*.cpp files that need them; this
// header is not part of the public mith API.

#include "mith/core/trace_sink.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mith_test {

// TraceSink that turns each emit into a JSON line (via JsonTraceSink::format)
// and stores it for inspection. Tests grep the resulting lines for expected
// substrings — no FILE I/O involved.
struct JsonCapturingSink : mith::TraceSink {
    std::vector<std::string> lines;

    using TraceSink::emit;
    void emit(mith::TraceLevel level, std::string_view event,
              const mith::TraceField* fields, std::size_t count) noexcept override {
        lines.push_back(mith::JsonTraceSink::format(level, event, fields, count));
    }
};

inline bool contains(std::string_view haystack, std::string_view needle) noexcept {
    return haystack.find(needle) != std::string_view::npos;
}

} // namespace mith_test
