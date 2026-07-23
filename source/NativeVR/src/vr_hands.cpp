#include "vr_hands.hpp"
#include "vr_tools.hpp"
#include "mechanic_hands_asset.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace scrapvr::hands
{
	namespace
	{
		using Vertex = mechanic_hands_asset::Vertex;
		struct Matrix { float m[16]; };
		struct TrackedPose
		{
			XrPosef pose = { { 0, 0, 0, 1 }, { 0, 0, 0 } };
			float curls[5] = {};
			ULONGLONG last_valid_ms = 0;
			bool active = false;
			bool optical = false;
			bool interaction = false;
			bool firing = false;
		};
		struct HandConstants { Matrix mvp; Matrix model; Matrix bones[24]; float eye_position[4]; };
		struct TgaHeader
		{
			uint8_t id_length, color_map_type, image_type;
			uint16_t color_map_first, color_map_length;
			uint8_t color_map_depth;
			uint16_t x_origin, y_origin, width, height;
			uint8_t pixel_depth, descriptor;
		};

		ID3D11Device *g_device = nullptr;
		LogFunction g_log = nullptr;
		ID3D11Buffer *g_vertex_buffers[2] = {};
		ID3D11Buffer *g_constant_buffer = nullptr;
		ID3D11VertexShader *g_vertex_shader = nullptr;
		ID3D11PixelShader *g_pixel_shader = nullptr;
		ID3D11InputLayout *g_input_layout = nullptr;
		ID3D11ShaderResourceView *g_texture = nullptr;
		ID3D11SamplerState *g_sampler = nullptr;
		ID3D11RasterizerState *g_rasterizer = nullptr;
		ID3D11DepthStencilState *g_depth_state = nullptr;
		ID3D11Texture2D *g_depth_texture = nullptr;
		ID3D11DepthStencilView *g_depth_view = nullptr;
		uint32_t g_depth_width = 0, g_depth_height = 0;
		TrackedPose g_poses[2];
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
			const XrVector3f negative = { -pose.position.x, -pose.position.y, -pose.position.z };
			inverse.position = rotate(inverse.orientation, negative);
			return pose_matrix(inverse);
		}

		Matrix projection(const XrFovf &fov, float near_z = 0.025f, float far_z = 100.0f)
		{
			const float left = std::tan(fov.angleLeft), right = std::tan(fov.angleRight);
			const float down = std::tan(fov.angleDown), up = std::tan(fov.angleUp);
			Matrix result = {};
			result.m[0] = 2.0f / (right - left);
			result.m[5] = 2.0f / (up - down);
			result.m[8] = (right + left) / (right - left);
			result.m[9] = (up + down) / (up - down);
			result.m[10] = -far_z / (far_z - near_z);
			result.m[11] = -1.0f;
			result.m[14] = -(far_z * near_z) / (far_z - near_z);
			return result;
		}

		Matrix translation(float x, float y, float z)
		{
			Matrix result = identity(); result.m[12] = x; result.m[13] = y; result.m[14] = z; return result;
		}

		Matrix uniform_scale(float value)
		{
			Matrix result = identity();
			result.m[0] = result.m[5] = result.m[10] = value;
			return result;
		}

		Matrix rotation_x(float angle)
		{
			Matrix result = identity(); const float c = std::cos(angle), s = std::sin(angle);
			result.m[5] = c; result.m[9] = -s; result.m[6] = s; result.m[10] = c; return result;
		}

		XrQuaternionf quaternion_multiply(const XrQuaternionf &a, const XrQuaternionf &b)
		{
			return {
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
			};
		}

		XrQuaternionf axis_rotation(float x, float y, float z, float degrees)
		{
			constexpr float degrees_to_half_radians = 0.00872664625997164788f;
			const float half_angle = degrees * degrees_to_half_radians;
			const float sine = std::sin(half_angle);
			return { x * sine, y * sine, z * sine, std::cos(half_angle) };
		}

		XrQuaternionf controller_calibration(uint32_t hand)
		{
			// Final Touch-controller calibration selected in-headset. Optical palm
			// tracking deliberately bypasses this controller-specific grip transform.
			const float y = hand == 0 ? 75.0f : -75.0f;
			const float z = hand == 0 ? 90.0f : -90.0f;
			return quaternion_multiply(
				axis_rotation(0.0f, 0.0f, 1.0f, z),
				axis_rotation(0.0f, 1.0f, 0.0f, y));
		}

		Matrix rotate_about(const mechanic_hands_asset::BonePivot &pivot, float angle)
		{
			return multiply(
				translation(pivot.x, pivot.y, pivot.z),
				multiply(rotation_x(angle), translation(-pivot.x, -pivot.y, -pivot.z)));
		}

		void build_bones(uint32_t hand, Matrix output[24])
		{
			const auto *pivots = hand == 0
				? mechanic_hands_asset::left_bone_pivots
				: mechanic_hands_asset::right_bone_pivots;
			for (uint32_t bone = 0; bone < 24; ++bone)
			{
				output[bone] = identity();
				const int finger = pivots[bone].finger;
				const int last_segment = pivots[bone].segment > 3 ? 3 : pivots[bone].segment;
				if (finger < 0 || last_segment <= 0)
					continue;
				const float per_joint = -g_poses[hand].curls[finger] * (finger == 0 ? 0.38f : 0.55f);
				for (int segment = 1; segment <= last_segment; ++segment)
					for (uint32_t pivot_bone = 0; pivot_bone < 24; ++pivot_bone)
						if (pivots[pivot_bone].finger == finger && pivots[pivot_bone].segment == segment)
						{
							output[bone] = multiply(output[bone], rotate_about(pivots[pivot_bone], per_joint));
							break;
						}
			}
		}

		std::wstring texture_path()
		{
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"scrap_native_vr.addon64");
			if (!module || GetModuleFileNameW(module, module_path, MAX_PATH) == 0)
				return {};
			std::wstring path(module_path);
			const size_t slash = path.find_last_of(L"\\/");
			if (slash == std::wstring::npos)
				return {};
			path.resize(slash);
			path += L"\\..\\Survival\\Character\\Char_Male\\Outfit\\Gloves\\Outfit_default_gloves\\outfit_default_gloves_dif.tga";
			return path;
		}

		bool load_tga(std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height)
		{
			const std::wstring path = texture_path();
			std::ifstream file(path.c_str(), std::ios::binary);
			if (!file)
				return false;
			uint8_t raw_header[18] = {};
			file.read(reinterpret_cast<char *>(raw_header), sizeof(raw_header));
			if (!file)
				return false;
			TgaHeader header = {};
			header.id_length = raw_header[0]; header.color_map_type = raw_header[1]; header.image_type = raw_header[2];
			header.width = static_cast<uint16_t>(raw_header[12] | raw_header[13] << 8);
			header.height = static_cast<uint16_t>(raw_header[14] | raw_header[15] << 8);
			header.pixel_depth = raw_header[16]; header.descriptor = raw_header[17];
			if (header.color_map_type != 0 || (header.image_type != 2 && header.image_type != 10) ||
				(header.pixel_depth != 24 && header.pixel_depth != 32) || header.width == 0 || header.height == 0)
				return false;
			file.seekg(header.id_length, std::ios::cur);
			width = header.width; height = header.height;
			rgba.resize(static_cast<size_t>(width) * height * 4);
			const uint32_t bytes_per_pixel = header.pixel_depth / 8;
			const bool top_origin = (header.descriptor & 0x20) != 0;
			uint32_t pixel = 0;
			auto write_pixel = [&](const uint8_t *bgra)
			{
				const uint32_t x = pixel % width, source_y = pixel / width;
				const uint32_t y = top_origin ? source_y : (height - 1 - source_y);
				uint8_t *destination = &rgba[(static_cast<size_t>(y) * width + x) * 4];
				destination[0] = bgra[2]; destination[1] = bgra[1]; destination[2] = bgra[0];
				destination[3] = bytes_per_pixel == 4 ? bgra[3] : 255;
				++pixel;
			};
			while (pixel < width * height && file)
			{
				uint8_t sample[4] = { 0, 0, 0, 255 };
				if (header.image_type == 2)
				{
					file.read(reinterpret_cast<char *>(sample), bytes_per_pixel);
					write_pixel(sample);
					continue;
				}
				uint8_t packet = 0; file.read(reinterpret_cast<char *>(&packet), 1);
				const uint32_t count = (packet & 0x7f) + 1;
				if (packet & 0x80)
				{
					file.read(reinterpret_cast<char *>(sample), bytes_per_pixel);
					for (uint32_t i = 0; i < count && pixel < width * height; ++i) write_pixel(sample);
				}
				else
					for (uint32_t i = 0; i < count && pixel < width * height; ++i)
					{
						file.read(reinterpret_cast<char *>(sample), bytes_per_pixel); write_pixel(sample);
					}
			}
			return pixel == width * height;
		}

		bool create_depth(uint32_t width, uint32_t height)
		{
			if (g_depth_view && width == g_depth_width && height == g_depth_height)
				return true;
			release(g_depth_view); release(g_depth_texture);
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_depth_texture)) ||
				FAILED(g_device->CreateDepthStencilView(g_depth_texture, nullptr, &g_depth_view)))
				return false;
			g_depth_width = width; g_depth_height = height;
			return true;
		}
	}

	bool initialize(ID3D11Device *device, LogFunction log)
	{
		if (g_initialized)
			return true;
		g_device = device; g_log = log;
		if (!g_device)
			return false;

		const char *shader = R"(
			cbuffer HandConstants : register(b0) { float4x4 mvp; float4x4 model; float4x4 bones[24]; float4 eye_position; };
			struct VSIn { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; uint4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT; };
			struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float3 world_normal : TEXCOORD1; float3 world_position : TEXCOORD2; };
			VSOut vs_main(VSIn input) {
				float4 position = 0.0; float3 normal = 0.0;
				[unroll] for (uint i = 0; i < 4; ++i) { position += input.weights[i] * mul(bones[input.joints[i]], float4(input.position, 1.0)); normal += input.weights[i] * mul((float3x3)bones[input.joints[i]], input.normal); }
				VSOut output; float4 world = mul(model, position); output.position = mul(mvp, position); output.uv = input.uv;
				output.world_position = world.xyz; output.world_normal = normalize(mul((float3x3)model, normal)); return output;
			}
			Texture2D glove_texture : register(t0); SamplerState glove_sampler : register(s0);
			float4 ps_main(VSOut input) : SV_TARGET {
				float4 color = glove_texture.Sample(glove_sampler, input.uv); float3 n = normalize(input.world_normal);
				float3 key_direction = normalize(float3(-0.35, 0.78, -0.52));
				float sky = 0.5 + 0.5 * n.y; float ambient = lerp(0.53, 0.69, sky);
				float key = 0.30 * saturate(dot(n, key_direction));
				float fill = 0.09 * saturate(dot(n, normalize(float3(0.65, 0.25, 0.72))));
				float3 view_direction = normalize(eye_position.xyz - input.world_position);
				float3 half_vector = normalize(key_direction + view_direction);
				float specular = 0.055 * pow(saturate(dot(n, half_vector)), 28.0);
				float3 linear_lit = color.rgb * (ambient + key + fill) + specular;
				// Eye RTVs are deliberately UNORM even when the OpenXR image is sRGB.
				// Encode the linear texture result once here instead of displaying it too dark.
				return float4(pow(saturate(linear_lit), 1.0 / 2.2), color.a);
			}
		)";
		HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
		using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
		auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
		ID3DBlob *vs_blob = nullptr, *ps_blob = nullptr, *errors = nullptr;
		if (!compile || FAILED(compile(shader, std::strlen(shader), "vr_hands", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_blob, &errors)))
		{
			if (g_log) g_log("VR HAND RENDERER: vertex shader compilation failed%s%s", errors ? ": " : "", errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
			release(errors); if (compiler) FreeLibrary(compiler); return false;
		}
		release(errors);
		if (FAILED(compile(shader, std::strlen(shader), "vr_hands", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps_blob, &errors)))
		{
			if (g_log) g_log("VR HAND RENDERER: pixel shader compilation failed%s%s", errors ? ": " : "", errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
			release(errors); release(vs_blob); if (compiler) FreeLibrary(compiler); return false;
		}
		if (compiler) FreeLibrary(compiler);
		if (FAILED(g_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vertex_shader)) ||
			FAILED(g_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_pixel_shader)))
			return false;
		D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		if (FAILED(g_device->CreateInputLayout(elements, 5, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_input_layout)))
			return false;
		release(vs_blob); release(ps_blob); release(errors);
		const Vertex *vertices[] = { mechanic_hands_asset::left_vertices, mechanic_hands_asset::right_vertices };
		const uint32_t counts[] = { mechanic_hands_asset::left_vertex_count, mechanic_hands_asset::right_vertex_count };
		for (uint32_t hand = 0; hand < 2; ++hand)
		{
			D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = counts[hand] * sizeof(Vertex); desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = vertices[hand];
			if (FAILED(g_device->CreateBuffer(&desc, &data, &g_vertex_buffers[hand]))) return false;
		}
		D3D11_BUFFER_DESC constant_desc = {}; constant_desc.ByteWidth = sizeof(HandConstants); constant_desc.Usage = D3D11_USAGE_DEFAULT; constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(g_device->CreateBuffer(&constant_desc, nullptr, &g_constant_buffer))) return false;
		std::vector<uint8_t> pixels; uint32_t texture_width = 0, texture_height = 0;
		if (!load_tga(pixels, texture_width, texture_height)) { if (g_log) g_log("VR HAND RENDERER: mechanic glove texture could not be decoded"); return false; }
		D3D11_TEXTURE2D_DESC texture_desc = {}; texture_desc.Width = texture_width; texture_desc.Height = texture_height; texture_desc.MipLevels = 1; texture_desc.ArraySize = 1;
		texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; texture_desc.SampleDesc.Count = 1; texture_desc.Usage = D3D11_USAGE_IMMUTABLE; texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA texture_data = {}; texture_data.pSysMem = pixels.data(); texture_data.SysMemPitch = texture_width * 4;
		ID3D11Texture2D *texture = nullptr;
		if (FAILED(g_device->CreateTexture2D(&texture_desc, &texture_data, &texture)) || FAILED(g_device->CreateShaderResourceView(texture, nullptr, &g_texture))) { release(texture); return false; }
		release(texture);
		D3D11_SAMPLER_DESC sampler = {}; sampler.Filter = D3D11_FILTER_ANISOTROPIC; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxAnisotropy = 8; sampler.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(g_device->CreateSamplerState(&sampler, &g_sampler))) return false;
		D3D11_RASTERIZER_DESC rasterizer = {}; rasterizer.FillMode = D3D11_FILL_SOLID; rasterizer.CullMode = D3D11_CULL_NONE; rasterizer.DepthClipEnable = TRUE;
		if (FAILED(g_device->CreateRasterizerState(&rasterizer, &g_rasterizer))) return false;
		D3D11_DEPTH_STENCIL_DESC depth = {}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(g_device->CreateDepthStencilState(&depth, &g_depth_state))) return false;
		g_initialized = true;
		if (!scrapvr::tools::initialize(g_device, g_log) && g_log)
			g_log("VR TOOL RENDERER: initialization failed; tracked hands remain available");
		if (g_log) g_log("VR HAND RENDERER READY: original mechanic default glove mesh and texture loaded for Touch and optical tracking");
		return true;
	}

	void set_pose(uint32_t hand, const XrPosef &pose, bool active, bool optical)
	{
		if (hand >= 2) return;
		TrackedPose &tracked = g_poses[hand];
		const ULONGLONG now = GetTickCount64();
		if (active)
		{
			tracked.pose = pose;
			if (!optical)
				tracked.pose.orientation = quaternion_multiply(
					pose.orientation, controller_calibration(hand));
			tracked.active = true;
			tracked.optical = optical;
			tracked.last_valid_ms = now;
		}
		else if (!tracked.active || tracked.last_valid_ms == 0 || now - tracked.last_valid_ms > 600)
		{
			// OpenXR can briefly omit a controller pose during hand/controller handoff.
			// Retain the last valid pose for a few frames so attached tools do not blink.
			tracked.active = false;
		}
	}

	void set_interaction(uint32_t hand, bool interaction)
	{
		if (hand >= 2) return;
		g_poses[hand].interaction = interaction;
	}

	void set_firing(uint32_t hand, bool firing)
	{
		if (hand >= 2) return;
		g_poses[hand].firing = firing;
	}

	bool get_pose(uint32_t hand, XrPosef &pose, bool &optical, bool &interaction)
	{
		if (hand >= 2 || !g_poses[hand].active) return false;
		pose = g_poses[hand].pose;
		optical = g_poses[hand].optical;
		interaction = g_poses[hand].interaction;
		return true;
	}

	void set_finger_curls(uint32_t hand, const float curls[5])
	{
		if (hand >= 2 || curls == nullptr) return;
		for (uint32_t finger = 0; finger < 5; ++finger) g_poses[hand].curls[finger] = curls[finger];
	}

	bool render(ID3D11DeviceContext *context, ID3D11RenderTargetView *target, uint32_t width, uint32_t height, const XrView &eye)
	{
		if (!g_initialized || !context || !target || (!g_poses[0].active && !g_poses[1].active) || !create_depth(width, height)) return false;
		context->ClearDepthStencilView(g_depth_view, D3D11_CLEAR_DEPTH, 1.0f, 0);
		context->OMSetRenderTargets(1, &target, g_depth_view);
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport); context->RSSetState(g_rasterizer); context->OMSetDepthStencilState(g_depth_state, 0);
		context->IASetInputLayout(g_input_layout); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(g_vertex_shader, nullptr, 0); context->VSSetConstantBuffers(0, 1, &g_constant_buffer);
		context->PSSetShader(g_pixel_shader, nullptr, 0); context->PSSetShaderResources(0, 1, &g_texture); context->PSSetSamplers(0, 1, &g_sampler);
		const Matrix view_projection = multiply(projection(eye.fov), inverse_pose(eye.pose));
		const uint32_t counts[] = { mechanic_hands_asset::left_vertex_count, mechanic_hands_asset::right_vertex_count };
		for (uint32_t hand = 0; hand < 2; ++hand)
		{
			if (!g_poses[hand].active) continue;
			Matrix model = pose_matrix(g_poses[hand].pose);
			Matrix palm_offset = identity(); palm_offset.m[14] = 0.1125f;
			HandConstants constants = {};
			constants.model = multiply(model, multiply(palm_offset, uniform_scale(1.12f)));
			constants.mvp = multiply(view_projection, constants.model);
			constants.eye_position[0] = eye.pose.position.x; constants.eye_position[1] = eye.pose.position.y;
			constants.eye_position[2] = eye.pose.position.z; constants.eye_position[3] = 1.0f;
			build_bones(hand, constants.bones);
			context->UpdateSubresource(g_constant_buffer, 0, nullptr, &constants, 0, 0);
			UINT stride = sizeof(Vertex), offset = 0; context->IASetVertexBuffers(0, 1, &g_vertex_buffers[hand], &stride, &offset);
			context->Draw(counts[hand], 0);
		}
		scrapvr::tools::render(context, target, g_depth_view, width, height, eye,
			g_poses[1].pose, g_poses[1].active, g_poses[1].firing);
		ID3D11ShaderResourceView *none = nullptr; context->PSSetShaderResources(0, 1, &none); context->OMSetRenderTargets(1, &target, nullptr);
		if (!g_render_logged && g_log) { g_render_logged = true; g_log("VISIBLE TRACKED HANDS ACTIVE: mechanic glove geometry rendered independently into both stereo eyes"); }
		return true;
	}

	void shutdown()
	{
		scrapvr::tools::shutdown();
		release(g_depth_view); release(g_depth_texture); release(g_depth_state); release(g_rasterizer); release(g_sampler); release(g_texture);
		release(g_input_layout); release(g_pixel_shader); release(g_vertex_shader); release(g_constant_buffer);
		for (auto &buffer : g_vertex_buffers) release(buffer);
		g_device = nullptr; g_log = nullptr; g_initialized = false; g_render_logged = false;
	}
}
