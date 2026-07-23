#include "vr_tools.hpp"
#include "native_tool_asset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace scrapvr::tools
{
	namespace
	{
		using Vertex = native_tool_asset::Vertex;
		struct Matrix { float m[16]; };
		struct Constants { Matrix mvp; Matrix model; float eye_position[4]; };
		struct TgaHeader
		{
			uint8_t id_length, color_map_type, image_type;
			uint16_t color_map_first, color_map_length;
			uint8_t color_map_depth;
			uint16_t x_origin, y_origin, width, height;
			uint8_t pixel_depth, descriptor;
		};
		enum class Tool { none, hammer, connect, paint, weld, rifle, shotgun, gatling };
		enum DrawId
		{
			hammer_mesh, connect_mesh, paint_body, paint_can, weld_mesh,
			gun_grip, gun_body, gun_sight_screw, gun_sight, gun_stock, gun_tank,
			rifle_barrel, shotgun_barrel, shotgun_oil, gatling_barrel, draw_count
		};
		struct DrawResource
		{
			ID3D11Buffer *vertices = nullptr;
			ID3D11ShaderResourceView *texture = nullptr;
			uint32_t count = 0;
		};
		struct ToolCalibration
		{
			float tool_x = 0.0f, tool_y = -0.035f, tool_z = -0.045f;
			float laser_x = 0.0f, laser_y = 0.0f, laser_z = -0.300f;
		};

		ID3D11Device *g_device = nullptr;
		LogFunction g_log = nullptr;
		DrawResource g_draws[draw_count];
		ID3D11Buffer *g_constant_buffer = nullptr;
		ID3D11Buffer *g_laser_buffer = nullptr;
		ID3D11VertexShader *g_vertex_shader = nullptr;
		ID3D11PixelShader *g_pixel_shader = nullptr;
		ID3D11PixelShader *g_laser_pixel_shader = nullptr;
		ID3D11InputLayout *g_input_layout = nullptr;
		ID3D11SamplerState *g_sampler = nullptr;
		ID3D11RasterizerState *g_rasterizer = nullptr;
		ID3D11DepthStencilState *g_depth_state = nullptr;
		std::wstring g_game_root;
		Tool g_active_tool = Tool::none;
		bool g_player_seated = false;
		bool g_player_first_person = false;
		bool g_render_suppressed = false;
		ULONGLONG g_last_poll = 0;
		ULONGLONG g_gatling_animation_ms = 0;
		float g_gatling_angle = 0.0f;
		float g_gatling_speed = 0.0f;
		bool g_gatling_spin_logged = false;
		bool g_initialized = false;
		bool g_render_logged = false;

		template <typename T> void release(T *&value) { if (value) { value->Release(); value = nullptr; } }

		Matrix identity()
		{
			Matrix result = {};
			result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
			return result;
		}

		Matrix multiply(const Matrix &a, const Matrix &b)
		{
			Matrix result = {};
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					for (int k = 0; k < 4; ++k)
						result.m[column * 4 + row] += a.m[k * 4 + row] * b.m[column * 4 + k];
			return result;
		}

		XrVector3f rotate(const XrQuaternionf &q, const XrVector3f &v)
		{
			const XrVector3f u = { q.x, q.y, q.z };
			const float dot_uv = u.x * v.x + u.y * v.y + u.z * v.z;
			const float dot_uu = u.x * u.x + u.y * u.y + u.z * u.z;
			const XrVector3f cross = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
			return {
				2.0f * dot_uv * u.x + (q.w * q.w - dot_uu) * v.x + 2.0f * q.w * cross.x,
				2.0f * dot_uv * u.y + (q.w * q.w - dot_uu) * v.y + 2.0f * q.w * cross.y,
				2.0f * dot_uv * u.z + (q.w * q.w - dot_uu) * v.z + 2.0f * q.w * cross.z
			};
		}

		Matrix pose_matrix(const XrPosef &pose)
		{
			const float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
			Matrix result = identity();
			result.m[0] = 1 - 2 * (y * y + z * z); result.m[4] = 2 * (x * y - z * w); result.m[8] = 2 * (x * z + y * w);
			result.m[1] = 2 * (x * y + z * w); result.m[5] = 1 - 2 * (x * x + z * z); result.m[9] = 2 * (y * z - x * w);
			result.m[2] = 2 * (x * z - y * w); result.m[6] = 2 * (y * z + x * w); result.m[10] = 1 - 2 * (x * x + y * y);
			result.m[12] = pose.position.x; result.m[13] = pose.position.y; result.m[14] = pose.position.z;
			return result;
		}

		Matrix inverse_pose(const XrPosef &pose)
		{
			XrPosef inverse = {};
			inverse.orientation = { -pose.orientation.x, -pose.orientation.y, -pose.orientation.z, pose.orientation.w };
			inverse.position = rotate(inverse.orientation, { -pose.position.x, -pose.position.y, -pose.position.z });
			return pose_matrix(inverse);
		}

		Matrix projection(const XrFovf &fov, float near_z = 0.025f, float far_z = 100.0f)
		{
			const float left = std::tan(fov.angleLeft), right = std::tan(fov.angleRight);
			const float down = std::tan(fov.angleDown), up = std::tan(fov.angleUp);
			Matrix result = {};
			result.m[0] = 2.0f / (right - left); result.m[5] = 2.0f / (up - down);
			result.m[8] = (right + left) / (right - left); result.m[9] = (up + down) / (up - down);
			result.m[10] = -far_z / (far_z - near_z); result.m[11] = -1.0f;
			result.m[14] = -(far_z * near_z) / (far_z - near_z);
			return result;
		}

		Matrix translation(float x, float y, float z)
		{
			Matrix result = identity(); result.m[12] = x; result.m[13] = y; result.m[14] = z; return result;
		}

		Matrix uniform_scale(float value)
		{
			Matrix result = identity(); result.m[0] = result.m[5] = result.m[10] = value; return result;
		}

		Matrix tool_basis()
		{
			// Match the established Lua proxy orientation: mesh +Y points along the
			// tracked hand's -X, while mesh +Z points back along the controller.
			Matrix result = identity();
			result.m[0] = 0.0f; result.m[1] = -1.0f; result.m[2] = 0.0f;
			result.m[4] = -1.0f; result.m[5] = 0.0f; result.m[6] = 0.0f;
			result.m[8] = 0.0f; result.m[9] = 0.0f; result.m[10] = -1.0f;
			return result;
		}

		Matrix rotation_z(float angle)
		{
			Matrix result = identity(); const float c = std::cos(angle), s = std::sin(angle);
			result.m[0] = c; result.m[4] = -s; result.m[1] = s; result.m[5] = c;
			return result;
		}

		std::wstring module_root()
		{
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"scrap_native_vr.addon64");
			if (!module || !GetModuleFileNameW(module, module_path, MAX_PATH)) return {};
			std::wstring path(module_path);
			auto slash = path.find_last_of(L"\\/"); if (slash == std::wstring::npos) return {};
			path.resize(slash); slash = path.find_last_of(L"\\/"); if (slash == std::wstring::npos) return {};
			path.resize(slash);
			return path;
		}

		bool load_tga(const std::wstring &relative, std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height)
		{
			std::ifstream file((g_game_root + L"\\" + relative).c_str(), std::ios::binary);
			if (!file) return false;
			uint8_t raw[18] = {}; file.read(reinterpret_cast<char *>(raw), sizeof(raw)); if (!file) return false;
			TgaHeader header = {};
			header.id_length = raw[0]; header.color_map_type = raw[1]; header.image_type = raw[2];
			header.width = static_cast<uint16_t>(raw[12] | raw[13] << 8); header.height = static_cast<uint16_t>(raw[14] | raw[15] << 8);
			header.pixel_depth = raw[16]; header.descriptor = raw[17];
			if (header.color_map_type || (header.image_type != 2 && header.image_type != 10) ||
				(header.pixel_depth != 24 && header.pixel_depth != 32) || !header.width || !header.height) return false;
			file.seekg(header.id_length, std::ios::cur); width = header.width; height = header.height;
			rgba.resize(static_cast<size_t>(width) * height * 4);
			const uint32_t bpp = header.pixel_depth / 8; const bool top = (header.descriptor & 0x20) != 0; uint32_t pixel = 0;
			auto write = [&](const uint8_t *bgra)
			{
				const uint32_t x = pixel % width, sy = pixel / width, y = top ? sy : height - 1 - sy;
				auto *dst = &rgba[(static_cast<size_t>(y) * width + x) * 4];
				dst[0] = bgra[2]; dst[1] = bgra[1]; dst[2] = bgra[0]; dst[3] = bpp == 4 ? bgra[3] : 255; ++pixel;
			};
			while (pixel < width * height && file)
			{
				uint8_t sample[4] = { 0, 0, 0, 255 };
				if (header.image_type == 2) { file.read(reinterpret_cast<char *>(sample), bpp); write(sample); continue; }
				uint8_t packet = 0; file.read(reinterpret_cast<char *>(&packet), 1); const uint32_t count = (packet & 0x7f) + 1;
				if (packet & 0x80) { file.read(reinterpret_cast<char *>(sample), bpp); for (uint32_t i = 0; i < count && pixel < width * height; ++i) write(sample); }
				else for (uint32_t i = 0; i < count && pixel < width * height; ++i) { file.read(reinterpret_cast<char *>(sample), bpp); write(sample); }
			}
			return pixel == width * height;
		}

		bool create_texture(const wchar_t *relative, ID3D11ShaderResourceView **output)
		{
			std::vector<uint8_t> pixels; uint32_t width = 0, height = 0;
			if (!load_tga(relative, pixels, width, height)) { if (g_log) g_log("VR TOOL RENDERER: texture decode failed"); return false; }
			D3D11_TEXTURE2D_DESC desc = {}; desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = pixels.data(); data.SysMemPitch = width * 4;
			ID3D11Texture2D *texture = nullptr;
			const bool ok = SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)) && SUCCEEDED(g_device->CreateShaderResourceView(texture, nullptr, output));
			release(texture); return ok;
		}

		bool create_draw(DrawId id, const Vertex *vertices, uint32_t count, const wchar_t *texture)
		{
			DrawResource &draw = g_draws[id]; draw.count = count;
			D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = count * sizeof(Vertex); desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = vertices;
			return SUCCEEDED(g_device->CreateBuffer(&desc, &data, &draw.vertices)) && create_texture(texture, &draw.texture);
		}

		void draw_resource(ID3D11DeviceContext *context, DrawId id)
		{
			DrawResource &draw = g_draws[id]; if (!draw.vertices || !draw.texture) return;
			UINT stride = sizeof(Vertex), offset = 0;
			context->IASetVertexBuffers(0, 1, &draw.vertices, &stride, &offset);
			context->PSSetShaderResources(0, 1, &draw.texture);
			context->Draw(draw.count, 0);
		}

		const char *tool_name(Tool tool)
		{
			switch (tool) { case Tool::hammer: return "hammer"; case Tool::connect: return "connect"; case Tool::paint: return "paint"; case Tool::weld: return "weld"; case Tool::rifle: return "spudgun"; case Tool::shotgun: return "shotgun"; case Tool::gatling: return "gatling"; default: return "none"; }
		}

		Tool parse_tool(const std::string &line)
		{
			if (line.find("bb641a4f-e391-441c-bc6d-0ae21a069476") != std::string::npos) return Tool::hammer;
			if (line.find("8c7efc37-cd7c-4262-976e-39585f8527bf") != std::string::npos) return Tool::connect;
			if (line.find("c60b9627-fc2b-4319-97c5-05921cb976c6") != std::string::npos) return Tool::paint;
			if (line.find("fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce") != std::string::npos) return Tool::weld;
			if (line.find("c5ea0c2f-185b-48d6-b4df-45c386a575cc") != std::string::npos) return Tool::rifle;
			if (line.find("f6250bf4-9726-406f-a29a-945c06e460e5") != std::string::npos) return Tool::shotgun;
			if (line.find("9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b") != std::string::npos) return Tool::gatling;
			return Tool::none;
		}

		const ToolCalibration &calibration_for(Tool tool)
		{
			// Final values tuned in-headset on 2026-07-18. These are intentionally
			// baked so normal play performs no calibration-file polling.
			static const ToolCalibration values[8] = {
				{  0.000f, -0.035f, -0.045f,  0.000f,  0.000f, -0.300f }, // none
				{  0.000f, -0.025f, -0.065f,  0.000f,  0.000f, -0.300f }, // hammer
				{ -0.020f, -0.035f, -0.055f, -0.152f, -0.035f, -0.280f }, // connect
				{ -0.015f, -0.040f, -0.060f, -0.120f, -0.040f, -0.295f }, // paint
				{ -0.030f, -0.035f, -0.065f, -0.035f, -0.035f, -0.225f }, // weld
				{ -0.020f, -0.035f, -0.060f,  0.000f,  0.000f, -0.300f }, // spudgun
				{ -0.020f, -0.035f, -0.060f,  0.000f,  0.000f, -0.300f }, // shotgun
				{ -0.020f, -0.035f, -0.060f,  0.000f,  0.000f, -0.300f }  // gatling
			};
			return values[static_cast<size_t>(tool)];
		}

		void update_gatling_animation(bool firing)
		{
			const ULONGLONG now = GetTickCount64();
			if (!g_gatling_animation_ms) { g_gatling_animation_ms = now; return; }
			const ULONGLONG elapsed_ms = now - g_gatling_animation_ms;
			// Avoid advancing separately for the two stereo-eye submissions.
			if (elapsed_ms < 6) return;
			g_gatling_animation_ms = now;
			const float dt = std::min(0.050f, static_cast<float>(elapsed_ms) * 0.001f);
			const float target_speed = firing ? 30.0f : 0.0f;
			const float acceleration = firing ? 72.0f : 48.0f;
			if (g_gatling_speed < target_speed) g_gatling_speed = std::min(target_speed, g_gatling_speed + acceleration * dt);
			else g_gatling_speed = std::max(target_speed, g_gatling_speed - acceleration * dt);
			g_gatling_angle = std::fmod(g_gatling_angle + g_gatling_speed * dt, 6.28318530718f);
			if (firing && !g_gatling_spin_logged && g_log) { g_gatling_spin_logged = true; g_log("VR GATLING SPINNER ACTIVE: trigger-driven barrel spin-up and spin-down"); }
		}

		void poll_active_tool()
		{
			const ULONGLONG now = GetTickCount64(); if (now - g_last_poll < 150) return; g_last_poll = now;
			WIN32_FIND_DATAW data = {}; HANDLE search = FindFirstFileW((g_game_root + L"\\Logs\\game-*.log").c_str(), &data);
			if (search == INVALID_HANDLE_VALUE) return;
			std::wstring newest; FILETIME newest_time = {};
			do
			{
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && CompareFileTime(&data.ftLastWriteTime, &newest_time) > 0)
				{ newest_time = data.ftLastWriteTime; newest = data.cFileName; }
			} while (FindNextFileW(search, &data));
			FindClose(search); if (newest.empty()) return;
			HANDLE file = CreateFileW((g_game_root + L"\\Logs\\" + newest).c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return;
			LARGE_INTEGER size = {}; GetFileSizeEx(file, &size); const DWORD take = static_cast<DWORD>(std::min<LONGLONG>(size.QuadPart, 65536));
			LARGE_INTEGER offset = {}; offset.QuadPart = size.QuadPart - take; SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
			std::string text(take, '\0'); DWORD read = 0; ReadFile(file, text.data(), take, &read, nullptr); CloseHandle(file); text.resize(read);
			const std::string tool_marker = "SCRAPVR_NATIVE_TOOL ";
			const auto tool_found = text.rfind(tool_marker);
			if (tool_found != std::string::npos)
			{
				const auto end = text.find_first_of("\r\n", tool_found);
				const Tool next = parse_tool(text.substr(tool_found, end - tool_found));
				if (next != g_active_tool) { g_active_tool = next; if (g_log) g_log("VR TOOL ACTIVE: %s", tool_name(next)); }
			}
			const std::string seat_marker = "SCRAPVR_SEATED ";
			const auto seat_found = text.rfind(seat_marker);
			if (seat_found != std::string::npos)
			{
				const bool seated = seat_found + seat_marker.size() < text.size() &&
					text[seat_found + seat_marker.size()] == '1';
				if (seated != g_player_seated)
				{
					g_player_seated = seated;
					if (g_log) g_log("VR SEAT INPUT MODE: %s", seated ? "zoom X/C" : "hotbar X/Y");
				}
			}
			const std::string first_person_marker = "SCRAPVR_FIRST_PERSON ";
			const auto first_person_found = text.rfind(first_person_marker);
			if (first_person_found != std::string::npos)
			{
				const bool first_person =
					first_person_found + first_person_marker.size() < text.size() &&
					text[first_person_found + first_person_marker.size()] == '1';
				if (first_person != g_player_first_person)
				{
					g_player_first_person = first_person;
					if (g_log) g_log("VR CAMERA VIEW: %s", first_person ? "first person" : "third person");
				}
			}
		}

		float scale_for(Tool tool)
		{
			switch (tool) { case Tool::weld: return 0.15f; case Tool::rifle: case Tool::shotgun: case Tool::gatling: return 0.145f; default: return 0.16f; }
		}
	}

	bool initialize(ID3D11Device *device, LogFunction log)
	{
		if (g_initialized) return true;
		g_device = device; g_log = log; g_game_root = module_root(); if (!g_device || g_game_root.empty()) return false;
		const char *shader = R"(
			cbuffer ToolConstants : register(b0) { float4x4 mvp; float4x4 model; float4 eye_position; };
			struct VSIn { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
			struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float3 world_normal : TEXCOORD1; float3 world_position : TEXCOORD2; };
			VSOut vs_main(VSIn input) { VSOut o; float4 local = float4(input.position, 1); float4 world = mul(model, local);
				o.position = mul(mvp, local); o.uv = input.uv; o.world_position = world.xyz;
				o.world_normal = normalize(mul((float3x3)model, input.normal)); return o; }
			Texture2D tex : register(t0); SamplerState samp : register(s0);
			float4 ps_main(VSOut input) : SV_TARGET { float4 c = tex.Sample(samp, input.uv); float3 n = normalize(input.world_normal);
				float3 key_direction = normalize(float3(-0.35, 0.78, -0.52)); float sky = 0.5 + 0.5 * n.y;
				float ambient = lerp(0.53, 0.69, sky); float key = 0.30 * saturate(dot(n, key_direction));
				float fill = 0.09 * saturate(dot(n, normalize(float3(0.65, 0.25, 0.72))));
				float3 view_direction = normalize(eye_position.xyz - input.world_position);
				float specular = 0.065 * pow(saturate(dot(n, normalize(key_direction + view_direction))), 30.0);
				float3 linear_lit = c.rgb * (ambient + key + fill) + specular;
				return float4(pow(saturate(linear_lit), 1.0 / 2.2), c.a); }
			float4 ps_laser(VSOut input) : SV_TARGET { return float4(1, 1, 1, 1); }
		)";
		HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
		using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
		auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
		ID3DBlob *vs = nullptr, *ps = nullptr, *laser_ps = nullptr, *errors = nullptr;
		if (!compile || FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs, &errors))) { release(errors); if (compiler) FreeLibrary(compiler); return false; }
		release(errors);
		if (FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps, &errors)) ||
			FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_laser", "ps_5_0", 0, 0, &laser_ps, &errors)))
		{ release(errors); release(vs); release(ps); release(laser_ps); if (compiler) FreeLibrary(compiler); return false; }
		if (compiler) FreeLibrary(compiler);
		if (FAILED(g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vertex_shader)) ||
			FAILED(g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_pixel_shader)) ||
			FAILED(g_device->CreatePixelShader(laser_ps->GetBufferPointer(), laser_ps->GetBufferSize(), nullptr, &g_laser_pixel_shader))) return false;
		D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		if (FAILED(g_device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &g_input_layout))) return false;
		release(vs); release(ps); release(laser_ps); release(errors);
		D3D11_BUFFER_DESC cb = {}; cb.ByteWidth = sizeof(Constants); cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_constant_buffer))) return false;
		D3D11_BUFFER_DESC lb = {}; lb.ByteWidth = 2 * sizeof(Vertex); lb.Usage = D3D11_USAGE_DYNAMIC; lb.BindFlags = D3D11_BIND_VERTEX_BUFFER; lb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(g_device->CreateBuffer(&lb, nullptr, &g_laser_buffer))) return false;
		D3D11_SAMPLER_DESC sampler = {}; sampler.Filter = D3D11_FILTER_ANISOTROPIC; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxAnisotropy = 8; sampler.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(g_device->CreateSamplerState(&sampler, &g_sampler))) return false;
		D3D11_RASTERIZER_DESC raster = {}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
		if (FAILED(g_device->CreateRasterizerState(&raster, &g_rasterizer))) return false;
		D3D11_DEPTH_STENCIL_DESC depth = {}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(g_device->CreateDepthStencilState(&depth, &g_depth_state))) return false;

		using namespace native_tool_asset;
		const wchar_t *root = L"Data\\Character\\Char_Tools\\";
		auto path = [&](const wchar_t *tail) { return std::wstring(root) + tail; };
		if (!create_draw(hammer_mesh, hammer_main_0_NewUV_sledgehammer_new_initialShadingGroup4_vertices, hammer_main_0_NewUV_sledgehammer_new_initialShadingGroup4_vertex_count, path(L"Char_sledgehammer\\char_sledgehammer_dif.tga").c_str()) ||
			!create_draw(connect_mesh, connect_main_0_char_connecttool_mat_vertices, connect_main_0_char_connecttool_mat_vertex_count, path(L"Char_connecttool\\char_connecttool_dif.tga").c_str()) ||
			!create_draw(paint_body, paint_main_0_char_painttool_mat_vertices, paint_main_0_char_painttool_mat_vertex_count, path(L"Char_painttool\\char_painttool_dif.tga").c_str()) ||
			!create_draw(paint_can, paint_main_1_char_paintcan_mat_vertices, paint_main_1_char_paintcan_mat_vertex_count, path(L"Char_painttool\\char_paintcan_dif.tga").c_str()) ||
			!create_draw(weld_mesh, weld_main_0_char_weldtool_mat_vertices, weld_main_0_char_weldtool_mat_vertex_count, path(L"Char_weldtool\\char_weldtool_dif.tga").c_str()) ||
			!create_draw(gun_grip, gunshared_base_0_char_spudgun_grip_mat_vertices, gunshared_base_0_char_spudgun_grip_mat_vertex_count, path(L"Char_spudgun\\Base\\char_spudgun_grip_dif.tga").c_str()) ||
			!create_draw(gun_body, gunshared_base_1_char_spudgun_base_mat_vertices, gunshared_base_1_char_spudgun_base_mat_vertex_count, path(L"Char_spudgun\\Base\\char_spudgun_base_dif.tga").c_str()) ||
			!create_draw(gun_sight_screw, gunshared_sight_0_sightscrew_basicbarrel_mat_vertices, gunshared_sight_0_sightscrew_basicbarrel_mat_vertex_count, path(L"Char_spudgun\\Sight\\Sight_basic\\char_spudgun_sight_basic_screw_dif.tga").c_str()) ||
			!create_draw(gun_sight, gunshared_sight_1_sight_basic_mat_vertices, gunshared_sight_1_sight_basic_mat_vertex_count, path(L"Char_spudgun\\Sight\\Sight_basic\\char_spudgun_sight_basic_dif.tga").c_str()) ||
			!create_draw(gun_stock, gunshared_stock_0_lambert2_vertices, gunshared_stock_0_lambert2_vertex_count, path(L"Char_spudgun\\Stock\\Stock_broom\\char_spudgun_stock_broom_dif.tga").c_str()) ||
			!create_draw(gun_tank, gunshared_tank_0_lambert2_vertices, gunshared_tank_0_lambert2_vertex_count, path(L"Char_spudgun\\Tank\\Tank_basic\\char_spudgun_tank_basic_dif.TGA").c_str()) ||
			!create_draw(rifle_barrel, rifle_barrel_0_lambert1_vertices, rifle_barrel_0_lambert1_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_basic\\char_spudgun_barrel_basic_dif.tga").c_str()) ||
			!create_draw(shotgun_barrel, shotgun_barrel_0_barrel_vertices, shotgun_barrel_0_barrel_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_frier\\char_spudgun_barrel_frier_dif.tga").c_str()) ||
			!create_draw(shotgun_oil, shotgun_barrel_1_fryeroil_vertices, shotgun_barrel_1_fryeroil_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_frier\\char_spudgun_barrel_frier_oil_dif.tga").c_str()) ||
			!create_draw(gatling_barrel, gatling_barrel_0_barrel_spinner_mat_vertices, gatling_barrel_0_barrel_spinner_mat_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_spinner\\char_spudgun_barrel_spinner_dif.tga").c_str()))
		{ if (g_log) g_log("VR TOOL RENDERER: a native mesh or texture resource failed to initialize"); return false; }
		g_initialized = true;
		if (g_log) g_log("VR TOOL RENDERER READY: hammer, connect, paint, weld and three spudgun variants use the direct stereo hand pass");
		return true;
	}

	bool render(ID3D11DeviceContext *context, ID3D11RenderTargetView *target, ID3D11DepthStencilView *depth,
		uint32_t width, uint32_t height, const XrView &eye, const XrPosef &right_hand_pose, bool right_hand_active, bool right_firing)
	{
		if (!g_initialized || !context || !target || !depth) return false;
		poll_active_tool(); if (g_render_suppressed || !right_hand_active || g_active_tool == Tool::none) return false;
		context->OMSetRenderTargets(1, &target, depth);
		D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
		context->RSSetViewports(1, &viewport); context->RSSetState(g_rasterizer); context->OMSetDepthStencilState(g_depth_state, 0);
		context->IASetInputLayout(g_input_layout); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(g_vertex_shader, nullptr, 0); context->VSSetConstantBuffers(0, 1, &g_constant_buffer);
		context->PSSetShader(g_pixel_shader, nullptr, 0); context->PSSetSamplers(0, 1, &g_sampler);
		const Matrix view_projection = multiply(projection(eye.fov), inverse_pose(eye.pose));
		const auto &calibration = calibration_for(g_active_tool);
		const Matrix local = multiply(translation(calibration.tool_x, calibration.tool_y, calibration.tool_z), multiply(tool_basis(), uniform_scale(scale_for(g_active_tool))));
		const Matrix model = multiply(pose_matrix(right_hand_pose), local);
		Constants constants = { multiply(view_projection, model), model,
			{ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f } };
		context->UpdateSubresource(g_constant_buffer, 0, nullptr, &constants, 0, 0);
		switch (g_active_tool)
		{
			case Tool::hammer: draw_resource(context, hammer_mesh); break;
			case Tool::connect: draw_resource(context, connect_mesh); break;
			case Tool::paint: draw_resource(context, paint_body); draw_resource(context, paint_can); break;
			case Tool::weld: draw_resource(context, weld_mesh); break;
			case Tool::rifle: case Tool::shotgun: case Tool::gatling:
				draw_resource(context, gun_grip); draw_resource(context, gun_body); draw_resource(context, gun_sight_screw);
				draw_resource(context, gun_sight); draw_resource(context, gun_stock); draw_resource(context, gun_tank);
				if (g_active_tool == Tool::rifle) draw_resource(context, rifle_barrel);
				else if (g_active_tool == Tool::shotgun) { draw_resource(context, shotgun_barrel); draw_resource(context, shotgun_oil); }
				else
				{
					update_gatling_animation(right_firing);
					// The spinner's longitudinal axis is local +Z through y=1.248825.
					const Matrix spin = multiply(translation(0.0f, 1.248825f, 0.0f),
						multiply(rotation_z(g_gatling_angle), translation(0.0f, -1.248825f, 0.0f)));
					const Matrix spinner_model = multiply(model, spin);
					Constants spinner_constants = { multiply(view_projection, spinner_model), spinner_model,
						{ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f } };
					context->UpdateSubresource(g_constant_buffer, 0, nullptr, &spinner_constants, 0, 0);
					draw_resource(context, gatling_barrel);
				}
				break;
			default: break;
		}

		if (g_active_tool == Tool::connect || g_active_tool == Tool::paint || g_active_tool == Tool::weld)
		{
			Vertex laser[2] = {
				{ calibration.laser_x, calibration.laser_y, calibration.laser_z, 0, 0, 1, 0, 0 },
				{ calibration.laser_x, calibration.laser_y, -8.0f, 0, 0, 1, 0, 0 }
			};
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(g_laser_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { std::memcpy(mapped.pData, laser, sizeof(laser)); context->Unmap(g_laser_buffer, 0); }
			const Matrix laser_model = pose_matrix(right_hand_pose); Constants laser_constants = { multiply(view_projection, laser_model), laser_model,
				{ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f } };
			context->UpdateSubresource(g_constant_buffer, 0, nullptr, &laser_constants, 0, 0);
			UINT stride = sizeof(Vertex), offset = 0; context->IASetVertexBuffers(0, 1, &g_laser_buffer, &stride, &offset);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST); context->PSSetShader(g_laser_pixel_shader, nullptr, 0); context->Draw(2, 0);
			// The second stereo eye begins immediately after this pass and some Scrap
			// Mechanic shadow draws inherit topology/shader state. Leaving LINELIST here
			// turned the right-eye shadow geometry into a wire mesh only for laser tools.
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->PSSetShader(g_pixel_shader, nullptr, 0);
		}
		ID3D11ShaderResourceView *none = nullptr; context->PSSetShaderResources(0, 1, &none);
		if (!g_render_logged && g_log) { g_render_logged = true; g_log("NATIVE VR TOOLS VISIBLE: selected tool and pointer share the tracked-hand stereo pose and depth buffer"); }
		return true;
	}

	bool get_interaction_laser_offset(XrVector3f &offset)
	{
		if (g_active_tool != Tool::connect && g_active_tool != Tool::paint &&
			g_active_tool != Tool::weld)
			return false;
		const auto &calibration = calibration_for(g_active_tool);
		offset = { calibration.laser_x, calibration.laser_y, calibration.laser_z };
		return true;
	}

	bool is_hammer_active()
	{
		return g_active_tool == Tool::hammer;
	}

	bool is_player_seated()
	{
		poll_active_tool();
		return g_player_seated;
	}

	bool is_player_first_person()
	{
		poll_active_tool();
		return g_player_first_person;
	}

	void set_render_suppressed(bool suppressed)
	{
		g_render_suppressed = suppressed;
	}

	void shutdown()
	{
		for (auto &draw : g_draws) { release(draw.vertices); release(draw.texture); draw.count = 0; }
		release(g_depth_state); release(g_rasterizer); release(g_sampler); release(g_input_layout);
		release(g_laser_pixel_shader); release(g_pixel_shader); release(g_vertex_shader); release(g_laser_buffer); release(g_constant_buffer);
		g_device = nullptr; g_log = nullptr; g_game_root.clear(); g_active_tool = Tool::none;
		g_player_seated = false; g_player_first_person = false;
		g_render_suppressed = false; g_last_poll = 0;
		g_gatling_animation_ms = 0; g_gatling_angle = 0.0f; g_gatling_speed = 0.0f; g_gatling_spin_logged = false;
		g_initialized = false; g_render_logged = false;
	}
}
