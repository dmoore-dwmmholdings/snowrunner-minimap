#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <optional>
#include <array>
#include <thread>
#include <cstdint>
#include <string>

struct SrmmDisplaySettings {
    float left=50,top=50,size=360,zoom=1;
    int tracking=2;
    bool square=false;
};
bool srmm_valid_display_settings(const SrmmDisplaySettings&) noexcept;
bool srmm_equal_display_settings(const SrmmDisplaySettings&,const SrmmDisplaySettings&) noexcept;

// Edge-triggered controls. No message hooks or input injection; the game also sees keys.
enum SrmmSettingsKey : unsigned {
    srmm_settings_toggle=1, srmm_settings_previous=2, srmm_settings_next=4,
    srmm_settings_decrease=8, srmm_settings_increase=16, srmm_settings_defaults=32
};
struct SrmmSettingsInput {
    unsigned down=0;
    bool foreground=false;
    // Optional menu-state hint for display text, never an editing prerequisite.
    bool menu_visible=false;
};
SrmmSettingsInput srmm_poll_settings_input(HWND window,bool menu_visible=false) noexcept;

// Pure model, also exercised without a game or GPU. Caller applies returned
// settings immediately and persists them on a background/control thread.
class SrmmSettingsModel {
    unsigned previous_=0;
    unsigned row_=0;
    bool open_=false,editing_allowed_=false,menu_visible_=false;
    SrmmDisplaySettings settings_;
public:
    std::optional<SrmmDisplaySettings> update(const SrmmSettingsInput&,const SrmmDisplaySettings&,unsigned width,unsigned height) noexcept;
    bool open() const noexcept {return open_;}
    bool editable() const noexcept {return open_&&editing_allowed_;}
    bool menu_visible() const noexcept {return menu_visible_;}
    unsigned row() const noexcept {return row_;}
    const SrmmDisplaySettings& values() const noexcept {return settings_;}
};

enum class SrmmSettingsSaveStatus { loaded,pending,saved,failed };
class SrmmNativeSettings {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    const std::thread::id owner_=std::this_thread::get_id();
    SrmmSettingsModel model_;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> deferred_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11ShaderResourceView> image_;
    std::array<std::wstring,15> rendered_lines_{};
    int rendered_row_=-1;
    bool alignment_preview_=true;
    SrmmSettingsSaveStatus save_status_=SrmmSettingsSaveStatus::loaded;
    std::optional<std::uint64_t> saved_age_seconds_;
    HRESULT initialize(ID3D11Device*) noexcept;
    HRESULT refresh_image() noexcept;
public:
    std::optional<SrmmDisplaySettings> update(const SrmmSettingsInput&,const SrmmDisplaySettings&,unsigned width,unsigned height) noexcept;
    HRESULT draw(IDXGISwapChain*) noexcept;
    void preference_status(SrmmSettingsSaveStatus status) noexcept {if(std::this_thread::get_id()==owner_)save_status_=status;}
    void navigation_status(bool alignment_preview,std::optional<std::uint64_t> saved_age_seconds) noexcept;
    void reset() noexcept;
    bool open() const noexcept {return model_.open();}
};

