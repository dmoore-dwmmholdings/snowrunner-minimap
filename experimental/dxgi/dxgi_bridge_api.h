#pragma once
#include <cstdint>
struct SrmmBridgeStats {
    std::uint32_t size,version;
    std::uint64_t factories,swaps,presents,resizes,live_factories,live_swaps,unknown_queries;
    std::uint64_t rendered,render_failures;
};
