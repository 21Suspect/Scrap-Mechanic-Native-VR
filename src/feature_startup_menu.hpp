#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr.h>

#include <cstdint>
#include <string>

namespace smvr::features
{
enum class UiHapticEvent { none, hover, click };

class StartupMenuUi
{
public:
    struct Matrix { float m[16]{}; };
    struct Constants
    {
        Matrix transform{};
        float pointer[4]{};
        float laser_start[4]{};
        float laser_end[4]{};
    };

    bool initialize(ID3D11Device *device, const wchar_t *asset_path);
    void update_visibility(bool game_ui_open_intent);
    void set_world_anchor(const XrPosef &player_anchor);
    void reset_world_anchor();
    void update_pointer(const XrPosef &right_hand, bool active);
    void update_interaction(bool select_down, float scroll_axis);
    bool capture_native_menu(ID3D11DeviceContext *context, IDXGISwapChain *swapchain);
    bool render(ID3D11DeviceContext *context, const XrView *views,
                ID3D11RenderTargetView *const *targets,
                const uint32_t *widths, const uint32_t *heights);
    void reset_state();
    void shutdown();

    bool visible() const { return visible_; }
    bool pointer_active() const { return dynamic_mode_ ? pointer_on_panel_ : hovered_button_ != 0; }
    bool dynamic_mode() const { return dynamic_mode_; }
    bool in_game_mode() const { return in_game_mode_; }
    bool native_capture_due() const;
    UiHapticEvent consume_haptic_event();

private:
    bool load_asset(const wchar_t *asset_path);
    bool poll_world_active();
    bool modal_cursor_visible() const;
    void request_native_capture(uint64_t delay_ms);
    void request_native_capture_after_pointer_settles(uint64_t delay_ms);
    void draw_menu(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                   uint32_t width, uint32_t height, const XrView &view);
    void draw_laser(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                    uint32_t width, uint32_t height, const XrView &view);

    ID3D11Device *device_ = nullptr;
    ID3D11Texture2D *menu_texture_ = nullptr;
    ID3D11ShaderResourceView *menu_view_ = nullptr;
    ID3D11Texture2D *native_menu_texture_ = nullptr;
    ID3D11ShaderResourceView *native_menu_view_ = nullptr;
    ID3D11VertexShader *panel_vertex_shader_ = nullptr;
    ID3D11VertexShader *laser_vertex_shader_ = nullptr;
    ID3D11PixelShader *panel_pixel_shader_ = nullptr;
    ID3D11PixelShader *native_panel_pixel_shader_ = nullptr;
    ID3D11PixelShader *laser_pixel_shader_ = nullptr;
    ID3D11SamplerState *sampler_ = nullptr;
    ID3D11Buffer *constants_ = nullptr;
    ID3D11RasterizerState *rasterizer_ = nullptr;
    ID3D11BlendState *alpha_blend_ = nullptr;
    uint32_t asset_width_ = 0;
    uint32_t asset_height_ = 0;
    uint32_t native_width_ = 0;
    uint32_t native_height_ = 0;
    XrPosef panel_pose_{{0,0,0,1},{0,0,0}};
    XrPosef dynamic_panel_pose_{{0,0,0,1},{0,0,0}};
    float pointer_u_ = 0.5f;
    float pointer_v_ = 0.5f;
    XrVector3f pointer_origin_{};
    XrVector3f pointer_hit_{};
    uint32_t hidden_frames_after_menu_ = 0;
    uint32_t hovered_button_ = 0;
    int pointer_client_x_ = 0;
    int pointer_client_y_ = 0;
    uint64_t native_capture_count_ = 0;
    uint64_t ui_click_count_ = 0;
    uint64_t scroll_last_ms_ = 0;
    uint64_t world_state_poll_ms_ = 0;
    uint64_t native_capture_due_ms_ = 0;
    uint64_t native_capture_followup_ms_ = 0;
    uint64_t native_capture_last_ms_ = 0;
    std::wstring world_state_path_;
    int native_capture_pointer_x_ = 0;
    int native_capture_pointer_y_ = 0;
    bool world_anchor_valid_ = false;
    bool world_active_ = false;
    bool world_state_known_ = false;
    bool pointer_on_panel_ = false;
    bool pointer_laser_active_ = false;
    bool visible_ = false;
    bool logged_ = false;
    bool dynamic_mode_ = false;
    bool select_down_ = false;
    bool engine_button_down_ = false;
    bool pointer_client_valid_ = false;
    bool pointer_client_initialized_ = false;
    bool native_capture_logged_ = false;
    bool input_route_logged_ = false;
    bool native_capture_requested_ = true;
    bool native_capture_pointer_valid_ = false;
    bool in_game_mode_ = false;
    UiHapticEvent pending_haptic_event_ = UiHapticEvent::none;
};
} // namespace smvr::features
