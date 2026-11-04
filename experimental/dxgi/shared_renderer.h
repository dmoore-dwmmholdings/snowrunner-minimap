#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>

// Borrowed swap-chain rendering. The caller must own the immediate-context
// thread for the complete call; this object cannot synchronize a foreign app.
struct SrmmMapFrame {
    float left=16, top=16, size=96;
    float center_u=.5f, center_v=.5f, uv_per_pixel=.004f, heading=0;
    float map_rotation=0;
    bool square=false;
    bool alignment_preview=false;
    bool texture_v_flipped=false; // Center remains north-up; only texture storage changes.
};
class SharedRenderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> deferred_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11ShaderResourceView> map_;
    HRESULT initialize_texture(ID3D11Device*,const void*,UINT,UINT,DXGI_FORMAT,UINT) noexcept;
public:
    HRESULT initialize_dds(ID3D11Device* device,const void* bytes,std::size_t size) noexcept;
    HRESULT initialize(ID3D11Device* device,const std::uint32_t* rgba,UINT width,UINT height) noexcept;
    HRESULT draw(IDXGISwapChain* swap,const SrmmMapFrame& frame) noexcept;
    void reset() noexcept;
};
