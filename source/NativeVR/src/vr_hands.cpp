#include "vr_hands.hpp"
#include "vr_tools.hpp"
#include "mechanic_hands_asset.hpp"

#include <algorithm>
#include <array>
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
		constexpr uint32_t kHandBoneCount = 28;
		static_assert(mechanic_hands_asset::left_bone_count == kHandBoneCount &&
			mechanic_hands_asset::right_bone_count == kHandBoneCount);
		struct Matrix { float m[16]; };
		struct TrackedPose
		{
			XrPosef pose = { { 0, 0, 0, 1 }, { 0, 0, 0 } };
			float curls[5] = {};
			float bends[5][3] = {};
			ULONGLONG last_valid_ms = 0;
			bool active = false;
			bool optical = false;
			bool interaction = false;
			bool firing = false;
			bool precise_fingers = false;
		};
		struct HandConstants { Matrix mvp; Matrix model; Matrix bones[kHandBoneCount]; float eye_position[4]; };
		struct HudVertex { float position[3]; float uv[2]; };
		struct HudConstants { Matrix mvp; };
		struct HudColor { uint8_t r, g, b, a; };
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
		ID3D11BlendState *g_opaque_blend_state = nullptr;
		ID3D11DepthStencilState *g_depth_state = nullptr;
		ID3D11Texture2D *g_depth_texture = nullptr;
		ID3D11DepthStencilView *g_depth_view = nullptr;
		uint32_t g_depth_width = 0, g_depth_height = 0;
		ID3D11Buffer *g_hud_vertex_buffer = nullptr;
		ID3D11Buffer *g_hud_constant_buffer = nullptr;
		ID3D11VertexShader *g_hud_vertex_shader = nullptr;
		ID3D11PixelShader *g_hud_pixel_shader = nullptr;
		ID3D11InputLayout *g_hud_input_layout = nullptr;
		ID3D11Texture2D *g_hud_texture_resource = nullptr;
		ID3D11ShaderResourceView *g_hud_texture = nullptr;
		ID3D11SamplerState *g_hud_sampler = nullptr;
		ID3D11BlendState *g_hud_blend_state = nullptr;
		ID3D11DepthStencilState *g_hud_depth_state = nullptr;
		std::vector<uint8_t> g_hud_pixels;
		ULONGLONG g_hud_last_update_ms = 0;
		float g_hud_last_heading = 100.0f;
		scrapvr::tools::WristHudState g_hud_last_state{};
		constexpr uint32_t kHudCurveSegments = 8;
		constexpr uint32_t kHudVerticesPerPanel = kHudCurveSegments * 6;
		// A slightly deeper bow keeps the card wrapped to the glove when viewed
		// from above, without introducing visible faceting or bending the text.
		constexpr float kHudCurveDepth = 0.023f;
		constexpr float kHudWidth = 0.144f;
		constexpr float kHudHeight = 0.072f;
		// Fixed smartwatch pose. Keep the small dorsal-wrist lift, then move the
		// display along the controller's local forward axis (negative local Z) so
		// it sits on the glove instead of floating behind the cuff. The slightly
		// overturned pitch tips its face away from the player. The same local pose
		// is used for both mirrored hands.
		constexpr float kHudWristOffsetX = 0.000f;
		constexpr float kHudWristOffsetY = 0.055f;
		constexpr float kHudWristOffsetZ = 0.065f;
		constexpr float kHudDefaultScale = 0.800f;
		constexpr float kHudDefaultPitchRadians = -1.83259571459404601923f; // -105 deg
		struct HudPoseConfig
		{
			float position_x;
			float position_y;
			float position_z;
			float pitch_radians;
			float scale;
		};
		constexpr HudPoseConfig kHudPose = {
			kHudWristOffsetX, kHudWristOffsetY, kHudWristOffsetZ,
			kHudDefaultPitchRadians, kHudDefaultScale
		};
		TrackedPose g_poses[2];
		bool g_initialized = false;
		bool g_render_logged = false;

		template <typename T> void release(T *&value) { if (value) { value->Release(); value = nullptr; } }

		struct ContextStateGuard
		{
			ID3D11DeviceContext *context = nullptr;
			ID3D11RenderTargetView *target = nullptr;
			ID3D11DepthStencilView *depth_view = nullptr;
			ID3D11BlendState *blend = nullptr;
			float blend_factor[4]{};
			UINT sample_mask = 0xffffffffu;
			ID3D11DepthStencilState *depth_state = nullptr;
			UINT stencil_reference = 0;
			ID3D11RasterizerState *rasterizer = nullptr;
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			ID3D11InputLayout *input_layout = nullptr;
			D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ID3D11Buffer *vertex_buffer = nullptr;
			UINT vertex_stride = 0;
			UINT vertex_offset = 0;
			ID3D11VertexShader *vertex_shader = nullptr;
			ID3D11GeometryShader *geometry_shader = nullptr;
			ID3D11HullShader *hull_shader = nullptr;
			ID3D11DomainShader *domain_shader = nullptr;
			ID3D11PixelShader *pixel_shader = nullptr;
			ID3D11Buffer *vertex_constant = nullptr;
			ID3D11ShaderResourceView *pixel_resource = nullptr;
			ID3D11SamplerState *pixel_sampler = nullptr;

			explicit ContextStateGuard(ID3D11DeviceContext *value) : context(value)
			{
				context->OMGetRenderTargets(1, &target, &depth_view);
				context->OMGetBlendState(&blend, blend_factor, &sample_mask);
				context->OMGetDepthStencilState(&depth_state, &stencil_reference);
				context->RSGetState(&rasterizer);
				context->RSGetViewports(&viewport_count, viewports);
				context->IAGetInputLayout(&input_layout);
				context->IAGetPrimitiveTopology(&topology);
				context->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
				context->VSGetShader(&vertex_shader, nullptr, nullptr);
				context->GSGetShader(&geometry_shader, nullptr, nullptr);
				context->HSGetShader(&hull_shader, nullptr, nullptr);
				context->DSGetShader(&domain_shader, nullptr, nullptr);
				context->PSGetShader(&pixel_shader, nullptr, nullptr);
				context->VSGetConstantBuffers(0, 1, &vertex_constant);
				context->PSGetShaderResources(0, 1, &pixel_resource);
				context->PSGetSamplers(0, 1, &pixel_sampler);
				context->GSSetShader(nullptr, nullptr, 0);
				context->HSSetShader(nullptr, nullptr, 0);
				context->DSSetShader(nullptr, nullptr, 0);
			}

			~ContextStateGuard()
			{
				context->OMSetRenderTargets(1, &target, depth_view);
				context->OMSetBlendState(blend, blend_factor, sample_mask);
				context->OMSetDepthStencilState(depth_state, stencil_reference);
				context->RSSetState(rasterizer);
				context->RSSetViewports(viewport_count, viewports);
				context->IASetInputLayout(input_layout);
				context->IASetPrimitiveTopology(topology);
				context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
				context->VSSetShader(vertex_shader, nullptr, 0);
				context->GSSetShader(geometry_shader, nullptr, 0);
				context->HSSetShader(hull_shader, nullptr, 0);
				context->DSSetShader(domain_shader, nullptr, 0);
				context->PSSetShader(pixel_shader, nullptr, 0);
				context->VSSetConstantBuffers(0, 1, &vertex_constant);
				context->PSSetShaderResources(0, 1, &pixel_resource);
				context->PSSetSamplers(0, 1, &pixel_sampler);
				release(pixel_sampler); release(pixel_resource); release(vertex_constant);
				release(pixel_shader); release(domain_shader); release(hull_shader);
				release(geometry_shader); release(vertex_shader); release(vertex_buffer);
				release(input_layout); release(rasterizer); release(depth_state);
				release(blend); release(depth_view); release(target);
			}
		};

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

		Matrix scale_xyz(float x, float y, float z)
		{
			Matrix result = identity();
			result.m[0] = x; result.m[5] = y; result.m[10] = z;
			return result;
		}

		void put_hud_pixel(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int x, int y, HudColor color)
		{
			if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(y) >= height ||
				color.a == 0) return;
			uint8_t *destination = pixels.data() + (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
			const float alpha = static_cast<float>(color.a) / 255.0f;
			const float inverse = 1.0f - alpha;
			destination[0] = static_cast<uint8_t>(std::clamp(alpha * color.r + inverse * destination[0], 0.0f, 255.0f));
			destination[1] = static_cast<uint8_t>(std::clamp(alpha * color.g + inverse * destination[1], 0.0f, 255.0f));
			destination[2] = static_cast<uint8_t>(std::clamp(alpha * color.b + inverse * destination[2], 0.0f, 255.0f));
			destination[3] = static_cast<uint8_t>(std::clamp(static_cast<float>(color.a) + inverse * destination[3], 0.0f, 255.0f));
		}

		void fill_hud_rect(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int left, int top, int right, int bottom, HudColor color)
		{
			for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
				for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
					put_hud_pixel(pixels, width, height, x, y, color);
		}

		void fill_hud_round_rect(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int left, int top, int right, int bottom, int radius, HudColor color)
		{
			if (right <= left || bottom <= top) return;
			radius = std::max(0, std::min(radius, std::min((right - left) / 2, (bottom - top) / 2)));
			const float radius_squared = static_cast<float>(radius * radius);
			for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
				for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
				{
					int cx = x, cy = y;
					if (x < left + radius) cx = left + radius;
					else if (x >= right - radius) cx = right - radius - 1;
					if (y < top + radius) cy = top + radius;
					else if (y >= bottom - radius) cy = bottom - radius - 1;
					const float dx = static_cast<float>(x - cx), dy = static_cast<float>(y - cy);
					if ((x >= left + radius && x < right - radius) ||
						(y >= top + radius && y < bottom - radius) || dx * dx + dy * dy <= radius_squared)
						put_hud_pixel(pixels, width, height, x, y, color);
				}
		}

		void draw_hud_line(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int x0, int y0, int x1, int y1, int thickness, HudColor color)
		{
			const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
			const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
			int error = dx + dy, x = x0, y = y0;
			const int radius = std::max(0, thickness / 2);
			for (;;)
			{
				fill_hud_rect(pixels, width, height, x - radius, y - radius,
					x + radius + 1, y + radius + 1, color);
				if (x == x1 && y == y1) break;
				const int twice = 2 * error;
				if (twice >= dy) { error += dy; x += sx; }
				if (twice <= dx) { error += dx; y += sy; }
			}
		}

		void draw_hud_diamond(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int center_x, int center_y, int radius, HudColor color)
		{
			radius = std::max(1, radius);
			for (int row = -radius; row <= radius; ++row)
			{
				const int half_width = radius - std::abs(row);
				fill_hud_rect(pixels, width, height,
					center_x - half_width, center_y + row,
					center_x + half_width + 1, center_y + row + 1, color);
			}
		}

		std::array<uint8_t, 7> hud_glyph(char value)
		{
			if (value >= 'a' && value <= 'z') value = static_cast<char>(value - 'a' + 'A');
			switch (value)
			{
			case 'A': return {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11};
			case 'B': return {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e};
			case 'C': return {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f};
			case 'D': return {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e};
			case 'E': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f};
			case 'F': return {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10};
			case 'G': return {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f};
			case 'H': return {0x11,0x11,0x11,0x1f,0x11,0x11,0x11};
			case 'I': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x1f};
			case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0e};
			case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
			case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1f};
			case 'M': return {0x11,0x1b,0x15,0x15,0x11,0x11,0x11};
			case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
			case 'O': return {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e};
			case 'P': return {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10};
			case 'Q': return {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d};
			case 'R': return {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11};
			case 'S': return {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e};
			case 'T': return {0x1f,0x04,0x04,0x04,0x04,0x04,0x04};
			case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0e};
			case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0a,0x04};
			case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1b,0x11};
			case 'X': return {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11};
			case 'Y': return {0x11,0x11,0x0a,0x04,0x04,0x04,0x04};
			case 'Z': return {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f};
			case '0': return {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e};
			case '1': return {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e};
			case '2': return {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f};
			case '3': return {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e};
			case '4': return {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02};
			case '5': return {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e};
			case '6': return {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e};
			case '7': return {0x1f,0x01,0x02,0x04,0x08,0x08,0x08};
			case '8': return {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e};
			case '9': return {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e};
			case ':': return {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
			case '.': return {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c};
			case '-': return {0x00,0x00,0x00,0x1f,0x00,0x00,0x00};
			case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
			case '%': return {0x19,0x19,0x02,0x04,0x08,0x13,0x13};
			default: return {0,0,0,0,0,0,0};
			}
		}

		int hud_text_width(const std::string &text, int scale, int spacing = 2)
		{
			if (text.empty()) return 0;
			return static_cast<int>(text.size()) * (5 * scale + spacing) - spacing;
		}

		void draw_hud_text(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int x, int y, const std::string &text, int scale, HudColor color, int spacing = 2)
		{
			if (scale <= 0) return;
			for (char value : text)
			{
				const std::array<uint8_t, 7> rows = hud_glyph(value);
				for (int row = 0; row < 7; ++row)
					for (int column = 0; column < 5; ++column)
						if (rows[static_cast<size_t>(row)] & (1u << (4 - column)))
							fill_hud_rect(pixels, width, height, x + column * scale,
								y + row * scale, x + (column + 1) * scale,
								y + (row + 1) * scale, color);
				x += 5 * scale + spacing;
			}
		}

		void draw_hud_text_center(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int center_x, int y, const std::string &text, int scale, HudColor color, int spacing = 2)
		{
			draw_hud_text(pixels, width, height, center_x - hud_text_width(text, scale, spacing) / 2,
				y, text, scale, color, spacing);
		}

		void draw_hud_text_right(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int right_x, int y, const std::string &text, int scale, HudColor color, int spacing = 2)
		{
			draw_hud_text(pixels, width, height, right_x - hud_text_width(text, scale, spacing),
				y, text, scale, color, spacing);
		}

		std::string hud_two_digits(uint32_t value)
		{
			value %= 100;
			std::string result(2, '0');
			result[0] = static_cast<char>('0' + value / 10);
			result[1] = static_cast<char>('0' + value % 10);
			return result;
		}

		std::string hud_clock(uint32_t minutes)
		{
			minutes %= 1440;
			return hud_two_digits(minutes / 60) + ":" + hud_two_digits(minutes % 60);
		}

		float hud_normalize_angle(float angle)
		{
			constexpr float two_pi = 6.28318530717958647692f;
			while (angle > 3.14159265358979323846f) angle -= two_pi;
			while (angle < -3.14159265358979323846f) angle += two_pi;
			return angle;
		}

		void draw_hud_heart(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
			int left, int top, int scale, HudColor color)
		{
			static constexpr uint8_t rows[] = { 0x0a, 0x1f, 0x1f, 0x1f, 0x0e, 0x04 };
			for (size_t row = 0; row < sizeof(rows); ++row)
				for (int column = 0; column < 5; ++column)
					if (rows[row] & (1u << (4 - column)))
						fill_hud_rect(pixels, width, height, left + column * scale,
							top + static_cast<int>(row) * scale,
							left + (column + 1) * scale, top + (static_cast<int>(row) + 1) * scale, color);
		}

		void draw_wrist_hud_bitmap(std::vector<uint8_t> &pixels,
			const scrapvr::tools::WristHudState &state, float heading)
		{
			constexpr uint32_t width = 512, height = 512;
			pixels.assign(static_cast<size_t>(width) * height * 4, 0);
			const uint32_t clock_minutes = state.time_minutes % 1440;
			const bool night_mode = state.active &&
				(clock_minutes < 360 || clock_minutes >= 1080);
			const HudColor outline = night_mode
				? HudColor{145, 150, 154, 215} : HudColor{255, 204, 63, 235};
			const HudColor panel = night_mode
				? HudColor{3, 8, 14, 224} : HudColor{10, 24, 36, 218};
			const HudColor white = night_mode
				? HudColor{196, 207, 211, 238} : HudColor{232, 241, 242, 245};
			const HudColor muted = night_mode
				? HudColor{116, 128, 134, 218} : HudColor{154, 174, 181, 225};
			const HudColor yellow = night_mode
				? HudColor{196, 166, 67, 245} : HudColor{255, 204, 63, 255};
			const HudColor oxygen_blue = night_mode
				? HudColor{55, 136, 202, 245} : HudColor{67, 174, 244, 255};

			// Each wrist is a compact single-face display. The transparent canvas
			// outside these two faces keeps the glove and scene visible around them.
			fill_hud_round_rect(pixels, width, height, 52, 38, 460, 214, 20, outline);
			fill_hud_round_rect(pixels, width, height, 57, 43, 455, 209, 16, panel);
			draw_hud_text(pixels, width, height, 73, 61, "HP", 2, white, 3);
			draw_hud_heart(pixels, width, height, 111, 61, 2, yellow);
			const bool has_values = state.active && state.max_health > 0.0f;
			const float ratio = has_values ? std::clamp(state.health / state.max_health, 0.0f, 1.0f) : 0.0f;
			std::string health_text = has_values
				? std::to_string(static_cast<int>(std::lround(std::max(0.0f, state.health)))) + "/" +
					std::to_string(static_cast<int>(std::lround(std::max(1.0f, state.max_health))))
				: "--/--";
			draw_hud_text_right(pixels, width, height, 438, 61, health_text, 2, white, 2);
			fill_hud_round_rect(pixels, width, height, 73, 99, 439, 119, 8, HudColor{3, 10, 16, 235});
			if (has_values)
			{
				const HudColor health_color = ratio <= 0.25f ? HudColor{231, 82, 67, 255} :
					ratio <= 0.55f ? HudColor{255, 180, 45, 255} : HudColor{95, 206, 119, 255};
				fill_hud_round_rect(pixels, width, height, 76, 102,
					76 + static_cast<int>(360.0f * ratio), 116, 6, health_color);
			}
			else
				fill_hud_round_rect(pixels, width, height, 76, 102, 436, 116, 6, HudColor{77, 98, 106, 180});

			// Survival exposes breath only while the player is submerged. Keep the
			// compact card unchanged at the surface, and insert an oxygen row directly
			// below HP as soon as breath starts falling. The blue fill mirrors the
			// game's oxygen bar and the numeric value remains readable in low light.
			const bool has_oxygen = state.active && state.max_breath > 0.0f;
			const bool oxygen_depleted = has_oxygen && state.breath < state.max_breath - 0.05f;
			int time_y = 149;
			if (oxygen_depleted)
			{
				draw_hud_text(pixels, width, height, 73, 128, "O2", 2, muted, 3);
				draw_hud_text_right(pixels, width, height, 438, 128,
					std::to_string(static_cast<int>(std::lround(std::max(0.0f, state.breath)))) + "/" +
					std::to_string(static_cast<int>(std::lround(std::max(1.0f, state.max_breath)))),
					2, white, 2);
				fill_hud_round_rect(pixels, width, height, 73, 153, 439, 173, 8,
					HudColor{3, 10, 16, 235});
				const float oxygen_ratio = std::clamp(state.breath / state.max_breath, 0.0f, 1.0f);
				if (oxygen_ratio > 0.0f)
					fill_hud_round_rect(pixels, width, height, 76, 156,
						76 + static_cast<int>(360.0f * oxygen_ratio), 170, 6, oxygen_blue);
				time_y = 184;
			}
			draw_hud_text(pixels, width, height, 73, time_y, "TIME", 2, muted, 3);
			draw_hud_text(pixels, width, height, 151, time_y - 3,
				state.active ? hud_clock(state.time_minutes) : "--:--", 2, yellow, 3);
			draw_hud_text_right(pixels, width, height, 438, time_y,
				state.active && state.conscious ? "OK" : "DOWN", 2,
				state.active && state.conscious ? muted : HudColor{231, 82, 67, 255}, 3);

			// Right wrist: a simple compass strip with one fixed index. The labels
			// slide underneath it as the player turns the in-game camera or headset;
			// pitch and roll are excluded from the world-space heading.
			fill_hud_round_rect(pixels, width, height, 52, 298, 460, 474, 20, outline);
			fill_hud_round_rect(pixels, width, height, 57, 303, 455, 469, 16, panel);
			draw_hud_text(pixels, width, height, 73, 321, "COMPASS", 2, white, 3);
			const int degrees = static_cast<int>(std::lround((heading < 0.0f ? heading + 6.28318530718f : heading) *
				180.0f / 3.14159265359f)) % 360;
			draw_hud_text_right(pixels, width, height, 438, 321,
				std::to_string(degrees) + " DEG", 2, muted, 2);
			fill_hud_round_rect(pixels, width, height, 73, 366, 439, 444, 10, HudColor{3, 10, 16, 235});
			fill_hud_rect(pixels, width, height, 79, 405, 433, 407, HudColor{67, 93, 102, 180});
			for (int tick = 0; tick < 24; ++tick)
			{
				const float world_angle = static_cast<float>(tick) * (3.14159265359f / 12.0f);
				const float relative = hud_normalize_angle(world_angle - heading);
				const int x = static_cast<int>(std::lround(256.0f + relative * 103.0f));
				if (x < 84 || x > 428) continue;
				const int tick_height = tick % 6 == 0 ? 19 : (tick % 3 == 0 ? 13 : 8);
				draw_hud_line(pixels, width, height, x, 405 - tick_height / 2,
					x, 405 + tick_height / 2, tick % 6 == 0 ? 3 : 2, muted);
			}
			static constexpr char cardinal[] = {'N', 'E', 'S', 'W'};
			for (int index = 0; index < 4; ++index)
			{
				const float world_angle = static_cast<float>(index) * (3.14159265359f / 2.0f);
				const float relative = hud_normalize_angle(world_angle - heading);
				const int x = static_cast<int>(std::lround(256.0f + relative * 103.0f));
				if (x < 90 || x > 422) continue;
				const std::string label(1, cardinal[index]);
				draw_hud_text_center(pixels, width, height, x, 416, label, 2,
					std::fabs(relative) < 0.18f ? yellow : white, 2);
			}
			// Mirror the game's live compass icons. The Lua bridge supplies a
			// world bearing and distance for every visible marker (quest/beacon,
			// raid/event, enemy, and lost-item markers). Clamp off-screen markers to
			// the strip edges just like the desktop compass rather than dropping them.
			int last_distance_label_x = -10000;
			uint32_t distance_labels = 0;
			const uint32_t waypoint_count = std::min<uint32_t>(
				state.waypoint_count, static_cast<uint32_t>(scrapvr::tools::kMaxWristHudWaypoints));
			for (uint32_t index = 0; index < waypoint_count; ++index)
			{
				const scrapvr::tools::WristHudWaypoint &waypoint = state.waypoints[index];
				if (!std::isfinite(waypoint.angle) || !std::isfinite(waypoint.distance) ||
					waypoint.distance < 0.0f) continue;
				const float relative = hud_normalize_angle(waypoint.angle - heading);
				const int raw_x = static_cast<int>(std::lround(256.0f + relative * 103.0f));
				const int x = std::clamp(raw_x, 86, 426);
				HudColor marker_color = yellow;
				switch (waypoint.kind)
				{
				case 2: marker_color = night_mode ? HudColor{213, 94, 84, 245} : HudColor{239, 92, 76, 255}; break;
				case 3: marker_color = night_mode ? HudColor{63, 151, 194, 245} : HudColor{67, 184, 232, 255}; break;
				case 4: marker_color = night_mode ? HudColor{157, 113, 190, 245} : HudColor{191, 136, 224, 255}; break;
				default: break;
				}
				draw_hud_diamond(pixels, width, height, x, 379, 9, HudColor{3, 10, 16, 240});
				draw_hud_diamond(pixels, width, height, x, 379, 6, marker_color);
				if (raw_x < 86)
					draw_hud_line(pixels, width, height, x + 6, 379, x + 1, 373, 2, marker_color);
				else if (raw_x > 426)
					draw_hud_line(pixels, width, height, x - 6, 379, x - 1, 373, 2, marker_color);
				if (distance_labels < 6 && x >= 101 && x <= 411 &&
					std::abs(x - last_distance_label_x) >= 30)
				{
					const uint32_t metres = static_cast<uint32_t>(std::clamp(
						std::lround(waypoint.distance), 0l, 99999l));
					draw_hud_text_center(pixels, width, height, x, 389,
						std::to_string(metres) + "M", 1, white, 1);
					last_distance_label_x = x;
					++distance_labels;
				}
			}
			// Fixed index and a small chevron keep the current heading unambiguous.
			draw_hud_line(pixels, width, height, 256, 374, 256, 437, 3, yellow);
			draw_hud_line(pixels, width, height, 250, 380, 256, 371, 3, yellow);
			draw_hud_line(pixels, width, height, 262, 380, 256, 371, 3, yellow);
		}

		bool hud_waypoints_changed(const scrapvr::tools::WristHudState &left,
			const scrapvr::tools::WristHudState &right)
		{
			if (left.waypoint_count != right.waypoint_count) return true;
			const uint32_t count = std::min<uint32_t>(left.waypoint_count,
				static_cast<uint32_t>(scrapvr::tools::kMaxWristHudWaypoints));
			for (uint32_t index = 0; index < count; ++index)
			{
				const auto &a = left.waypoints[index];
				const auto &b = right.waypoints[index];
				if (a.kind != b.kind || std::fabs(a.angle - b.angle) >= 0.004f ||
					std::fabs(a.distance - b.distance) >= 0.25f) return true;
			}
			return false;
		}

		void update_hud_texture(ID3D11DeviceContext *context,
			const scrapvr::tools::WristHudState &state, float heading)
		{
			if (!context || !g_hud_texture_resource) return;
			const ULONGLONG now = GetTickCount64();
			const bool state_changed = state.active != g_hud_last_state.active ||
				state.conscious != g_hud_last_state.conscious ||
				std::fabs(state.health - g_hud_last_state.health) >= 0.05f ||
				std::fabs(state.max_health - g_hud_last_state.max_health) >= 0.05f ||
				std::fabs(state.breath - g_hud_last_state.breath) >= 0.05f ||
				std::fabs(state.max_breath - g_hud_last_state.max_breath) >= 0.05f ||
				state.time_minutes != g_hud_last_state.time_minutes ||
				hud_waypoints_changed(state, g_hud_last_state);
			// The compass is intentionally refreshed at most 20 Hz. This keeps the
			// VR render thread free of per-frame CPU bitmap work while remaining
			// visually smooth on a moving wrist; vitals still update immediately.
			if (!state_changed && now - g_hud_last_update_ms < 50) return;
			if (!state_changed && std::fabs(heading - g_hud_last_heading) < 0.01f &&
				now - g_hud_last_update_ms < 250) return;
			draw_wrist_hud_bitmap(g_hud_pixels, state, heading);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(g_hud_texture_resource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				for (uint32_t row = 0; row < 512; ++row)
					std::memcpy(static_cast<uint8_t *>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
						g_hud_pixels.data() + static_cast<size_t>(row) * 512 * 4, 512 * 4);
				context->Unmap(g_hud_texture_resource, 0);
				g_hud_last_update_ms = now;
				g_hud_last_heading = heading;
				g_hud_last_state = state;
			}
		}

		void render_wrist_hud(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
			ID3D11DepthStencilView *depth, uint32_t width, uint32_t height,
			const XrView &eye, float world_heading)
		{
			if (!context || !target || !depth || !g_hud_texture || !g_hud_vertex_buffer ||
				!g_hud_constant_buffer || !g_hud_vertex_shader || !g_hud_pixel_shader ||
				!g_hud_input_layout || !g_hud_sampler || !g_hud_blend_state || !g_hud_depth_state)
				return;
			const scrapvr::tools::WristHudState state = scrapvr::tools::wrist_hud_state();
			if (!state.active) return;
			// Compass labels use the engine's world-space camera heading combined with
			// the horizontal headset rotation relative to the recentered view. Pitch
			// and roll are discarded, so looking up/down or tilting the wrist cannot
			// change the displayed world direction.
			const float heading = std::isfinite(world_heading) ? world_heading : 0.0f;
			update_hud_texture(context, state, heading);
			context->OMSetRenderTargets(1, &target, depth);
			const float blend_factor[4] = {};
			context->OMSetBlendState(g_hud_blend_state, blend_factor, 0xffffffffu);
			D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(width),
				static_cast<float>(height), 0.0f, 1.0f};
			context->RSSetViewports(1, &viewport);
			context->RSSetState(g_rasterizer);
			context->OMSetDepthStencilState(g_hud_depth_state, 0);
			context->IASetInputLayout(g_hud_input_layout);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(g_hud_vertex_shader, nullptr, 0);
			context->VSSetConstantBuffers(0, 1, &g_hud_constant_buffer);
			context->PSSetShader(g_hud_pixel_shader, nullptr, 0);
			context->PSSetShaderResources(0, 1, &g_hud_texture);
			context->PSSetSamplers(0, 1, &g_hud_sampler);
			const Matrix view_projection = multiply(projection(eye.fov), inverse_pose(eye.pose));
			UINT stride = sizeof(HudVertex), offset = 0;
			context->IASetVertexBuffers(0, 1, &g_hud_vertex_buffer, &stride, &offset);
			for (uint32_t hand = 0; hand < 2; ++hand)
			{
				if (!g_poses[hand].active) continue;
				// The compact card is fixed to the dorsal wrist like a smartwatch.
				// Both hands use this mirrored controller-local pose; local -Z follows
				// the fingers, keeping the full card surface on the glove when extended.
				const HudPoseConfig &pose = kHudPose;
				const Matrix local = multiply(
					translation(pose.position_x, pose.position_y, pose.position_z),
					multiply(rotation_x(pose.pitch_radians),
						scale_xyz(kHudWidth * pose.scale, kHudHeight * pose.scale, pose.scale)));
				const Matrix model = multiply(pose_matrix(g_poses[hand].pose), local);
				HudConstants constants = {multiply(view_projection, model)};
				context->UpdateSubresource(g_hud_constant_buffer, 0, nullptr, &constants, 0, 0);
				context->Draw(kHudVerticesPerPanel, hand == 0 ? 0 : kHudVerticesPerPanel);
			}
			ID3D11ShaderResourceView *none = nullptr;
			context->PSSetShaderResources(0, 1, &none);
		}

		float finger_joint_angle(const TrackedPose &pose, int finger, int segment)
		{
			if (finger == 0)
			{
				const float curl = std::clamp(pose.curls[0], 0.0f, 1.0f);
				return -curl * 0.26f + (1.0f - curl) * 0.10f;
			}
			const int joint = std::clamp(segment - 1, 0, 2);
			const float normalized_curl = std::clamp(pose.curls[finger], 0.0f, 1.0f);
			const float tracked_bend = pose.precise_fingers
				? std::clamp(pose.bends[finger][joint], 0.0f, 1.65f)
				: normalized_curl * 0.44f;
			const float bend = tracked_bend * (pose.precise_fingers ? 0.74f : 1.0f);
			// The source glove bind pose is slightly curled. Apply a fading
			// extension bias to the four fingers so a physically open Quest hand
			// reaches a visibly straight pose without overextending a closed hand.
			constexpr float extension[3] = {0.28f, 0.20f, 0.13f};
			const float openness = 1.0f - std::clamp(bend / 0.65f, 0.0f, 1.0f);
			return -bend + extension[joint] * openness;
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
			// The OpenXR grip-pose axes are defined consistently for held motion
			// controllers, including Touch and Index. Optical palm tracking bypasses
			// this controller-to-glove mesh transform.
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

		void build_bones(uint32_t hand, Matrix output[kHandBoneCount])
		{
			const auto *pivots = hand == 0
				? mechanic_hands_asset::left_bone_pivots
				: mechanic_hands_asset::right_bone_pivots;
			for (uint32_t bone = 0; bone < kHandBoneCount; ++bone)
			{
				output[bone] = identity();
				const int finger = pivots[bone].finger;
				const int last_segment = pivots[bone].segment > 3 ? 3 : pivots[bone].segment;
				if (finger < 0 || last_segment <= 0)
					continue;
				for (int segment = 1; segment <= last_segment; ++segment)
					for (uint32_t pivot_bone = 0; pivot_bone < kHandBoneCount; ++pivot_bone)
						if (pivots[pivot_bone].finger == finger && pivots[pivot_bone].segment == segment)
						{
							output[bone] = multiply(output[bone], rotate_about(
								pivots[pivot_bone], finger_joint_angle(g_poses[hand], finger, segment)));
							break;
						}
			}
		}

		std::wstring texture_path()
		{
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"smvr_native_vr_v1.addon64");
			if (!module || GetModuleFileNameW(module, module_path, MAX_PATH) == 0)
				return {};
			std::wstring path(module_path);
			const size_t slash = path.find_last_of(L"\\/");
			if (slash == std::wstring::npos)
				return {};
			path.resize(slash);
			// The embedded hand mesh is generated from the ship-mechanic glove DAE,
			// so its UVs must use the matching ship-mechanic texture atlas.
			path += L"\\..\\Survival\\Character\\Char_Shipmechanic\\char_shipmechanic_gloves_dif.tga";
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

		uint32_t read_u32(const uint8_t *value)
		{
			return static_cast<uint32_t>(value[0]) |
				(static_cast<uint32_t>(value[1]) << 8) |
				(static_cast<uint32_t>(value[2]) << 16) |
				(static_cast<uint32_t>(value[3]) << 24);
		}

		bool decode_lz4_block(const uint8_t *source, size_t source_size,
			uint8_t *destination, size_t destination_size)
		{
			const uint8_t *input = source;
			const uint8_t *const input_end = source + source_size;
			uint8_t *output = destination;
			uint8_t *const output_end = destination + destination_size;
			while (input < input_end)
			{
				const uint8_t token = *input++;
				size_t literal_length = token >> 4;
				if (literal_length == 15)
				{
					uint8_t extension = 255;
					while (extension == 255)
					{
						if (input >= input_end) return false;
						extension = *input++;
						literal_length += extension;
					}
				}
				if (literal_length > static_cast<size_t>(input_end - input) ||
					literal_length > static_cast<size_t>(output_end - output)) return false;
				std::memcpy(output, input, literal_length);
				input += literal_length;
				output += literal_length;
				if (input == input_end) break;
				if (input_end - input < 2) return false;
				const size_t offset = static_cast<size_t>(input[0]) |
					(static_cast<size_t>(input[1]) << 8);
				input += 2;
				if (offset == 0 || offset > static_cast<size_t>(output - destination)) return false;
				size_t match_length = token & 0x0f;
				if (match_length == 15)
				{
					uint8_t extension = 255;
					while (extension == 255)
					{
						if (input >= input_end) return false;
						extension = *input++;
						match_length += extension;
					}
				}
				match_length += 4;
				if (match_length > static_cast<size_t>(output_end - output)) return false;
				const uint8_t *match = output - offset;
				for (size_t i = 0; i < match_length; ++i) output[i] = match[i];
				output += match_length;
			}
			return input == input_end && output == output_end;
		}

		std::wstring compiled_texture_path()
		{
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"smvr_native_vr_v1.addon64");
			if (!module || GetModuleFileNameW(module, module_path, MAX_PATH) == 0) return {};
			std::wstring path(module_path);
			for (uint32_t i = 0; i < 2; ++i)
			{
				const size_t slash = path.find_last_of(L"\\/");
				if (slash == std::wstring::npos) return {};
				path.resize(slash);
			}
			path += L"\\Cache\\Textures\\char_shipmechanic_gloves_dif_*.tco";
			WIN32_FIND_DATAW found{};
			HANDLE search = FindFirstFileW(path.c_str(), &found);
			if (search == INVALID_HANDLE_VALUE) return {};
			FindClose(search);
			path.resize(path.find_last_of(L"\\/") + 1);
			path += found.cFileName;
			return path;
		}

		bool create_compiled_glove_texture(ID3D11ShaderResourceView **output)
		{
			if (!output) return false;
			const std::wstring path = compiled_texture_path();
			std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
			if (!file) return false;
			const std::streamsize file_size = file.tellg();
			if (file_size < 48) return false;
			file.seekg(0, std::ios::beg);
			std::vector<uint8_t> encoded(static_cast<size_t>(file_size));
			file.read(reinterpret_cast<char *>(encoded.data()), file_size);
			if (!file) return false;

			const uint32_t version = read_u32(encoded.data());
			const uint32_t compressed_size = read_u32(encoded.data() + 0x10);
			const uint32_t decoded_size = read_u32(encoded.data() + 0x14);
			const uint32_t texture_type = read_u32(encoded.data() + 0x18);
			const uint32_t width = read_u32(encoded.data() + 0x1c);
			const uint32_t height = read_u32(encoded.data() + 0x20);
			const uint32_t mip_count = read_u32(encoded.data() + 0x28);
			if (version != 4 || texture_type != 2 || width == 0 || height == 0 ||
				mip_count == 0 || mip_count > 16 || compressed_size != encoded.size() - 48)
				return false;

			size_t expected_size = 0;
			uint32_t mip_width = width, mip_height = height;
			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				expected_size += static_cast<size_t>((mip_width + 3) / 4) *
					static_cast<size_t>((mip_height + 3) / 4) * 16;
				mip_width = std::max(1u, mip_width / 2);
				mip_height = std::max(1u, mip_height / 2);
			}
			if (decoded_size != expected_size) return false;
			std::vector<uint8_t> decoded(decoded_size);
			if (!decode_lz4_block(encoded.data() + 48, compressed_size,
				decoded.data(), decoded.size())) return false;

			std::vector<D3D11_SUBRESOURCE_DATA> subresources(mip_count);
			size_t cursor = 0;
			mip_width = width; mip_height = height;
			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				const uint32_t pitch = ((mip_width + 3) / 4) * 16;
				const uint32_t rows = (mip_height + 3) / 4;
				subresources[mip].pSysMem = decoded.data() + cursor;
				subresources[mip].SysMemPitch = pitch;
				subresources[mip].SysMemSlicePitch = pitch * rows;
				cursor += static_cast<size_t>(pitch) * rows;
				mip_width = std::max(1u, mip_width / 2);
				mip_height = std::max(1u, mip_height / 2);
			}

			D3D11_TEXTURE2D_DESC description{};
			description.Width = width;
			description.Height = height;
			description.MipLevels = mip_count;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			ID3D11Texture2D *texture = nullptr;
			HRESULT result = g_device->CreateTexture2D(&description, subresources.data(), &texture);
			if (SUCCEEDED(result)) result = g_device->CreateShaderResourceView(texture, nullptr, output);
			release(texture);
			if (SUCCEEDED(result) && g_log)
				g_log("VR HAND TEXTURE: loaded Chapter 2 compiled default glove size=%ux%u mips=%u format=BC3_SRGB",
					width, height, mip_count);
			return SUCCEEDED(result);
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
			cbuffer HandConstants : register(b0) { float4x4 mvp; float4x4 model; float4x4 bones[28]; float4 eye_position; };
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
				float sky = 0.5 + 0.5 * n.y; float ambient = lerp(0.32, 0.48, sky);
				float key = 0.38 * saturate(dot(n, key_direction));
				float fill = 0.06 * saturate(dot(n, normalize(float3(0.65, 0.25, 0.72))));
				float3 view_direction = normalize(eye_position.xyz - input.world_position);
				float3 half_vector = normalize(key_direction + view_direction);
				float specular = 0.035 * pow(saturate(dot(n, half_vector)), 28.0);
				float3 linear_lit = color.rgb * (ambient + key + fill) + specular;
				// The OpenXR RTV is R8G8B8A8_UNORM_SRGB. Return linear light and let
				// the target perform the one required transfer; manual gamma here was
				// the cause of the pale, bright hand/tool overlays.
				return float4(saturate((linear_lit - 0.18) * 1.08 + 0.16), 1.0);
			}
		)";
		const char *hud_shader = R"(
			cbuffer HudConstants : register(b0) { float4x4 mvp; };
			struct VSIn { float3 position : POSITION; float2 uv : TEXCOORD0; };
			struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
			VSOut vs_hud(VSIn input) {
				VSOut output; output.position = mul(mvp, float4(input.position, 1.0));
				output.uv = input.uv; return output;
			}
			Texture2D hud_texture : register(t0); SamplerState hud_sampler : register(s0);
			float4 ps_hud(VSOut input) : SV_TARGET { return hud_texture.Sample(hud_sampler, input.uv); }
		)";
		HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
		using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
		auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
		ID3DBlob *vs_blob = nullptr, *ps_blob = nullptr, *hud_vs_blob = nullptr, *hud_ps_blob = nullptr, *errors = nullptr;
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
		release(errors);
		if (FAILED(compile(hud_shader, std::strlen(hud_shader), "vr_wrist_hud", nullptr, nullptr,
			"vs_hud", "vs_5_0", 0, 0, &hud_vs_blob, &errors)))
		{
			if (g_log) g_log("VR WRIST HUD: vertex shader compilation failed%s%s", errors ? ": " : "",
				errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
			release(errors); release(vs_blob); release(ps_blob); if (compiler) FreeLibrary(compiler); return false;
		}
		release(errors);
		if (FAILED(compile(hud_shader, std::strlen(hud_shader), "vr_wrist_hud", nullptr, nullptr,
			"ps_hud", "ps_5_0", 0, 0, &hud_ps_blob, &errors)))
		{
			if (g_log) g_log("VR WRIST HUD: pixel shader compilation failed%s%s", errors ? ": " : "",
				errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
			release(errors); release(vs_blob); release(ps_blob); release(hud_vs_blob);
			if (compiler) FreeLibrary(compiler); return false;
		}
		if (compiler) FreeLibrary(compiler);
		if (FAILED(g_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vertex_shader)) ||
			FAILED(g_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_pixel_shader)) ||
			FAILED(g_device->CreateVertexShader(hud_vs_blob->GetBufferPointer(), hud_vs_blob->GetBufferSize(), nullptr, &g_hud_vertex_shader)) ||
			FAILED(g_device->CreatePixelShader(hud_ps_blob->GetBufferPointer(), hud_ps_blob->GetBufferSize(), nullptr, &g_hud_pixel_shader)))
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
		D3D11_INPUT_ELEMENT_DESC hud_elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		if (FAILED(g_device->CreateInputLayout(hud_elements, 2, hud_vs_blob->GetBufferPointer(),
			hud_vs_blob->GetBufferSize(), &g_hud_input_layout))) return false;
		release(vs_blob); release(ps_blob); release(hud_vs_blob); release(hud_ps_blob); release(errors);
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
		std::array<HudVertex, kHudVerticesPerPanel * 2> hud_vertices{};
		for (uint32_t panel = 0; panel < 2; ++panel)
		{
			const float v_top = panel == 0 ? 0.0f : 0.5f;
			const float v_bottom = panel == 0 ? 0.5f : 1.0f;
			for (uint32_t segment = 0; segment < kHudCurveSegments; ++segment)
			{
				const float u0 = static_cast<float>(segment) / static_cast<float>(kHudCurveSegments);
				const float u1 = static_cast<float>(segment + 1) / static_cast<float>(kHudCurveSegments);
				const float x0 = -0.5f + u0;
				const float x1 = -0.5f + u1;
				const float z0 = kHudCurveDepth * (1.0f - 4.0f * x0 * x0);
				const float z1 = kHudCurveDepth * (1.0f - 4.0f * x1 * x1);
				const size_t base = static_cast<size_t>(panel) * kHudVerticesPerPanel +
					static_cast<size_t>(segment) * 6;
				hud_vertices[base + 0] = {{x0, -0.5f, z0}, {u0, v_bottom}};
				hud_vertices[base + 1] = {{x1, -0.5f, z1}, {u1, v_bottom}};
				hud_vertices[base + 2] = {{x1, 0.5f, z1}, {u1, v_top}};
				hud_vertices[base + 3] = {{x0, -0.5f, z0}, {u0, v_bottom}};
				hud_vertices[base + 4] = {{x1, 0.5f, z1}, {u1, v_top}};
				hud_vertices[base + 5] = {{x0, 0.5f, z0}, {u0, v_top}};
			}
		}
		D3D11_BUFFER_DESC hud_vertex_desc = {};
		hud_vertex_desc.ByteWidth = static_cast<UINT>(sizeof(hud_vertices));
		hud_vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
		hud_vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA hud_vertex_data = {}; hud_vertex_data.pSysMem = hud_vertices.data();
		if (FAILED(g_device->CreateBuffer(&hud_vertex_desc, &hud_vertex_data, &g_hud_vertex_buffer))) return false;
		D3D11_BUFFER_DESC hud_constant_desc = {};
		hud_constant_desc.ByteWidth = sizeof(HudConstants);
		hud_constant_desc.Usage = D3D11_USAGE_DEFAULT;
		hud_constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(g_device->CreateBuffer(&hud_constant_desc, nullptr, &g_hud_constant_buffer))) return false;
		g_hud_pixels.assign(static_cast<size_t>(512) * 512 * 4, 0);
		draw_wrist_hud_bitmap(g_hud_pixels, scrapvr::tools::WristHudState{}, 0.0f);
		D3D11_TEXTURE2D_DESC hud_texture_desc = {};
		hud_texture_desc.Width = 512; hud_texture_desc.Height = 512;
		hud_texture_desc.MipLevels = 1; hud_texture_desc.ArraySize = 1;
		hud_texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		hud_texture_desc.SampleDesc.Count = 1;
		hud_texture_desc.Usage = D3D11_USAGE_DYNAMIC;
		hud_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		hud_texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SUBRESOURCE_DATA hud_texture_data = {};
		hud_texture_data.pSysMem = g_hud_pixels.data(); hud_texture_data.SysMemPitch = 512 * 4;
		if (FAILED(g_device->CreateTexture2D(&hud_texture_desc, &hud_texture_data, &g_hud_texture_resource)) ||
			FAILED(g_device->CreateShaderResourceView(g_hud_texture_resource, nullptr, &g_hud_texture))) return false;
		D3D11_SAMPLER_DESC hud_sampler_desc = {};
		hud_sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		hud_sampler_desc.AddressU = hud_sampler_desc.AddressV = hud_sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		hud_sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(g_device->CreateSamplerState(&hud_sampler_desc, &g_hud_sampler))) return false;
		D3D11_BLEND_DESC hud_blend_desc = {};
		hud_blend_desc.RenderTarget[0].BlendEnable = TRUE;
		// put_hud_pixel builds a premultiplied-alpha bitmap while layering the
		// panel, text, and markers. Use ONE here so translucent pixels are not
		// multiplied by alpha a second time when composited into the eye target.
		hud_blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		hud_blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		hud_blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		hud_blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		hud_blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		hud_blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		hud_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(g_device->CreateBlendState(&hud_blend_desc, &g_hud_blend_state))) return false;
		D3D11_DEPTH_STENCIL_DESC hud_depth_desc = {};
		hud_depth_desc.DepthEnable = TRUE;
		hud_depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		hud_depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(g_device->CreateDepthStencilState(&hud_depth_desc, &g_hud_depth_state))) return false;
		std::vector<uint8_t> pixels; uint32_t texture_width = 0, texture_height = 0;
		if (load_tga(pixels, texture_width, texture_height))
		{
			D3D11_TEXTURE2D_DESC texture_desc = {}; texture_desc.Width = texture_width; texture_desc.Height = texture_height; texture_desc.MipLevels = 1; texture_desc.ArraySize = 1;
			texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; texture_desc.SampleDesc.Count = 1; texture_desc.Usage = D3D11_USAGE_IMMUTABLE; texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA texture_data = {}; texture_data.pSysMem = pixels.data(); texture_data.SysMemPitch = texture_width * 4;
			ID3D11Texture2D *texture = nullptr;
			if (FAILED(g_device->CreateTexture2D(&texture_desc, &texture_data, &texture)) || FAILED(g_device->CreateShaderResourceView(texture, nullptr, &g_texture))) { release(texture); return false; }
			release(texture);
			if (g_log) g_log("VR HAND TEXTURE: loaded loose default glove size=%ux%u format=RGBA8_SRGB", texture_width, texture_height);
		}
		else if (!create_compiled_glove_texture(&g_texture))
		{
			if (g_log) g_log("VR HAND RENDERER: Chapter 2 default glove texture could not be decoded; hand pass disabled instead of drawing an untextured fallback");
			return false;
		}
		D3D11_SAMPLER_DESC sampler = {}; sampler.Filter = D3D11_FILTER_ANISOTROPIC; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxAnisotropy = 8; sampler.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(g_device->CreateSamplerState(&sampler, &g_sampler))) return false;
		D3D11_RASTERIZER_DESC rasterizer = {}; rasterizer.FillMode = D3D11_FILL_SOLID; rasterizer.CullMode = D3D11_CULL_NONE; rasterizer.DepthClipEnable = TRUE;
		if (FAILED(g_device->CreateRasterizerState(&rasterizer, &g_rasterizer))) return false;
		D3D11_BLEND_DESC blend = {};
		blend.RenderTarget[0].BlendEnable = FALSE;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(g_device->CreateBlendState(&blend, &g_opaque_blend_state))) return false;
		D3D11_DEPTH_STENCIL_DESC depth = {}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(g_device->CreateDepthStencilState(&depth, &g_depth_state))) return false;
		g_initialized = true;
		if (!scrapvr::tools::initialize(g_device, g_log) && g_log)
			g_log("VR TOOL RENDERER: initialization failed; tracked hands remain available");
		if (g_log) g_log("VR HAND RENDERER READY: Scrap Mechanic 1.0 ship-mechanic glove mesh and matching texture loaded for OpenXR controllers and optical tracking");
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

	void set_finger_articulation(
		uint32_t hand, const float curls[5], const float bends[5][3], bool precise)
	{
		if (hand >= 2 || curls == nullptr) return;
		for (uint32_t finger = 0; finger < 5; ++finger) g_poses[hand].curls[finger] = curls[finger];
		if (bends != nullptr)
			for (uint32_t finger = 0; finger < 5; ++finger)
				for (uint32_t joint = 0; joint < 3; ++joint)
					g_poses[hand].bends[finger][joint] = bends[finger][joint];
		g_poses[hand].precise_fingers = precise;
	}

	bool render(ID3D11DeviceContext *context, ID3D11RenderTargetView *target, uint32_t width,
			uint32_t height, const XrView &eye, const XrPosef &right_aim_pose,
			bool right_aim_active, float right_target_distance, bool right_target_active,
			float interaction_target_distance, bool interaction_target_active,
			float world_heading)
	{
		if (!g_initialized || !context || !target || (!g_poses[0].active && !g_poses[1].active) || !create_depth(width, height)) return false;
		ContextStateGuard preserved_state(context);
		context->ClearDepthStencilView(g_depth_view, D3D11_CLEAR_DEPTH, 1.0f, 0);
		context->OMSetRenderTargets(1, &target, g_depth_view);
		const float blend_factor[4] = {};
		context->OMSetBlendState(g_opaque_blend_state, blend_factor, 0xffffffffu);
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
			g_poses[1].pose, g_poses[1].active, g_poses[1].firing,
			right_aim_pose, right_aim_active, right_target_distance,
			right_target_active, interaction_target_distance,
			interaction_target_active);
		render_wrist_hud(context, target, g_depth_view, width, height, eye, world_heading);
		ID3D11ShaderResourceView *none = nullptr; context->PSSetShaderResources(0, 1, &none); context->OMSetRenderTargets(1, &target, nullptr);
		if (!g_render_logged && g_log) { g_render_logged = true; g_log("VISIBLE TRACKED HANDS ACTIVE: mechanic glove geometry rendered independently into both stereo eyes"); }
		return true;
	}

	void shutdown()
	{
		scrapvr::tools::shutdown();
		release(g_hud_depth_state); release(g_hud_blend_state); release(g_hud_sampler);
		release(g_hud_texture); release(g_hud_texture_resource); release(g_hud_input_layout);
		release(g_hud_pixel_shader); release(g_hud_vertex_shader);
		release(g_hud_constant_buffer); release(g_hud_vertex_buffer);
		release(g_depth_view); release(g_depth_texture); release(g_depth_state); release(g_opaque_blend_state); release(g_rasterizer); release(g_sampler); release(g_texture);
		release(g_input_layout); release(g_pixel_shader); release(g_vertex_shader); release(g_constant_buffer);
		for (auto &buffer : g_vertex_buffers) release(buffer);
		g_device = nullptr; g_log = nullptr; g_initialized = false; g_render_logged = false;
		g_hud_pixels.clear(); g_hud_last_update_ms = 0; g_hud_last_heading = 100.0f;
		g_hud_last_state = scrapvr::tools::WristHudState{};
	}
}
