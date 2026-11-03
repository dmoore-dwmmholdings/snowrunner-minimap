#pragma once
#include "telemetry.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace minimap {
struct RgbaImage {
    unsigned size;
    std::vector<std::uint8_t> pixels;
};
// Owns a headless WARP device/context. Never accepts a game's device or window.
// Procedural mock terrain only; this is not an injected HUD renderer.
// Single-threaded: the owner serializes render, resize, suspend and destruction.
class MockD3d11Renderer {
public:
    explicit MockD3d11Renderer(unsigned size = 256);
    ~MockD3d11Renderer();
    MockD3d11Renderer(const MockD3d11Renderer&) = delete;
    MockD3d11Renderer& operator=(const MockD3d11Renderer&) = delete;
    RgbaImage render(const RenderPacket& packet);
    // Zero suspends and releases the owned device. Nonzero accepts 64..2048.
    // Resize allocation failure preserves an active renderer; resuming failure
    // leaves it suspended. Geometry must match the new viewport before drawing.
    void resize(unsigned size);
    void suspend() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
