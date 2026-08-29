#include "feature_startup_menu.hpp"
#include "feature_engine_input.hpp"

#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace smvr
{
void log_line(const char *format, ...);
}

namespace smvr::features
{
namespace
{
constexpr float kPanelHeight = 0.86f;
constexpr float kPanelAspect = 1024.0f / 1400.0f;
constexpr float kPanelWidth = kPanelHeight * kPanelAspect;
constexpr float kDynamicPanelHeight = 0.72f;
constexpr uint64_t kWorldStatePollMilliseconds = 100;

struct ButtonRegion
{
    float left, top, right, bottom;
    float desktop_x, desktop_y;
};

// Texture-space rectangles match tools/Generate-StartupMenuAsset.ps1. The
// desktop coordinates target the real 1920x1080 controls; the world panel does
// not replace or duplicate any game menu logic.
constexpr ButtonRegion kButtons[] = {
    {120.0f/1024.0f, 610.0f/1400.0f, 593.0f/1024.0f, 767.0f/1400.0f, 0.117f, 0.500f},
    {120.0f/1024.0f, 815.0f/1400.0f, 600.0f/1024.0f, 912.0f/1400.0f, 0.117f, 0.599f},
    {120.0f/1024.0f, 922.0f/1400.0f, 600.0f/1024.0f,1019.0f/1400.0f, 0.117f, 0.657f},
    {120.0f/1024.0f,1029.0f/1400.0f, 600.0f/1024.0f,1126.0f/1400.0f, 0.117f, 0.714f}
};

template <typename T> void release(T *&value)
{
    if (value) value->Release();
    value = nullptr;
}

XrVector3f rotate_vector(const XrQuaternionf &q, const XrVector3f &v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const float uv = u.x*v.x + u.y*v.y + u.z*v.z;
    const float uu = u.x*u.x + u.y*u.y + u.z*u.z;
    const XrVector3f cross{u.y*v.z-u.z*v.y, u.z*v.x-u.x*v.z, u.x*v.y-u.y*v.x};
    return {
        2*uv*u.x + (q.w*q.w-uu)*v.x + 2*q.w*cross.x,
        2*uv*u.y + (q.w*q.w-uu)*v.y + 2*q.w*cross.y,
        2*uv*u.z + (q.w*q.w-uu)*v.z + 2*q.w*cross.z
    };
}

float dot(const XrVector3f &a, const XrVector3f &b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

HWND game_window()
{
    struct Search { DWORD pid; HWND result; } search{GetCurrentProcessId(), nullptr};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto &search = *reinterpret_cast<Search *>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == search.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
        {
            search.result = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

DXGI_FORMAT capture_texture_format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    default:
        return format;
    }
}

DXGI_FORMAT capture_view_format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default:
        return format;
    }
}
} // namespace

static StartupMenuUi::Matrix identity_matrix()
{
    StartupMenuUi::Matrix result{};
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
    return result;
}

static StartupMenuUi::Matrix multiply_matrix(const StartupMenuUi::Matrix &a,
                                              const StartupMenuUi::Matrix &b)
{
    StartupMenuUi::Matrix result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.m[column*4+row] += a.m[k*4+row] * b.m[column*4+k];
    return result;
}

static StartupMenuUi::Matrix pose_matrix(const XrPosef &pose)
{
    const float x=pose.orientation.x, y=pose.orientation.y, z=pose.orientation.z, w=pose.orientation.w;
    auto result = identity_matrix();
    result.m[0]=1-2*(y*y+z*z); result.m[4]=2*(x*y-z*w); result.m[8]=2*(x*z+y*w);
    result.m[1]=2*(x*y+z*w); result.m[5]=1-2*(x*x+z*z); result.m[9]=2*(y*z-x*w);
    result.m[2]=2*(x*z-y*w); result.m[6]=2*(y*z+x*w); result.m[10]=1-2*(x*x+y*y);
    result.m[12]=pose.position.x; result.m[13]=pose.position.y; result.m[14]=pose.position.z;
    return result;
}

static StartupMenuUi::Matrix inverse_pose_matrix(const XrPosef &pose)
{
    XrPosef inverse{};
    inverse.orientation = {-pose.orientation.x,-pose.orientation.y,-pose.orientation.z,pose.orientation.w};
    inverse.position = rotate_vector(inverse.orientation,
        {-pose.position.x,-pose.position.y,-pose.position.z});
    return pose_matrix(inverse);
}

static StartupMenuUi::Matrix projection_matrix(const XrFovf &fov)
{
    constexpr float near_z=0.025f, far_z=100.0f;
    const float left=std::tan(fov.angleLeft), right=std::tan(fov.angleRight);
    const float down=std::tan(fov.angleDown), up=std::tan(fov.angleUp);
    StartupMenuUi::Matrix result{};
    result.m[0]=2/(right-left); result.m[5]=2/(up-down);
    result.m[8]=(right+left)/(right-left); result.m[9]=(up+down)/(up-down);
    result.m[10]=-far_z/(far_z-near_z); result.m[11]=-1;
    result.m[14]=-(far_z*near_z)/(far_z-near_z);
    return result;
}

bool StartupMenuUi::load_asset(const wchar_t *asset_path)
{
    if (!asset_path || !*asset_path) return false;
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(com_result);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) return false;

    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(asset_path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    UINT width = 0, height = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (SUCCEEDED(hr) && (width == 0 || height == 0 || width > 4096 || height > 4096))
        hr = E_INVALIDARG;

    std::vector<uint8_t> pixels;
    if (SUCCEEDED(hr))
    {
        pixels.resize(static_cast<size_t>(width) * height * 4u);
        hr = converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
        // A typeless backing resource is required when the immutable upload is
        // interpreted through an sRGB shader-resource view.
        desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels.data(); data.SysMemPitch=width*4u;
        hr=device_->CreateTexture2D(&desc,&data,&menu_texture_);
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels=1;
        if (SUCCEEDED(hr)) hr=device_->CreateShaderResourceView(menu_texture_,&srv,&menu_view_);
    }
    release(converter); release(frame); release(decoder); release(factory);
    if (uninitialize) CoUninitialize();
    if (FAILED(hr))
    {
        release(menu_view_); release(menu_texture_);
        log_line("VR_STARTUP_MENU_ASSET_FAIL hr=%08x", static_cast<unsigned>(hr));
        return false;
    }
    asset_width_=width; asset_height_=height;
    log_line("VR_STARTUP_MENU_ASSET_READY size=%ux%u source=custom_vr_logo_official_game_buttons", width, height);
    return true;
}

bool StartupMenuUi::initialize(ID3D11Device *device, const wchar_t *asset_path)
{
    if (device_ && panel_vertex_shader_ && laser_vertex_shader_ &&
        panel_pixel_shader_ && laser_pixel_shader_ && sampler_ && constants_ &&
        rasterizer_ && alpha_blend_) return true;
    if (!device) return false;
    device_ = device;
    device_->AddRef();
    // The VR panel is an exact live copy of Scrap Mechanic's current native
    // menu. Do not decode or upload a separate fallback image during OpenXR
    // startup: besides being redundant, that work runs while the game is still
    // loading assets. The first pre-mirror capture supplies the texture before
    // the panel is rendered.
    world_state_path_.clear();
    if (asset_path && *asset_path)
    {
        std::wstring directory(asset_path);
        const size_t slash=directory.find_last_of(L"\\/");
        if (slash!=std::wstring::npos)
        {
            directory.resize(slash);
            const std::wstring relative=directory+L"\\..\\Data\\NativeVR\\world_state.json";
            wchar_t absolute[MAX_PATH]{};
            if (GetFullPathNameW(relative.c_str(),MAX_PATH,absolute,nullptr)>0)
                world_state_path_=absolute;
        }
    }
    log_line("VR_STARTUP_MENU_INIT_BEGIN source=pre_mirror_game_backbuffer static_asset_decode=0");

    const char *shader = R"(
        cbuffer MenuConstants : register(b0) {
            float4x4 transform; float4 pointerData; float4 laserStart; float4 laserEnd;
        };
        struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
        VSOut vs_quad(uint id : SV_VertexID) {
            float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
            VSOut output; output.uv=corners[id];
            output.position=mul(transform,float4(output.uv.x-0.5,0.5-output.uv.y,0,1)); return output;
        }
        VSOut vs_laser(uint id : SV_VertexID) {
            VSOut output; output.uv=0;
            output.position=mul(transform,float4(id==0?laserStart.xyz:laserEnd.xyz,1)); return output;
        }
        Texture2D menuTexture : register(t0); SamplerState menuSampler : register(s0);
        bool button_region(float2 uv, uint button) {
            if (uv.x<0.1171875) return false;
            if (button==1) return uv.x<=0.5791016 && uv.y>=0.4357143 && uv.y<=0.5478572;
            if (uv.x>0.5859375) return false;
            if (button==2) return uv.y>=0.5821429 && uv.y<=0.6514286;
            if (button==3) return uv.y>=0.6585714 && uv.y<=0.7278572;
            if (button==4) return uv.y>=0.7350000 && uv.y<=0.8042857;
            return false;
        }
        float4 ps_menu(VSOut input) : SV_TARGET {
            float4 color=menuTexture.SampleLevel(menuSampler,input.uv,0);
            if (color.a<0.015) discard;
            uint hovered=(uint)(pointerData.z+0.5);
            if (hovered>0 && button_region(input.uv,hovered)) {
                float2 delta=abs(input.uv-pointerData.xy);
                color.rgb=min(float3(1,1,1),color.rgb*1.11+float3(0.055,0.04,0.0));
                if (delta.x<0.010 && delta.y<0.010) color.rgb=float3(1,1,1);
            }
            return color;
        }
        float4 ps_native_menu(VSOut input) : SV_TARGET {
            float4 color=menuTexture.SampleLevel(menuSampler,input.uv,0);
            // The desktop blur/fade pass has no desktop scene beneath it in the
            // VR UI-only capture. It therefore writes a growing neutral-black
            // rectangle. Convert that complete dark range back to coverage,
            // independently of the source alpha and shared pointer constants.
            // Linear 0.015 is approximately sRGB 33: the actual grey Axolot UI
            // panels remain above it, while black and the fade fringe disappear.
            float peak=max(color.r,max(color.g,color.b));
            float fadeCoverage=smoothstep(0.0030,0.0150,peak);
            color.a=min(saturate(color.a),fadeCoverage);
            if (color.a<0.004) discard;
            return color;
        }
        float4 ps_laser(VSOut input) : SV_TARGET { return float4(1,1,1,1); }
    )";
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    using Compile = HRESULT (WINAPI *)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,
        LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler,"D3DCompile")) : nullptr;
    ID3DBlob *vs=nullptr,*lvs=nullptr,*ps=nullptr,*nps=nullptr,*lps=nullptr,*errors=nullptr;
    auto make = [&](const char *entry,const char *profile,ID3DBlob **blob) {
        if (errors) { errors->Release(); errors=nullptr; }
        return compile && SUCCEEDED(compile(shader,std::strlen(shader),"smvr_startup_menu",nullptr,
            nullptr,entry,profile,0,0,blob,&errors));
    };
    bool ok = make("vs_quad","vs_5_0",&vs) && make("vs_laser","vs_5_0",&lvs) &&
        make("ps_menu","ps_5_0",&ps) && make("ps_native_menu","ps_5_0",&nps) &&
        make("ps_laser","ps_5_0",&lps);
    if (ok) ok = SUCCEEDED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&panel_vertex_shader_)) &&
        SUCCEEDED(device_->CreateVertexShader(lvs->GetBufferPointer(),lvs->GetBufferSize(),nullptr,&laser_vertex_shader_)) &&
        SUCCEEDED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&panel_pixel_shader_)) &&
        SUCCEEDED(device_->CreatePixelShader(nps->GetBufferPointer(),nps->GetBufferSize(),nullptr,&native_panel_pixel_shader_)) &&
        SUCCEEDED(device_->CreatePixelShader(lps->GetBufferPointer(),lps->GetBufferSize(),nullptr,&laser_pixel_shader_));
    release(vs); release(lvs); release(ps); release(nps); release(lps); release(errors);
    if (compiler) FreeLibrary(compiler);

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth=sizeof(Constants); constants.Usage=D3D11_USAGE_DEFAULT;
    constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable=TRUE;
    blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device_->CreateSamplerState(&sampler,&sampler_)) &&
        SUCCEEDED(device_->CreateBuffer(&constants,nullptr,&constants_)) &&
        SUCCEEDED(device_->CreateRasterizerState(&raster,&rasterizer_)) &&
        SUCCEEDED(device_->CreateBlendState(&blend,&alpha_blend_));
    if (!ok)
    {
        log_line("VR_STARTUP_MENU_INIT_FAIL");
        shutdown();
        return false;
    }
    log_line("VR_STARTUP_MENU_READY world_locked=1 source=pre_mirror_game_backbuffer laser=white live_capture_required=1 static_asset_decode=0");
    return true;
}

void StartupMenuUi::update_visibility()
{
    const bool was_visible=visible_;
    const bool world_active=poll_world_active();
    if (world_active)
    {
        visible_=false; pointer_on_panel_=false; pointer_laser_active_=false; hovered_button_=0;
        pointer_client_valid_=false;
        if (engine_button_down_ && EngineInputQueue::instance().queue_mouse_button(0,false))
            engine_button_down_=false;
        return;
    }
    // Scrap Mechanic draws its own software cursor and normally keeps the
    // Win32 hardware cursor hidden on every startup screen. The exact native
    // menu therefore stays live until a local player script explicitly marks
    // a world active. This covers every submenu and confirmation dialog.
    visible_=true;
    if (!was_visible) request_native_capture(0);
}

void StartupMenuUi::request_native_capture(uint64_t delay_ms)
{
    native_capture_requested_=true;
    native_capture_due_ms_=GetTickCount64()+delay_ms;
}

bool StartupMenuUi::native_capture_due() const
{
    if (!visible_) return false;
    if (!native_menu_view_) return true;
    const uint64_t now=GetTickCount64();
    return (native_capture_requested_ && now>=native_capture_due_ms_) ||
        (native_capture_followup_ms_!=0 && now>=native_capture_followup_ms_);
}

void StartupMenuUi::set_world_anchor(const XrPosef &player_anchor)
{
    if (world_anchor_valid_) return;
    panel_pose_.orientation = player_anchor.orientation;
    const XrVector3f offset = rotate_vector(player_anchor.orientation, {-0.48f,-0.08f,-0.95f});
    panel_pose_.position = {player_anchor.position.x+offset.x,player_anchor.position.y+offset.y,
        player_anchor.position.z+offset.z};
    dynamic_panel_pose_.orientation = player_anchor.orientation;
    const XrVector3f dynamic_offset = rotate_vector(
        player_anchor.orientation, {-0.12f,-0.02f,-1.12f});
    dynamic_panel_pose_.position = {
        player_anchor.position.x+dynamic_offset.x,
        player_anchor.position.y+dynamic_offset.y,
        player_anchor.position.z+dynamic_offset.z};
    world_anchor_valid_ = true;
    log_line("VR_STARTUP_MENU_WORLD_ANCHOR position=%.4f,%.4f,%.4f follows_live_head=0 width=%.3f height=%.3f",
        panel_pose_.position.x,panel_pose_.position.y,panel_pose_.position.z,kPanelWidth,kPanelHeight);
}

void StartupMenuUi::reset_world_anchor()
{
    world_anchor_valid_ = false;
    pointer_on_panel_ = false;
    pointer_laser_active_ = false;
    hovered_button_ = 0;
    pointer_client_valid_ = false;
}

bool StartupMenuUi::poll_world_active()
{
    const uint64_t now=GetTickCount64();
    if (world_state_poll_ms_ && now-world_state_poll_ms_<kWorldStatePollMilliseconds)
        return world_active_;
    world_state_poll_ms_=now;
    if (world_state_path_.empty()) return false;

    HANDLE file=CreateFileW(world_state_path_.c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE)
    {
        if (!world_state_known_)
        {
            world_state_known_=true;
            world_active_=false;
            log_line("VR_STARTUP_MENU_STATE source=world_bridge active=0 reason=marker_absent");
        }
        return world_active_;
    }
    char bytes[512]{};
    DWORD read=0;
    const bool read_ok=ReadFile(file,bytes,sizeof(bytes)-1,&read,nullptr)!=FALSE;
    CloseHandle(file);
    if (!read_ok || read==0) return world_active_;
    bytes[read]=0;
    const char *active=std::strstr(bytes,"\"active\"");
    if (!active) return world_active_;
    active=std::strchr(active,':');
    if (!active) return world_active_;
    ++active;
    while (*active==' ' || *active=='\t' || *active=='\r' || *active=='\n') ++active;
    const bool parsed_true=std::strncmp(active,"true",4)==0;
    const bool parsed_false=std::strncmp(active,"false",5)==0;
    if (!parsed_true && !parsed_false) return world_active_;
    const bool next=parsed_true;
    if (!world_state_known_ || next!=world_active_)
        log_line("VR_STARTUP_MENU_STATE source=world_bridge active=%u",next?1u:0u);
    world_state_known_=true;
    world_active_=next;
    return world_active_;
}

void StartupMenuUi::update_pointer(const XrPosef &hand, bool active)
{
    pointer_on_panel_=false;
    pointer_laser_active_=false;
    hovered_button_=0;
    pointer_client_valid_=false;
    if (!visible_ || !world_anchor_valid_ || !active) return;
    const XrPosef &active_pose = dynamic_mode_ ? dynamic_panel_pose_ : panel_pose_;
    const float active_height = dynamic_mode_ ? kDynamicPanelHeight : kPanelHeight;
    const float active_width = dynamic_mode_
        ? active_height * (native_height_ ? static_cast<float>(native_width_) /
            static_cast<float>(native_height_) : 16.0f/9.0f)
        : kPanelWidth;
    const XrVector3f ray=rotate_vector(hand.orientation,{0,0,-1});
    pointer_origin_=hand.position;
    pointer_hit_={hand.position.x+ray.x*2.0f,hand.position.y+ray.y*2.0f,
        hand.position.z+ray.z*2.0f};
    pointer_laser_active_=true;
    const XrVector3f normal=rotate_vector(active_pose.orientation,{0,0,1});
    const float denominator=dot(ray,normal);
    if (std::fabs(denominator)<0.0001f) return;
    const XrVector3f to_panel{active_pose.position.x-hand.position.x,
        active_pose.position.y-hand.position.y,active_pose.position.z-hand.position.z};
    const float distance=dot(to_panel,normal)/denominator;
    if (distance<=0.02f || distance>4.0f) return;
    const XrVector3f hit{hand.position.x+ray.x*distance,hand.position.y+ray.y*distance,
        hand.position.z+ray.z*distance};
    const XrVector3f offset{hit.x-active_pose.position.x,hit.y-active_pose.position.y,
        hit.z-active_pose.position.z};
    const XrQuaternionf inverse{-active_pose.orientation.x,-active_pose.orientation.y,
        -active_pose.orientation.z,active_pose.orientation.w};
    const XrVector3f local=rotate_vector(inverse,offset);
    const float u=local.x/active_width+0.5f, v=0.5f-local.y/active_height;
    if (u<0 || u>1 || v<0 || v>1) return;
    pointer_u_=u; pointer_v_=v; pointer_hit_=hit;
    pointer_on_panel_=true;

    HWND window=game_window(); RECT client{};
    if (!window || !GetClientRect(window,&client)) return;
    const LONG client_width=std::max(1L,client.right-client.left);
    const LONG client_height=std::max(1L,client.bottom-client.top);

    if (dynamic_mode_)
    {
        const int client_x=std::clamp(static_cast<int>(u*static_cast<float>(client_width-1)),
            0,static_cast<int>(client_width-1));
        const int client_y=std::clamp(static_cast<int>(v*static_cast<float>(client_height-1)),
            0,static_cast<int>(client_height-1));
        const bool first=!pointer_client_initialized_;
        const int delta_x=first?1:client_x-pointer_client_x_;
        const int delta_y=first?0:client_y-pointer_client_y_;
        pointer_client_x_=client_x; pointer_client_y_=client_y;
        pointer_client_initialized_=true;
        pointer_client_valid_=EngineInputQueue::instance().queue_mouse_move(
            delta_x,delta_y,client_x,client_y);
        if (pointer_client_valid_ && (!native_capture_pointer_valid_ ||
            std::abs(client_x-native_capture_pointer_x_)>=6 ||
            std::abs(client_y-native_capture_pointer_y_)>=6))
        {
            native_capture_pointer_x_=client_x;
            native_capture_pointer_y_=client_y;
            native_capture_pointer_valid_=true;
            request_native_capture(90);
        }
        return;
    }

    for (uint32_t index=0; index<std::size(kButtons); ++index)
    {
        const ButtonRegion &button=kButtons[index];
        if (u<button.left || u>button.right || v<button.top || v>button.bottom) continue;
        const int client_x=static_cast<int>(button.desktop_x*static_cast<float>(client_width-1));
        const int client_y=static_cast<int>(button.desktop_y*static_cast<float>(client_height-1));
        const bool first=!pointer_client_initialized_;
        const int delta_x=first?1:client_x-pointer_client_x_;
        const int delta_y=first?0:client_y-pointer_client_y_;
        pointer_client_x_=client_x; pointer_client_y_=client_y;
        pointer_client_initialized_=true;
        pointer_client_valid_=EngineInputQueue::instance().queue_mouse_move(
            delta_x,delta_y,client_x,client_y);
        if (!pointer_client_valid_) return;
        hovered_button_=index+1;
        return;
    }
}

void StartupMenuUi::update_interaction(bool select_down, float scroll_axis)
{
    const bool can_select=pointer_client_valid_ && pointer_active();
    if (select_down!=select_down_)
    {
        if (select_down && can_select)
        {
            if (EngineInputQueue::instance().queue_mouse_button(0,true))
            {
                engine_button_down_=true;
                ++ui_click_count_;
                request_native_capture(0);
                native_capture_followup_ms_=GetTickCount64()+350;
                if (!input_route_logged_)
                {
                    input_route_logged_=true;
                    log_line("VR_UI_INPUT_ACTIVE route=scrap_mechanic_private_queue coordinates=client trigger=right_hand win32_mouse_simulation=0");
                }
                if (!dynamic_mode_)
                {
                    dynamic_mode_=true;
                    log_line("VR_NATIVE_MENU_MODE_REQUESTED click=%llu native_capture_ready=%u",
                        static_cast<unsigned long long>(ui_click_count_),native_menu_view_?1u:0u);
                }
            }
        }
        else if (!select_down && engine_button_down_)
        {
            if (EngineInputQueue::instance().queue_mouse_button(0,false))
            {
                engine_button_down_=false;
                request_native_capture(70);
            }
        }
        select_down_=select_down;
    }

    const uint64_t now=GetTickCount64();
    if (dynamic_mode_ && can_select && std::fabs(scroll_axis)>0.55f &&
        now-scroll_last_ms_>=110)
    {
        const int wheel=scroll_axis>0.0f?WHEEL_DELTA:-WHEEL_DELTA;
        if (EngineInputQueue::instance().queue_mouse_wheel(wheel))
        {
            scroll_last_ms_=now;
            request_native_capture(80);
            native_capture_followup_ms_=now+260;
        }
    }
}

bool StartupMenuUi::capture_native_menu(ID3D11DeviceContext *context, IDXGISwapChain *swapchain)
{
    if (!visible_ || !device_ || !context || !swapchain) return false;
    ID3D11Texture2D *source=nullptr;
    // Scrap Mechanic uses DXGI_SWAP_EFFECT_DISCARD, so buffer zero is the
    // application renderable backbuffer. This is called before the VR mirror
    // overwrites it, never from the Present callback after the overwrite.
    HRESULT hr=swapchain->GetBuffer(0,IID_PPV_ARGS(&source));
    if (FAILED(hr) || !source) return false;
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if (source_desc.Width==0 || source_desc.Height==0 || source_desc.ArraySize!=1 ||
        source_desc.SampleDesc.Count!=1)
    {
        source->Release();
        return false;
    }

    const DXGI_FORMAT texture_format=capture_texture_format(source_desc.Format);
    bool recreate=!native_menu_texture_ || !native_menu_view_;
    if (!recreate)
    {
        D3D11_TEXTURE2D_DESC existing{};
        native_menu_texture_->GetDesc(&existing);
        recreate=existing.Width!=source_desc.Width || existing.Height!=source_desc.Height ||
            existing.Format!=texture_format;
    }
    if (recreate)
    {
        release(native_menu_view_); release(native_menu_texture_);
        D3D11_TEXTURE2D_DESC destination=source_desc;
        destination.Format=texture_format;
        destination.MipLevels=1; destination.ArraySize=1;
        destination.Usage=D3D11_USAGE_DEFAULT;
        destination.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        destination.CPUAccessFlags=0; destination.MiscFlags=0;
        hr=device_->CreateTexture2D(&destination,nullptr,&native_menu_texture_);
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format=capture_view_format(source_desc.Format);
        view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MostDetailedMip=0; view.Texture2D.MipLevels=1;
        if (SUCCEEDED(hr))
            hr=device_->CreateShaderResourceView(native_menu_texture_,&view,&native_menu_view_);
        if (FAILED(hr) || !native_menu_texture_ || !native_menu_view_)
        {
            release(native_menu_view_); release(native_menu_texture_);
            source->Release();
            log_line("VR_NATIVE_MENU_CAPTURE_FAIL stage=create hr=%08x source_format=%u",
                static_cast<unsigned>(hr),static_cast<unsigned>(source_desc.Format));
            return false;
        }
        native_width_=source_desc.Width; native_height_=source_desc.Height;
    }
    context->CopyResource(native_menu_texture_,source);
    source->Release();
    const uint64_t capture_time=GetTickCount64();
    native_capture_requested_=false;
    native_capture_due_ms_=0;
    if (native_capture_followup_ms_!=0 && capture_time>=native_capture_followup_ms_)
        native_capture_followup_ms_=0;
    native_capture_pointer_x_=pointer_client_x_;
    native_capture_pointer_y_=pointer_client_y_;
    native_capture_pointer_valid_=pointer_client_initialized_;
    ++native_capture_count_;
    if (!native_capture_logged_)
    {
        native_capture_logged_=true;
        log_line("VR_NATIVE_MENU_CAPTURE_READY size=%ux%u source_format=%u view_format=%u source_alpha=1 opaque_black_key=1 pre_mirror=1",
            native_width_,native_height_,static_cast<unsigned>(source_desc.Format),
            static_cast<unsigned>(capture_view_format(source_desc.Format)));
    }
    if (!dynamic_mode_)
    {
        dynamic_mode_=true;
        pointer_client_initialized_=false;
        log_line("VR_NATIVE_MENU_LIVE_ACTIVE source=pre_mirror_game_backbuffer capture=%llu exact_pc_menu=1",
            static_cast<unsigned long long>(native_capture_count_));
    }
    if ((native_capture_count_%300)==0)
        log_line("VR_NATIVE_MENU_CAPTURE_PROGRESS count=%llu exact_pc_menu=1",
            static_cast<unsigned long long>(native_capture_count_));
    return true;
}

void StartupMenuUi::draw_menu(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                              uint32_t width, uint32_t height, const XrView &view)
{
    const float factor[4]{};
    context->OMSetRenderTargets(1,&target,nullptr);
    context->OMSetBlendState(alpha_blend_,factor,0xffffffffu);
    context->OMSetDepthStencilState(nullptr,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    context->RSSetViewports(1,&viewport); context->RSSetState(rasterizer_);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer *none=nullptr; UINT zero=0;
    context->IASetVertexBuffers(0,1,&none,&zero,&zero);

    ID3D11ShaderResourceView *active_view=dynamic_mode_?native_menu_view_:menu_view_;
    if (!active_view) return;
    const XrPosef &active_pose=dynamic_mode_?dynamic_panel_pose_:panel_pose_;
    const float active_height=dynamic_mode_?kDynamicPanelHeight:kPanelHeight;
    const float active_width=dynamic_mode_
        ? active_height*(native_height_?static_cast<float>(native_width_)/static_cast<float>(native_height_):16.0f/9.0f)
        : kPanelWidth;
    const Matrix view_projection=multiply_matrix(projection_matrix(view.fov),inverse_pose_matrix(view.pose));
    Matrix local=identity_matrix(); local.m[0]=active_width; local.m[5]=active_height;
    Constants values{};
    values.transform=multiply_matrix(view_projection,multiply_matrix(pose_matrix(active_pose),local));
    values.pointer[0]=pointer_u_; values.pointer[1]=pointer_v_;
    values.pointer[2]=static_cast<float>(hovered_button_);
    values.pointer[3]=dynamic_mode_?-1.0f:(pointer_on_panel_?1.0f:0.0f);
    context->UpdateSubresource(constants_,0,nullptr,&values,0,0);
    context->VSSetShader(panel_vertex_shader_,nullptr,0); context->VSSetConstantBuffers(0,1,&constants_);
    context->PSSetShader(dynamic_mode_?native_panel_pixel_shader_:panel_pixel_shader_,nullptr,0);
    context->PSSetShaderResources(0,1,&active_view);
    context->PSSetSamplers(0,1,&sampler_); context->Draw(6,0);
    ID3D11ShaderResourceView *none_view=nullptr;
    context->PSSetShaderResources(0,1,&none_view);
    context->OMSetRenderTargets(0,nullptr,nullptr);
}

void StartupMenuUi::draw_laser(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                               uint32_t width, uint32_t height, const XrView &view)
{
    if (!pointer_laser_active_) return;
    Constants values{};
    values.transform=multiply_matrix(projection_matrix(view.fov),inverse_pose_matrix(view.pose));
    values.laser_start[0]=pointer_origin_.x; values.laser_start[1]=pointer_origin_.y;
    values.laser_start[2]=pointer_origin_.z; values.laser_start[3]=1;
    values.laser_end[0]=pointer_hit_.x; values.laser_end[1]=pointer_hit_.y;
    values.laser_end[2]=pointer_hit_.z; values.laser_end[3]=1;
    const float factor[4]{};
    context->OMSetRenderTargets(1,&target,nullptr); context->OMSetBlendState(alpha_blend_,factor,0xffffffffu);
    context->OMSetDepthStencilState(nullptr,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    context->RSSetViewports(1,&viewport); context->RSSetState(rasterizer_);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    ID3D11Buffer *none=nullptr; UINT zero=0; context->IASetVertexBuffers(0,1,&none,&zero,&zero);
    context->UpdateSubresource(constants_,0,nullptr,&values,0,0);
    context->VSSetShader(laser_vertex_shader_,nullptr,0); context->VSSetConstantBuffers(0,1,&constants_);
    context->PSSetShader(laser_pixel_shader_,nullptr,0); context->Draw(2,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetRenderTargets(0,nullptr,nullptr);
}

bool StartupMenuUi::render(ID3D11DeviceContext *context, const XrView *views,
                           ID3D11RenderTargetView *const *targets,
                           const uint32_t *widths, const uint32_t *heights)
{
    if (!visible_ || !world_anchor_valid_ || !context || !views || !targets ||
        !(dynamic_mode_?native_menu_view_:menu_view_))
        return false;
    for (uint32_t eye=0; eye<2; ++eye)
    {
        if (!targets[eye]) continue;
        draw_menu(context,targets[eye],widths[eye],heights[eye],views[eye]);
        draw_laser(context,targets[eye],widths[eye],heights[eye],views[eye]);
    }
    if (!logged_)
    {
        logged_=true;
        log_line("VR_STARTUP_MENU_ACTIVE source=custom_vr_logo_official_game_buttons world_locked=1 follows_live_head=0 transparent_background_recovery=black_key laser=right_hand_white controller_ray=openxr_aim_pose native_submenus=live_capture third_scene_render=0");
    }
    return true;
}

void StartupMenuUi::reset_state()
{
    if (engine_button_down_ && EngineInputQueue::instance().queue_mouse_button(0,false))
        engine_button_down_=false;
    visible_=false; pointer_on_panel_=false; pointer_laser_active_=false; hovered_button_=0;
    dynamic_mode_=false; select_down_=false; pointer_client_valid_=false;
    pointer_client_initialized_=false;
    native_capture_requested_=true; native_capture_pointer_valid_=false;
    native_capture_due_ms_=native_capture_followup_ms_=0;
}

void StartupMenuUi::shutdown()
{
    release(alpha_blend_); release(rasterizer_); release(constants_); release(sampler_);
    release(laser_pixel_shader_); release(native_panel_pixel_shader_); release(panel_pixel_shader_);
    release(laser_vertex_shader_); release(panel_vertex_shader_);
    release(native_menu_view_); release(native_menu_texture_);
    release(menu_view_); release(menu_texture_); release(device_);
    asset_width_=asset_height_=native_width_=native_height_=0;
    panel_pose_={{0,0,0,1},{0,0,0}};
    dynamic_panel_pose_={{0,0,0,1},{0,0,0}}; pointer_u_=pointer_v_=0.5f;
    hidden_frames_after_menu_=hovered_button_=0;
    native_capture_count_=ui_click_count_=scroll_last_ms_=0;
    native_capture_due_ms_=native_capture_followup_ms_=0;
    world_state_poll_ms_=0;
    world_state_path_.clear();
    world_anchor_valid_=world_active_=world_state_known_=pointer_on_panel_=pointer_laser_active_=visible_=logged_=false;
    dynamic_mode_=select_down_=engine_button_down_=pointer_client_valid_=
        pointer_client_initialized_=native_capture_logged_=input_route_logged_=false;
    native_capture_requested_=true; native_capture_pointer_valid_=false;
}
} // namespace smvr::features
