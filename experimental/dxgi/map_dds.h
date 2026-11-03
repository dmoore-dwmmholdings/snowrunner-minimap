#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
struct SrmmDdsView {
    std::uint32_t width{},height{},row_pitch{};
    const std::uint8_t* blocks{};
    std::size_t size{};
};
// Borrowed bytes. Only the importer-produced single-mip 2D BC3 variant is accepted.
std::optional<SrmmDdsView> srmm_parse_map_dds(const void* data,std::size_t size) noexcept;
