#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mock_d3d11_renderer.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace minimap {
using Microsoft::WRL::ComPtr;
namespace {
void checked(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed, HRESULT " + std::to_string(result));
}
// Entirely original procedural map. Cyan landmark is at image (300,250).
constexpr char shader[] = R"(
cbuffer Frame : register(b0) {
    float4 mapView; // center x/y, cos/sin of heading-up rotation
    float4 screen;  // viewport size, zoom, player x/y
    float4 marker;  // cos/sin of marker heading
};
float4 vertex(uint id : SV_VertexID) : SV_POSITION {
    float2 p = id == 0 ? float2(-1,-1) : (id == 1 ? float2(-1,3) : float2(3,-1));
    return float4(p, 0, 1);
}
float4 pixel(float4 position : SV_POSITION) : SV_TARGET {
    float2 centered = position.xy - screen.x * 0.5;
    if (dot(centered, centered) > screen.x * screen.x * 0.25) discard;
    float2 d = centered / screen.y;
    float2 image = mapView.xy + float2(mapView.z*d.x-mapView.w*d.y, mapView.w*d.x+mapView.z*d.y);
    float3 color = float3(0.16, 0.26, 0.20);
    float2 grid = frac(image / 25.0);
    if (grid.x < 0.04 || grid.y < 0.04) color = float3(0.30, 0.42, 0.32);
    if (abs(image.y - 280) < 3) color = float3(0.80, 0.62, 0.32);
    if (distance(image, float2(300,250)) < 6) color = float3(0,1,1);
    float2 m = position.xy - screen.zw;
    m = float2(marker.x*m.x+marker.y*m.y, -marker.y*m.x+marker.x*m.y);
    if (m.y >= -12 && m.y <= 8 && abs(m.x) <= 0.4*(m.y+12)) color = float3(1,1,1);
    return float4(color, 1);
}
)";
ComPtr<ID3DBlob> compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> result, errors;
    HRESULT status = D3DCompile(shader, sizeof(shader)-1, "original-mock-minimap", nullptr, nullptr,
                               entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &result, &errors);
    if (FAILED(status) && errors)
        throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    checked(status, "Compile original shader");
    return result;
}
struct Constants { std::array<float, 4> map, screen, marker; };
static_assert(sizeof(Constants) == 48);
bool bounded(double value, double maximum) { return std::isfinite(value) && std::abs(value) <= maximum; }
}
struct MockD3d11Renderer::Impl {
    unsigned size;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    explicit Impl(unsigned dimension) : size(dimension) {
        if (size < 64 || size > 2048) throw std::invalid_argument("Mock viewport must be 64..2048 pixels.");
        const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL actual{};
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, requested, 1, D3D11_SDK_VERSION,
                                  &device, &actual, &context), "Create owned WARP device");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = texture.Height = size;
        texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&texture, nullptr, &target), "Create offscreen texture");
        checked(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "Create owned render target");
        texture.Usage = D3D11_USAGE_STAGING;
        texture.BindFlags = 0;
        texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&texture, nullptr, &staging), "Create readback texture");
        auto vertex = compile("vertex", "vs_5_0"), pixel = compile("pixel", "ps_5_0");
        checked(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &vs), "Create vertex shader");
        checked(device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &ps), "Create pixel shader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, &constants), "Create frame constants");
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_SOLID;
        raster_desc.CullMode = D3D11_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        checked(device->CreateRasterizerState(&raster_desc, &raster), "Create raster state");
    }
    RgbaImage readback() {
        RgbaImage result{size, std::vector<std::uint8_t>(static_cast<size_t>(size)*size*4)};
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read rendered pixels");
        for (unsigned y = 0; y < size; ++y)
            std::memcpy(result.pixels.data() + static_cast<size_t>(y)*size*4,
                        static_cast<const std::uint8_t*>(mapped.pData) + static_cast<size_t>(y)*mapped.RowPitch, size*4);
        context->Unmap(staging.Get(), 0);
        return result;
    }
};
MockD3d11Renderer::MockD3d11Renderer(unsigned size) : impl_(std::make_unique<Impl>(size)) {}
MockD3d11Renderer::~MockD3d11Renderer() = default;
RgbaImage MockD3d11Renderer::render(const RenderPacket& packet) {
    auto& p = *impl_;
    const float clear[4]{};
    p.context->ClearRenderTargetView(p.rtv.Get(), clear); // Every frame starts empty, including invalid transitions.
    if (packet.state != FrameState::synthetic_ready || !packet.geometry) return p.readback();
    const auto& g = *packet.geometry;
    const auto& v = g.view;
    if (!g.context.valid() || v.size != p.size || !bounded(v.center.x, 1e6) || !bounded(v.center.y, 1e6) ||
        !bounded(v.heading, 1e6) || !bounded(g.marker_heading, 1e6) ||
        !std::isfinite(v.pixels_per_image_pixel) || v.pixels_per_image_pixel < 0.001 || v.pixels_per_image_pixel > 1000 ||
        !bounded(g.player.x, p.size) || !bounded(g.player.y, p.size) || g.player.x < 0 || g.player.y < 0)
        return p.readback();
    const double angle = v.heading_up ? v.heading : 0;
    const Constants values{{static_cast<float>(v.center.x), static_cast<float>(v.center.y),
                            static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))},
                           {static_cast<float>(p.size), static_cast<float>(v.pixels_per_image_pixel),
                            static_cast<float>(g.player.x), static_cast<float>(g.player.y)},
                           {static_cast<float>(std::cos(g.marker_heading)), static_cast<float>(std::sin(g.marker_heading)), 0, 0}};
    p.context->UpdateSubresource(p.constants.Get(), 0, nullptr, &values, 0, 0);
    auto* target = p.rtv.Get();
    p.context->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(p.size), static_cast<float>(p.size), 0, 1};
    p.context->RSSetViewports(1, &viewport);
    p.context->RSSetState(p.raster.Get());
    p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vs.Get(), nullptr, 0);
    p.context->PSSetShader(p.ps.Get(), nullptr, 0);
    auto* constants = p.constants.Get();
    p.context->PSSetConstantBuffers(0, 1, &constants);
    p.context->Draw(3, 0);
    return p.readback();
}
}
