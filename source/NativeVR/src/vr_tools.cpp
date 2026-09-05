#include "vr_tools.hpp"
#include "custom_content_bridge.hpp"
#include "native_tool_asset.hpp"
#include "chapter2_tool_asset.hpp"
#include "held_item_asset.hpp"
#include "held_item_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace scrapvr::tools
	{
		namespace
	{
		using Vertex = native_tool_asset::Vertex;
		#pragma pack(push, 1)
		struct PackedCatalogVertex
		{
			float position[3];
			int16_t normal[3];
			uint16_t uv[2];
		};
		#pragma pack(pop)
		static_assert(sizeof(PackedCatalogVertex) == 22);
		static_assert(sizeof(PackedCatalogVertex) == held_item_catalog::packed_vertex_size);
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
		enum class Tool
		{
			none, hammer, connect, paint, weld, rifle, shotgun, gatling, scrap, launcher, clay,
			lift, handbook, bucket, glowstick, cornade, loose_clay, extinguisher, planter,
			fertilizer, food, feeder, soilbag, key, resource, carry, logbook, catalog, count
		};
		enum class HeldProfile
		{
			hammer, connect, paint, weld, rifle, shotgun, gatling, scrap, launcher, clay,
			lift, handbook, bucket, glowstick, cornade, loose_clay, extinguisher, planter,
			fertilizer, food, feeder, soilbag, key, powercore, resource, carry, logbook,
			blocks, wedges, small_parts, medium_parts, large_parts, consumables, resources,
			components, plantables, quest_items, other_parts, count
		};
		enum class ItemVariant
		{
			none,
			bucket_empty, bucket_water, bucket_oil, bucket_chemical,
			food_sunshake, food_milk, food_carrotburger, food_pizzaburger,
			food_banana, food_blueberry, food_orange, food_pineapple, food_carrot,
			food_redbeet, food_tomato, food_broccoli, food_corn, food_tea, food_chili,
			keycard, powercore
		};
		enum DrawId
		{
			hammer_mesh, connect_mesh, paint_body, paint_can, weld_mesh,
			gun_grip, gun_body, gun_sight_screw, gun_sight, gun_stock, gun_tank,
			rifle_barrel, shotgun_barrel, shotgun_oil, gatling_barrel,
			scrap_barrel, launcher_barrel,
			clay_body, clay_wheel, clay_container_fill, clay_container_glass, clay_grip, draw_count
		};
		struct DrawResource
		{
			ID3D11Buffer *vertices = nullptr;
			ID3D11ShaderResourceView *texture = nullptr;
			uint32_t count = 0;
			held_item_catalog::Material material = held_item_catalog::Material::opaque;
		};
		struct ToolCalibration
		{
			float tool_x = 0.0f, tool_y = -0.035f, tool_z = -0.045f;
			float laser_x = 0.0f, laser_y = 0.0f, laser_z = -0.300f;
		};
		struct PoseCalibration
		{
			float x = 0.0f, y = -0.035f, z = -0.065f;
			float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
			float scale = 0.16f;
		};
		struct ClayCalibration
		{
			float tool_x = -0.122f, tool_y = -0.031f, tool_z = -0.172f;
			float tool_pitch = 0.0f, tool_yaw = 0.0f, tool_roll = 0.0f;
			float scale = 0.145f;
			float container_pivot_x = 0.040000f, container_pivot_y = 0.510000f, container_pivot_z = 0.160580f;
			float container_axis_x = 0.0f, container_axis_y = 0.0f, container_axis_z = 1.0f;
			float container_speed = 1.0f, container_phase = 0.0f;
			float wheel_pivot_x = -0.010000f, wheel_pivot_y = 0.239770f, wheel_pivot_z = 1.416530f;
			float wheel_axis_x = 1.0f, wheel_axis_y = 0.0f, wheel_axis_z = 0.0f;
			float wheel_speed = 1.0f, wheel_phase = 0.0f;
		};

		ID3D11Device *g_device = nullptr;
		LogFunction g_log = nullptr;
		DrawResource g_draws[draw_count];
		DrawResource g_held_draws[held_item_asset::mesh_count];
		std::vector<DrawResource> g_catalog_draws;
		ID3D11Buffer *g_constant_buffer = nullptr;
		ID3D11Buffer *g_laser_buffer = nullptr;
		ID3D11VertexShader *g_vertex_shader = nullptr;
		ID3D11PixelShader *g_pixel_shader = nullptr;
		ID3D11PixelShader *g_cutout_pixel_shader = nullptr;
		ID3D11PixelShader *g_laser_pixel_shader = nullptr;
		ID3D11PixelShader *g_target_pixel_shader = nullptr;
		ID3D11InputLayout *g_input_layout = nullptr;
		ID3D11SamplerState *g_sampler = nullptr;
		ID3D11RasterizerState *g_rasterizer = nullptr;
		ID3D11DepthStencilState *g_depth_state = nullptr;
		ID3D11DepthStencilState *g_glass_depth_state = nullptr;
		ID3D11BlendState *g_alpha_blend_state = nullptr;
		std::wstring g_game_root;
		Tool g_active_tool = Tool::none;
		ItemVariant g_active_variant = ItemVariant::none;
		int g_active_catalog_item = -1;
		int g_loaded_catalog_item = -1;
		std::string g_active_item_uuid;
		bool g_player_seated = false;
		bool g_player_first_person = false;
		WristHudState g_wrist_hud_state{};
		bool g_render_suppressed = false;
		ULONGLONG g_last_poll = 0;
		ULONGLONG g_player_state_last_valid_ms = 0;
		uint64_t g_player_state_sequence = 0;
		bool g_player_state_sequence_valid = false;
		std::wstring g_player_state_source_path;
		bool g_player_state_source_custom = false;
		ULONGLONG g_gatling_animation_ms = 0;
		float g_gatling_angle = 0.0f;
		float g_gatling_speed = 0.0f;
		bool g_gatling_spin_logged = false;
		bool g_initialized = false;
		bool g_render_logged = false;
		ClayCalibration g_clay_calibration;
		std::wstring g_clay_calibration_path;
		ULONGLONG g_clay_calibration_poll_ms = 0;
		FILETIME g_clay_calibration_write_time = {};
		bool g_clay_calibration_loaded = false;
		PoseCalibration g_pose_calibrations[static_cast<size_t>(HeldProfile::count)];
		std::wstring g_held_calibration_path;
		std::wstring g_held_catalog_path;
		std::wstring g_held_status_path;
		ULONGLONG g_held_calibration_poll_ms = 0;
		FILETIME g_held_calibration_write_time = {};
		bool g_held_calibration_loaded = false;

		template <typename T> void release(T *&value) { if (value) { value->Release(); value = nullptr; } }

		float half_to_float(uint16_t half)
		{
			const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
			uint32_t exponent = (half >> 10) & 0x1fu;
			uint32_t mantissa = half & 0x03ffu;
			uint32_t bits = 0;
			if (exponent == 0)
			{
				if (mantissa == 0) bits = sign;
				else
				{
					exponent = 127u - 15u + 1u;
					while ((mantissa & 0x0400u) == 0) { mantissa <<= 1; --exponent; }
					mantissa &= 0x03ffu;
					bits = sign | (exponent << 23) | (mantissa << 13);
				}
			}
			else if (exponent == 0x1fu) bits = sign | 0x7f800000u | (mantissa << 13);
			else bits = sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13);
			float result = 0.0f;
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}

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

		Matrix rotation_x(float angle)
		{
			Matrix result = identity(); const float c = std::cos(angle), s = std::sin(angle);
			result.m[5] = c; result.m[9] = -s; result.m[6] = s; result.m[10] = c;
			return result;
		}

		Matrix rotation_y(float angle)
		{
			Matrix result = identity(); const float c = std::cos(angle), s = std::sin(angle);
			result.m[0] = c; result.m[8] = s; result.m[2] = -s; result.m[10] = c;
			return result;
		}

		Matrix rotation_axis(float x, float y, float z, float angle)
		{
			const float length = std::sqrt(x * x + y * y + z * z);
			if (length < 0.0001f || !std::isfinite(length)) return identity();
			x /= length; y /= length; z /= length;
			const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
			Matrix result = identity();
			result.m[0] = t * x * x + c;
			result.m[4] = t * x * y - s * z;
			result.m[8] = t * x * z + s * y;
			result.m[1] = t * x * y + s * z;
			result.m[5] = t * y * y + c;
			result.m[9] = t * y * z - s * x;
			result.m[2] = t * x * z - s * y;
			result.m[6] = t * y * z + s * x;
			result.m[10] = t * z * z + c;
			return result;
		}

		const wchar_t *profile_section(HeldProfile profile)
		{
			switch (profile)
			{
			case HeldProfile::hammer: return L"Hammer";
			case HeldProfile::connect: return L"ConnectionTool";
			case HeldProfile::paint: return L"PaintTool";
			case HeldProfile::weld: return L"WeldTool";
			case HeldProfile::rifle: return L"Spudgun";
			case HeldProfile::shotgun: return L"Shotgun";
			case HeldProfile::gatling: return L"GatlingGun";
			case HeldProfile::scrap: return L"ScrapSpudgun";
			case HeldProfile::launcher: return L"PotatoLauncher";
			case HeldProfile::clay: return L"ClayGun";
			case HeldProfile::lift: return L"Lift";
			case HeldProfile::handbook: return L"Handbook";
			case HeldProfile::bucket: return L"Bucket";
			case HeldProfile::glowstick: return L"Glowstick";
			case HeldProfile::cornade: return L"Cornade";
			case HeldProfile::loose_clay: return L"LooseClay";
			case HeldProfile::extinguisher: return L"FireExtinguisher";
			case HeldProfile::planter: return L"SeedPlanter";
			case HeldProfile::fertilizer: return L"Fertilizer";
			case HeldProfile::food: return L"FoodAndDrink";
			case HeldProfile::feeder: return L"LongSandwich";
			case HeldProfile::soilbag: return L"SoilBag";
			case HeldProfile::key: return L"KeyItems";
			case HeldProfile::powercore: return L"PowerCore";
			case HeldProfile::resource: return L"ResourceTool";
			case HeldProfile::carry: return L"CarryItems";
			case HeldProfile::logbook: return L"Logbook";
			case HeldProfile::blocks: return L"Blocks";
			case HeldProfile::wedges: return L"Wedges";
			case HeldProfile::small_parts: return L"SmallParts";
			case HeldProfile::medium_parts: return L"MediumParts";
			case HeldProfile::large_parts: return L"LargeParts";
			case HeldProfile::consumables: return L"Consumables";
			case HeldProfile::resources: return L"Resources";
			case HeldProfile::components: return L"Components";
			case HeldProfile::plantables: return L"Plantables";
			case HeldProfile::quest_items: return L"QuestSpecial";
			case HeldProfile::other_parts: return L"OtherParts";
			default: return L"OtherParts";
			}
		}

		const char *profile_name(HeldProfile profile)
		{
			switch (profile)
			{
			case HeldProfile::hammer: return "Hammer";
			case HeldProfile::connect: return "Connection Tool";
			case HeldProfile::paint: return "Paint Tool";
			case HeldProfile::weld: return "Weld Tool";
			case HeldProfile::rifle: return "Spudgun";
			case HeldProfile::shotgun: return "Shotgun";
			case HeldProfile::gatling: return "Gatling Gun";
			case HeldProfile::scrap: return "Scrap Spudgun";
			case HeldProfile::launcher: return "Potato Launcher";
			case HeldProfile::clay: return "Clay Gun";
			case HeldProfile::lift: return "Lift";
			case HeldProfile::handbook: return "Handbook";
			case HeldProfile::bucket: return "Bucket";
			case HeldProfile::glowstick: return "Glowstick";
			case HeldProfile::cornade: return "Cornade";
			case HeldProfile::loose_clay: return "Loose Clay";
			case HeldProfile::extinguisher: return "Fire Extinguisher";
			case HeldProfile::planter: return "Seed Planter";
			case HeldProfile::fertilizer: return "Fertilizer";
			case HeldProfile::food: return "Food and Drink";
			case HeldProfile::feeder: return "Long Sandwich";
			case HeldProfile::soilbag: return "Soil Bag";
			case HeldProfile::key: return "Key Items";
			case HeldProfile::powercore: return "Power Core";
			case HeldProfile::resource: return "Resource Tool";
			case HeldProfile::carry: return "Carry Items";
			case HeldProfile::logbook: return "Logbook";
			case HeldProfile::blocks: return "Blocks";
			case HeldProfile::wedges: return "Wedges";
			case HeldProfile::small_parts: return "Small Parts";
			case HeldProfile::medium_parts: return "Medium Parts";
			case HeldProfile::large_parts: return "Large Parts";
			case HeldProfile::consumables: return "Consumables";
			case HeldProfile::resources: return "Resources";
			case HeldProfile::components: return "Components";
			case HeldProfile::plantables: return "Plantables";
			case HeldProfile::quest_items: return "Quest / Special";
			case HeldProfile::other_parts: return "Other Parts";
			default: return "Other Parts";
			}
		}

		PoseCalibration default_pose(HeldProfile profile)
		{
			switch (profile)
			{
			case HeldProfile::hammer: return { 0.002f, -0.025f, -0.065f, 0, 0, 0, 0.148f };
			case HeldProfile::connect: return { -0.020f, -0.035f, -0.055f, 0, 0, 0, 0.160f };
			case HeldProfile::paint: return { -0.015f, -0.040f, -0.060f, 0, 0, 0, 0.160f };
			case HeldProfile::weld: return { -0.030f, -0.035f, -0.065f, 0, 0, 0, 0.150f };
			case HeldProfile::rifle:
			case HeldProfile::shotgun:
			case HeldProfile::gatling:
			case HeldProfile::scrap:
			case HeldProfile::launcher: return { -0.020f, -0.035f, -0.060f, 0, 0, 0, 0.145f };
			case HeldProfile::clay: return { -0.122f, -0.043f, -0.163f, 0, 0, 0, 0.145f };
			case HeldProfile::lift: return { -0.004f, -0.064f, -0.069f, 0, 60.10f, 0, 0.190f };
			case HeldProfile::handbook: return { -0.037f, -0.190f, -0.070f, 0, 48.90f, 0, 0.130f };
			case HeldProfile::bucket: return { -0.016f, -0.154f, -0.181f, 0, -2.50f, -94.60f, 0.070f };
			case HeldProfile::glowstick: return { -0.010f, -0.030f, -0.071f, 0, 0, 0, 0.160f };
			case HeldProfile::cornade: return { -0.015f, -0.035f, -0.069f, 0, 0, 0, 0.085f };
			case HeldProfile::loose_clay: return { -0.010f, -0.045f, -0.080f, 0, 0, 0, 0.105f };
			case HeldProfile::extinguisher: return { -0.020f, -0.045f, -0.045f, 0, 0, 0, 0.115f };
			case HeldProfile::planter: return { -0.010f, -0.025f, -0.045f, 0, 0, 0, 0.140f };
			case HeldProfile::fertilizer: return { -0.010f, -0.040f, -0.065f, 0, 0, 0, 0.120f };
			case HeldProfile::food: return { -0.010f, -0.035f, -0.055f, 0, 0, 0, 0.059f };
			case HeldProfile::feeder: return { -0.010f, -0.040f, -0.085f, 0, 0, 0, 0.075f };
			case HeldProfile::soilbag: return { -0.010f, -0.040f, -0.075f, 0, 0, 0, 0.100f };
			case HeldProfile::key: return { -0.010f, -0.030f, -0.045f, 0, 0, 0, 0.130f };
			case HeldProfile::powercore: return { -0.010f, -0.030f, -0.045f, 0, 0, 0, 0.140f };
			case HeldProfile::resource: return { -0.010f, -0.035f, -0.055f, 0, 0, 0, 0.140f };
			case HeldProfile::carry: return { -0.010f, -0.090f, -0.160f, 0, 0, 0, 0.210f };
			case HeldProfile::logbook: return { -0.010f, -0.040f, -0.085f, 0, 0, 0, 0.130f };
			case HeldProfile::blocks: return { -0.010f, -0.040f, -0.085f, 0, 0, 0, 0.075f };
			case HeldProfile::wedges: return { -0.010f, -0.040f, -0.085f, 0, 0, 0, 0.075f };
			case HeldProfile::small_parts: return { -0.010f, -0.040f, -0.085f, 0, 0, 0, 0.060f };
			case HeldProfile::medium_parts: return { -0.010f, -0.045f, -0.095f, 0, 0, 0, 0.075f };
			case HeldProfile::large_parts: return { -0.010f, -0.055f, -0.115f, 0, 0, 0, 0.095f };
			case HeldProfile::consumables: return { -0.010f, -0.040f, -0.075f, 0, 0, 0, 0.070f };
			case HeldProfile::resources: return { -0.010f, -0.045f, -0.090f, 0, 0, 0, 0.075f };
			case HeldProfile::components: return { -0.010f, -0.040f, -0.075f, 0, 0, 0, 0.065f };
			case HeldProfile::plantables: return { -0.010f, -0.040f, -0.080f, 0, 0, 0, 0.075f };
			case HeldProfile::quest_items: return { -0.010f, -0.045f, -0.090f, 0, 0, 0, 0.075f };
			case HeldProfile::other_parts:
			default: return { -0.010f, -0.045f, -0.090f, 0, 0, 0, 0.075f };
			}
		}

		void reset_pose_defaults()
		{
			for (size_t index = 0; index < static_cast<size_t>(HeldProfile::count); ++index)
				g_pose_calibrations[index] = default_pose(static_cast<HeldProfile>(index));
		}

		float read_ini_float(const std::wstring &path, const wchar_t *section, const wchar_t *key, float fallback)
		{
			wchar_t fallback_text[64] = {}, value[128] = {};
			swprintf_s(fallback_text, L"%.7g", fallback);
			GetPrivateProfileStringW(section, key, fallback_text, value,
				static_cast<DWORD>(sizeof(value) / sizeof(value[0])), path.c_str());
			wchar_t *end = nullptr;
			const float parsed = std::wcstof(value, &end);
			return end != value && std::isfinite(parsed) ? parsed : fallback;
		}

		void poll_clay_calibration()
		{
			const ULONGLONG now = GetTickCount64();
			if (now - g_clay_calibration_poll_ms < 100) return;
			g_clay_calibration_poll_ms = now;
			WIN32_FILE_ATTRIBUTE_DATA attributes = {};
			if (g_clay_calibration_path.empty() ||
				!GetFileAttributesExW(g_clay_calibration_path.c_str(), GetFileExInfoStandard, &attributes))
				return;
			if (g_clay_calibration_loaded &&
				CompareFileTime(&attributes.ftLastWriteTime, &g_clay_calibration_write_time) == 0)
				return;

			ClayCalibration next;
			next.tool_x = read_ini_float(g_clay_calibration_path, L"Tool", L"PositionX", next.tool_x);
			next.tool_y = read_ini_float(g_clay_calibration_path, L"Tool", L"PositionY", next.tool_y);
			next.tool_z = read_ini_float(g_clay_calibration_path, L"Tool", L"PositionZ", next.tool_z);
			next.tool_pitch = read_ini_float(g_clay_calibration_path, L"Tool", L"PitchDegrees", next.tool_pitch);
			next.tool_yaw = read_ini_float(g_clay_calibration_path, L"Tool", L"YawDegrees", next.tool_yaw);
			next.tool_roll = read_ini_float(g_clay_calibration_path, L"Tool", L"RollDegrees", next.tool_roll);
			next.scale = std::clamp(read_ini_float(g_clay_calibration_path, L"Tool", L"Scale", next.scale), 0.010f, 1.000f);

			next.container_pivot_x = read_ini_float(g_clay_calibration_path, L"Container", L"PivotX", next.container_pivot_x);
			next.container_pivot_y = read_ini_float(g_clay_calibration_path, L"Container", L"PivotY", next.container_pivot_y);
			next.container_pivot_z = read_ini_float(g_clay_calibration_path, L"Container", L"PivotZ", next.container_pivot_z);
			next.container_axis_x = read_ini_float(g_clay_calibration_path, L"Container", L"AxisX", next.container_axis_x);
			next.container_axis_y = read_ini_float(g_clay_calibration_path, L"Container", L"AxisY", next.container_axis_y);
			next.container_axis_z = read_ini_float(g_clay_calibration_path, L"Container", L"AxisZ", next.container_axis_z);
			next.container_speed = read_ini_float(g_clay_calibration_path, L"Container", L"SpeedMultiplier", next.container_speed);
			next.container_phase = read_ini_float(g_clay_calibration_path, L"Container", L"PhaseDegrees", next.container_phase);

			next.wheel_pivot_x = read_ini_float(g_clay_calibration_path, L"Wheel", L"PivotX", next.wheel_pivot_x);
			next.wheel_pivot_y = read_ini_float(g_clay_calibration_path, L"Wheel", L"PivotY", next.wheel_pivot_y);
			next.wheel_pivot_z = read_ini_float(g_clay_calibration_path, L"Wheel", L"PivotZ", next.wheel_pivot_z);
			next.wheel_axis_x = read_ini_float(g_clay_calibration_path, L"Wheel", L"AxisX", next.wheel_axis_x);
			next.wheel_axis_y = read_ini_float(g_clay_calibration_path, L"Wheel", L"AxisY", next.wheel_axis_y);
			next.wheel_axis_z = read_ini_float(g_clay_calibration_path, L"Wheel", L"AxisZ", next.wheel_axis_z);
			next.wheel_speed = read_ini_float(g_clay_calibration_path, L"Wheel", L"SpeedMultiplier", next.wheel_speed);
			next.wheel_phase = read_ini_float(g_clay_calibration_path, L"Wheel", L"PhaseDegrees", next.wheel_phase);

			g_clay_calibration = next;
			g_clay_calibration_write_time = attributes.ftLastWriteTime;
			g_clay_calibration_loaded = true;
			if (g_log) g_log("VR CLAY CALIBRATION RELOADED: pos %.4f %.4f %.4f rot %.2f %.2f %.2f scale %.4f",
				next.tool_x, next.tool_y, next.tool_z,
				next.tool_pitch, next.tool_yaw, next.tool_roll, next.scale);
		}

		void poll_held_calibration()
		{
			const ULONGLONG now = GetTickCount64();
			if (now - g_held_calibration_poll_ms < 75) return;
			g_held_calibration_poll_ms = now;
			WIN32_FILE_ATTRIBUTE_DATA attributes = {};
			if (g_held_calibration_path.empty() ||
				!GetFileAttributesExW(g_held_calibration_path.c_str(), GetFileExInfoStandard, &attributes))
				return;
			if (g_held_calibration_loaded &&
				CompareFileTime(&attributes.ftLastWriteTime, &g_held_calibration_write_time) == 0)
				return;

			const bool first_load = !g_held_calibration_loaded;
			PoseCalibration next[static_cast<size_t>(HeldProfile::count)] = {};
			for (size_t index = 0; index < static_cast<size_t>(HeldProfile::count); ++index)
			{
				const HeldProfile profile = static_cast<HeldProfile>(index);
				next[index] = default_pose(profile);
				const wchar_t *section = profile_section(profile);
				next[index].x = read_ini_float(g_held_calibration_path, section, L"PositionX", next[index].x);
				next[index].y = read_ini_float(g_held_calibration_path, section, L"PositionY", next[index].y);
				next[index].z = read_ini_float(g_held_calibration_path, section, L"PositionZ", next[index].z);
				next[index].pitch = read_ini_float(g_held_calibration_path, section, L"PitchDegrees", next[index].pitch);
				next[index].yaw = read_ini_float(g_held_calibration_path, section, L"YawDegrees", next[index].yaw);
				next[index].roll = read_ini_float(g_held_calibration_path, section, L"RollDegrees", next[index].roll);
				next[index].scale = std::clamp(
					read_ini_float(g_held_calibration_path, section, L"Scale", next[index].scale), 0.005f, 2.000f);
			}
			std::copy(std::begin(next), std::end(next), std::begin(g_pose_calibrations));
			g_held_calibration_write_time = attributes.ftLastWriteTime;
			g_held_calibration_loaded = true;
			if (first_load && g_log) g_log("VR HELD ITEM CALIBRATION READY: %u grouped live profiles",
				static_cast<unsigned int>(HeldProfile::count));
		}

		std::wstring module_root()
		{
			wchar_t module_path[MAX_PATH] = {};
			HMODULE module = GetModuleHandleW(L"smvr_native_vr_v1.addon64");
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

		bool create_texture(const wchar_t *relative, ID3D11ShaderResourceView **output,
			uint32_t paint_rgba = 0xffffffffu, bool apply_paint = false,
			const wchar_t *mask_relative = nullptr,
			held_item_catalog::Material material = held_item_catalog::Material::opaque)
		{
			std::vector<uint8_t> pixels; uint32_t width = 0, height = 0;
			if (!load_tga(relative, pixels, width, height)) return false;
			std::vector<uint8_t> mask_pixels; uint32_t mask_width = 0, mask_height = 0;
			const bool has_cutout_mask = material == held_item_catalog::Material::cutout &&
				mask_relative && load_tga(mask_relative, mask_pixels, mask_width, mask_height) &&
				mask_width == width && mask_height == height;
			if (apply_paint)
			{
				// Scrap Mechanic's paintable diffuse maps do not store ordinary RGBA.
				// RGB contains the unpainted contribution and inverse alpha is the
				// paint mask. Sampling those files directly makes common blocks (whose
				// RGB is intentionally black) appear as featureless black geometry.
				const uint8_t paint[] = {
					static_cast<uint8_t>(paint_rgba & 0xffu),
					static_cast<uint8_t>((paint_rgba >> 8) & 0xffu),
					static_cast<uint8_t>((paint_rgba >> 16) & 0xffu)
				};
				for (size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
				{
					const unsigned int inverse_mask = 255u - pixels[offset + 3];
					for (size_t channel = 0; channel < 3; ++channel)
					{
						const unsigned int painted = pixels[offset + channel] +
							(static_cast<unsigned int>(paint[channel]) * inverse_mask + 127u) / 255u;
						pixels[offset + channel] = static_cast<uint8_t>(std::min(painted, 255u));
					}
					// Diffuse alpha is the paint mask, not surface transparency. Alpha
					// materials store their actual coverage in the red ASG channel.
					if (material == held_item_catalog::Material::glass)
						pixels[offset + 3] = 112;
					else if (has_cutout_mask)
						pixels[offset + 3] = mask_pixels[offset];
					else
						pixels[offset + 3] = 255;
				}
			}
			D3D11_TEXTURE2D_DESC desc = {}; desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = pixels.data(); data.SysMemPitch = width * 4;
			ID3D11Texture2D *texture = nullptr;
			const bool ok = SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)) && SUCCEEDED(g_device->CreateShaderResourceView(texture, nullptr, output));
			release(texture); return ok;
		}

		bool create_solid_texture(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
			ID3D11ShaderResourceView **output)
		{
			const uint8_t pixel[4] = { red, green, blue, alpha };
			D3D11_TEXTURE2D_DESC desc = {}; desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = pixel; data.SysMemPitch = 4;
			ID3D11Texture2D *texture = nullptr;
			const bool ok = SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)) &&
				SUCCEEDED(g_device->CreateShaderResourceView(texture, nullptr, output));
			release(texture); return ok;
		}

		bool create_resource(DrawResource &draw, const Vertex *vertices, uint32_t count,
			const wchar_t *texture, uint32_t rgba = 0xffffffffu, bool apply_paint = false,
			const wchar_t *mask = nullptr,
			held_item_catalog::Material material = held_item_catalog::Material::opaque)
		{
			release(draw.vertices);
			release(draw.texture);
			draw.count = count; draw.material = material;
			D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = count * sizeof(Vertex); desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = vertices;
			if (FAILED(g_device->CreateBuffer(&desc, &data, &draw.vertices))) return false;
			if (texture && create_texture(texture, &draw.texture, rgba, apply_paint, mask, material)) return true;
			const bool ok = create_solid_texture(
				static_cast<uint8_t>(rgba & 0xffu),
				static_cast<uint8_t>((rgba >> 8) & 0xffu),
				static_cast<uint8_t>((rgba >> 16) & 0xffu),
				material == held_item_catalog::Material::glass ? 112u :
				apply_paint ? 255u : static_cast<uint8_t>((rgba >> 24) & 0xffu), &draw.texture);
			if (!ok) { release(draw.vertices); draw.count = 0; }
			return ok;
		}

		bool create_draw(DrawId id, const Vertex *vertices, uint32_t count, const wchar_t *texture)
		{
			return create_resource(g_draws[id], vertices, count, texture);
		}

		bool create_solid_draw(DrawId id, const Vertex *vertices, uint32_t count,
			uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
		{
			DrawResource &draw = g_draws[id]; draw.count = count;
			D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = count * sizeof(Vertex);
			desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = vertices;
			return SUCCEEDED(g_device->CreateBuffer(&desc, &data, &draw.vertices)) &&
				create_solid_texture(red, green, blue, alpha, &draw.texture);
		}

		void draw_resource(ID3D11DeviceContext *context, DrawId id)
		{
			DrawResource &draw = g_draws[id]; if (!draw.vertices || !draw.texture) return;
			UINT stride = sizeof(Vertex), offset = 0;
			context->IASetVertexBuffers(0, 1, &draw.vertices, &stride, &offset);
			context->PSSetShaderResources(0, 1, &draw.texture);
			context->Draw(draw.count, 0);
		}

		void draw_held_range(ID3D11DeviceContext *context, held_item_asset::MeshRange range)
		{
			const unsigned int end = std::min(range.first + range.count, held_item_asset::mesh_count);
			for (unsigned int index = range.first; index < end; ++index)
			{
				DrawResource &draw = g_held_draws[index];
				if (!draw.vertices || !draw.texture) continue;
				UINT stride = sizeof(Vertex), offset = 0;
				context->IASetVertexBuffers(0, 1, &draw.vertices, &stride, &offset);
				context->PSSetShaderResources(0, 1, &draw.texture);
				context->Draw(draw.count, 0);
			}
		}

		void release_catalog_draws()
		{
			for (auto &draw : g_catalog_draws)
			{
				release(draw.vertices);
				release(draw.texture);
				draw.count = 0;
			}
			g_catalog_draws.clear();
			g_loaded_catalog_item = -1;
		}

		int find_catalog_item(std::string uuid)
		{
			std::transform(uuid.begin(), uuid.end(), uuid.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			const auto *begin = held_item_catalog::items;
			const auto *end = begin + held_item_catalog::item_count;
			const auto *found = std::lower_bound(begin, end, uuid.c_str(),
				[](const held_item_catalog::Item &item, const char *value) {
					return std::strcmp(item.uuid, value) < 0;
				});
			if (found == end || std::strcmp(found->uuid, uuid.c_str()) != 0) return -1;
			return static_cast<int>(found - begin);
		}

		bool load_catalog_item(int item_index)
		{
			if (item_index == g_loaded_catalog_item && !g_catalog_draws.empty()) return true;
			release_catalog_draws();
			if (item_index < 0 || static_cast<unsigned int>(item_index) >= held_item_catalog::item_count)
				return false;
			const auto &item = held_item_catalog::items[item_index];
			if (item.asset >= held_item_catalog::asset_count) return false;
			const auto &asset = held_item_catalog::assets[item.asset];
			if (asset.first_submesh > held_item_catalog::submesh_count ||
				asset.submesh_count > held_item_catalog::submesh_count - asset.first_submesh)
				return false;

			std::ifstream stream(g_held_catalog_path.c_str(), std::ios::binary);
			if (!stream)
			{
				if (g_log) g_log("VR HELD ITEM CATALOG: binary geometry file is missing");
				return false;
			}
			g_catalog_draws.reserve(asset.submesh_count);
			for (unsigned int offset = 0; offset < asset.submesh_count; ++offset)
			{
				const auto &submesh = held_item_catalog::submeshes[asset.first_submesh + offset];
				if (!submesh.vertex_count || submesh.first_vertex >
					static_cast<unsigned long long>(std::numeric_limits<std::streamoff>::max() / sizeof(PackedCatalogVertex)))
				{
					release_catalog_draws();
					return false;
				}
				std::vector<PackedCatalogVertex> packed(submesh.vertex_count);
				stream.clear();
				stream.seekg(static_cast<std::streamoff>(submesh.first_vertex * sizeof(PackedCatalogVertex)), std::ios::beg);
				stream.read(reinterpret_cast<char *>(packed.data()),
					static_cast<std::streamsize>(packed.size() * sizeof(PackedCatalogVertex)));
				if (!stream)
				{
					release_catalog_draws();
					return false;
				}
				std::vector<Vertex> vertices(submesh.vertex_count);
				for (size_t index = 0; index < packed.size(); ++index)
				{
					const auto &source = packed[index];
					vertices[index] = {
						source.position[0], source.position[1], source.position[2],
						static_cast<float>(source.normal[0]) / 32767.0f,
						static_cast<float>(source.normal[1]) / 32767.0f,
						static_cast<float>(source.normal[2]) / 32767.0f,
						half_to_float(source.uv[0]), half_to_float(source.uv[1])
					};
				}
				g_catalog_draws.emplace_back();
				if (!create_resource(g_catalog_draws.back(), vertices.data(), submesh.vertex_count,
					submesh.texture, submesh.rgba ? submesh.rgba : item.tint, true,
					submesh.mask, submesh.material))
				{
					release_catalog_draws();
					return false;
				}
			}
			g_loaded_catalog_item = item_index;
			return true;
		}

		void draw_catalog_item(ID3D11DeviceContext *context)
		{
			const float blend_factor[4] = { 0, 0, 0, 0 };
			for (auto &draw : g_catalog_draws)
			{
				if (!draw.vertices || !draw.texture) continue;
				if (draw.material == held_item_catalog::Material::glass)
				{
					context->PSSetShader(g_pixel_shader, nullptr, 0);
					context->OMSetBlendState(g_alpha_blend_state, blend_factor, 0xffffffffu);
					context->OMSetDepthStencilState(g_glass_depth_state, 0);
				}
				else
				{
					context->PSSetShader(draw.material == held_item_catalog::Material::cutout ?
						g_cutout_pixel_shader : g_pixel_shader, nullptr, 0);
					context->OMSetBlendState(nullptr, blend_factor, 0xffffffffu);
					context->OMSetDepthStencilState(g_depth_state, 0);
				}
				UINT stride = sizeof(Vertex), offset = 0;
				context->IASetVertexBuffers(0, 1, &draw.vertices, &stride, &offset);
				context->PSSetShaderResources(0, 1, &draw.texture);
				context->Draw(draw.count, 0);
			}
			context->PSSetShader(g_pixel_shader, nullptr, 0);
			context->OMSetBlendState(nullptr, blend_factor, 0xffffffffu);
			context->OMSetDepthStencilState(g_depth_state, 0);
		}

		HeldProfile catalog_profile(held_item_catalog::Profile profile)
		{
			switch (profile)
			{
			case held_item_catalog::Profile::blocks: return HeldProfile::blocks;
			case held_item_catalog::Profile::wedges: return HeldProfile::wedges;
			case held_item_catalog::Profile::small_parts: return HeldProfile::small_parts;
			case held_item_catalog::Profile::medium_parts: return HeldProfile::medium_parts;
			case held_item_catalog::Profile::large_parts: return HeldProfile::large_parts;
			case held_item_catalog::Profile::consumables: return HeldProfile::consumables;
			case held_item_catalog::Profile::resources: return HeldProfile::resources;
			case held_item_catalog::Profile::components: return HeldProfile::components;
			case held_item_catalog::Profile::plantables: return HeldProfile::plantables;
			case held_item_catalog::Profile::quest_items: return HeldProfile::quest_items;
			case held_item_catalog::Profile::other_parts:
			default: return HeldProfile::other_parts;
			}
		}

		HeldProfile profile_for_tool(Tool tool, ItemVariant variant, int catalog_item)
		{
			switch (tool)
			{
			case Tool::hammer: return HeldProfile::hammer;
			case Tool::connect: return HeldProfile::connect;
			case Tool::paint: return HeldProfile::paint;
			case Tool::weld: return HeldProfile::weld;
			case Tool::rifle: return HeldProfile::rifle;
			case Tool::shotgun: return HeldProfile::shotgun;
			case Tool::gatling: return HeldProfile::gatling;
			case Tool::scrap: return HeldProfile::scrap;
			case Tool::launcher: return HeldProfile::launcher;
			case Tool::clay: return HeldProfile::clay;
			case Tool::lift: return HeldProfile::lift;
			case Tool::handbook: return HeldProfile::handbook;
			case Tool::bucket: return HeldProfile::bucket;
			case Tool::glowstick: return HeldProfile::glowstick;
			case Tool::cornade: return HeldProfile::cornade;
			case Tool::loose_clay: return HeldProfile::loose_clay;
			case Tool::extinguisher: return HeldProfile::extinguisher;
			case Tool::planter: return HeldProfile::planter;
			case Tool::fertilizer: return HeldProfile::fertilizer;
			case Tool::food: return HeldProfile::food;
			case Tool::feeder: return HeldProfile::feeder;
			case Tool::soilbag: return HeldProfile::soilbag;
			case Tool::key: return variant == ItemVariant::powercore ? HeldProfile::powercore : HeldProfile::key;
			case Tool::resource: return HeldProfile::resource;
			case Tool::carry: return HeldProfile::carry;
			case Tool::logbook: return HeldProfile::logbook;
			case Tool::catalog:
				if (catalog_item >= 0 && static_cast<unsigned int>(catalog_item) < held_item_catalog::item_count)
					return catalog_profile(held_item_catalog::items[catalog_item].profile);
				return HeldProfile::other_parts;
			default: return HeldProfile::other_parts;
			}
		}

		HeldProfile active_profile()
		{
			return profile_for_tool(g_active_tool, g_active_variant, g_active_catalog_item);
		}

		const char *tool_name(Tool tool)
		{
			switch (tool)
			{
			case Tool::hammer: return "hammer"; case Tool::connect: return "connect";
			case Tool::paint: return "paint"; case Tool::weld: return "weld";
			case Tool::rifle: return "spudgun"; case Tool::shotgun: return "shotgun";
			case Tool::gatling: return "gatling"; case Tool::scrap: return "scrap spudgun";
			case Tool::launcher: return "potato launcher"; case Tool::clay: return "clay gun";
			case Tool::lift: return "lift remote"; case Tool::handbook: return "handbook";
			case Tool::bucket: return "bucket"; case Tool::glowstick: return "glowstick";
			case Tool::cornade: return "cornade"; case Tool::loose_clay: return "loose clay";
			case Tool::extinguisher: return "fire extinguisher"; case Tool::planter: return "seed planter";
			case Tool::fertilizer: return "fertilizer"; case Tool::food: return "food or drink";
			case Tool::feeder: return "long sandwich"; case Tool::soilbag: return "soil bag";
			case Tool::key: return "key item"; case Tool::resource: return "resource item";
			case Tool::carry: return "carry item"; case Tool::logbook: return "logbook";
			case Tool::catalog: return "catalog item";
			default: return "none";
			}
		}

		Tool parse_tool(const std::string &line, const std::string &adapter, ItemVariant &variant)
		{
			variant = ItemVariant::none;
			// Scrap Mechanic 1.0 gives the creative-mode sledgehammer its own
			// tool UUID. It uses the same mesh and physical-swing path.
			if (line.find("ed185725-ea12-43fc-9cd7-4295d0dbf88b") != std::string::npos) return Tool::hammer;
			if (line.find("bb641a4f-e391-441c-bc6d-0ae21a069476") != std::string::npos) return Tool::hammer;
			if (line.find("8c7efc37-cd7c-4262-976e-39585f8527bf") != std::string::npos) return Tool::connect;
			if (line.find("c60b9627-fc2b-4319-97c5-05921cb976c6") != std::string::npos) return Tool::paint;
			if (line.find("fdb8b8be-96e7-4de0-85c7-d2f42e4f33ce") != std::string::npos) return Tool::weld;
			if (line.find("c5ea0c2f-185b-48d6-b4df-45c386a575cc") != std::string::npos) return Tool::rifle;
			if (line.find("f6250bf4-9726-406f-a29a-945c06e460e5") != std::string::npos) return Tool::shotgun;
			if (line.find("9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b") != std::string::npos) return Tool::gatling;
			if (line.find("d51ec758-057b-4263-bd16-7a731e149480") != std::string::npos) return Tool::scrap;
			if (line.find("a2a2bb33-a841-4b23-88da-b758063d9206") != std::string::npos) return Tool::launcher;
			if (line.find("6993e5df-6852-4e84-88ae-df49f765e784") != std::string::npos) return Tool::clay;
			if (line.find("5cc12f03-275e-4c8e-b013-79fc0f913e1b") != std::string::npos ||
				line.find("8f190ce2-3a59-423e-8483-a7aa67bd5bc0") != std::string::npos) return Tool::lift;
			if (line.find("3384010e-bc1c-42bb-83ef-dbc78a1f9636") != std::string::npos) return Tool::handbook;

			if (line.find("798c2c81-1f8e-481b-8c32-b71b5dc5511a") != std::string::npos)
			{ variant = ItemVariant::bucket_empty; return Tool::bucket; }
			if (line.find("103fc4e6-7e57-465e-a86d-983343415877") != std::string::npos)
			{ variant = ItemVariant::bucket_water; return Tool::bucket; }
			if (line.find("cc80b6e0-f756-4036-9cd6-77af13a6de36") != std::string::npos)
			{ variant = ItemVariant::bucket_oil; return Tool::bucket; }
			if (line.find("2e792123-4a10-4cc6-b9ef-c5a518655cb4") != std::string::npos)
			{ variant = ItemVariant::bucket_chemical; return Tool::bucket; }
			if (line.find("3a3280e4-03b6-4a4d-9e02-e348478213c9") != std::string::npos) return Tool::glowstick;
			if (line.find("e3bdeea5-d349-4d08-9b5a-5695ea05537e") != std::string::npos) return Tool::cornade;
			if (line.find("6395a2f1-4169-4a7e-be15-a9864cb6ce7e") != std::string::npos) return Tool::loose_clay;
			if (line.find("d2fab7ef-21db-4681-a22a-cd4f278fc355") != std::string::npos) return Tool::extinguisher;
			if (line.find("ac0b5b0a-14e1-4b31-8944-0a351fbfcc67") != std::string::npos) return Tool::fertilizer;
			if (line.find("e243f642-6934-42bb-8cdd-f8ff1704d411") != std::string::npos) return Tool::feeder;
			if (line.find("9a3e478c-2224-44fa-887c-239965bd05ad") != std::string::npos) return Tool::soilbag;
			if (line.find("0f6d1667-ddf8-4ca9-b290-8bd3ec9b038b") != std::string::npos) return Tool::resource;
			if (line.find("a6113860-f4d4-42df-b8f9-129efdbaf777") != std::string::npos) return Tool::logbook;

			// All Chapter 2 seed items share the Planter auto-tool.
			static const char *seed_items[] = {
				"22beade5-38ca-47b4-a2ee-32403f58a862", "4b6d2bee-d0f1-4e56-96f0-d2596388cad2",
				"1c6756ca-3a60-4dcb-a5d1-353edf818308", "9c82a525-8a8b-4483-9595-505aaa042486",
				"8883e0ee-8a6e-423a-a4e0-583d9bf105bd", "93c27ab2-4930-4654-ba1c-bcfe35e966f6",
				"bee966b0-b5e5-41da-b992-5d363ab85ae4", "c44b27da-88cf-4e17-b872-6236a1172688",
				"9edb6f7c-fb44-4348-a1c4-8afb41b92d8a", "eb1ef696-5c05-4662-9e47-fe1e0875ff84",
				"64051718-a3f1-422b-bda3-277efa0c4545", "38e41fb5-dd50-4294-829d-a517f0282fed"
			};
			for (const char *uuid : seed_items) if (line.find(uuid) != std::string::npos) return Tool::planter;

			struct VariantItem { const char *uuid; ItemVariant variant; };
			static const VariantItem food_items[] = {
				{ "cb7305b2-d8b5-4302-aff3-6cdd9212ca64", ItemVariant::food_sunshake },
				{ "2c4a2633-153a-4800-ba3d-2ac0d993b9c8", ItemVariant::food_milk },
				{ "54d8ef21-357d-48a3-a66d-40446f6bb686", ItemVariant::food_carrotburger },
				{ "54d84731-d9ec-435d-bc9d-d48e0763b1bf", ItemVariant::food_pizzaburger },
				{ "aa4c9c5e-7fc6-4c27-967f-c550e551c872", ItemVariant::food_banana },
				{ "6a43fff2-8c6d-4460-9f44-e5483b5267dd", ItemVariant::food_blueberry },
				{ "f5098301-1693-457b-8efc-83b3504105ac", ItemVariant::food_orange },
				{ "4ec64cda-1a5b-4465-88b4-5ea452c4a556", ItemVariant::food_pineapple },
				{ "47ece75a-bfca-4e8a-b618-4f609fcea0da", ItemVariant::food_carrot },
				{ "4ce00048-f735-4fab-b978-5f405e60f48f", ItemVariant::food_redbeet },
				{ "6d92d8e7-25e9-4698-b83d-a64dc97978c8", ItemVariant::food_tomato },
				{ "b5cdd503-fe1c-482b-86ab-6a5d2cc4fc8f", ItemVariant::food_broccoli },
				{ "fe8bfeba-850b-4827-9785-10e2468c9c23", ItemVariant::food_corn },
				{ "8e61a423-5aa6-4dd3-ac57-ecac313f82f5", ItemVariant::food_tea },
				{ "ba3fbedc-1802-4406-852d-3f3a7ddc4be2", ItemVariant::food_chili }
			};
			for (const auto &item : food_items)
				if (line.find(item.uuid) != std::string::npos) { variant = item.variant; return Tool::food; }

			if (line.find("e49b210a-0d46-4f06-bcd8-08862379d156") != std::string::npos ||
				line.find("d6e5bd19-b26d-4619-9df6-d451d3389513") != std::string::npos)
			{ variant = ItemVariant::keycard; return Tool::key; }
			if (line.find("c654a023-ca53-4109-9f18-d297c18e9a02") != std::string::npos)
			{ variant = ItemVariant::powercore; return Tool::key; }

			// The patched stock scripts also publish their active tool class.  Keep
			// UUID matching above for exact visual variants, then use the class as a
			// complete fallback for content additions and every item routed through a
			// shared auto-tool (especially ResourceTool and CarryTool).
			if (adapter == "bucket") return Tool::bucket;
			if (adapter == "glowstick") return Tool::glowstick;
			if (adapter == "cornade") return Tool::cornade;
			if (adapter == "clay") return Tool::loose_clay;
			if (adapter == "extinguisher") return Tool::extinguisher;
			if (adapter == "planter") return Tool::planter;
			if (adapter == "fertilizer") return Tool::fertilizer;
			if (adapter == "food") return Tool::food;
			if (adapter == "feeder") return Tool::feeder;
			if (adapter == "soilbag") return Tool::soilbag;
			if (adapter == "key") return Tool::key;
			if (adapter == "resource") return Tool::resource;
			if (adapter == "carry") return Tool::carry;
			if (adapter == "logbook") return Tool::logbook;
			return Tool::none;
		}

		const ToolCalibration &calibration_for(Tool tool)
		{
			// Final values tuned in-headset on 2026-07-18. These are intentionally
			// baked so normal play performs no calibration-file polling.
			static const ToolCalibration values[static_cast<size_t>(Tool::count)] = {
				{  0.000f, -0.035f, -0.045f,  0.000f,  0.000f, -0.300f }, // none
				{  0.000f, -0.025f, -0.065f,  0.000f,  0.000f, -0.300f }, // hammer
				{ -0.020f, -0.035f, -0.055f, -0.152f, -0.035f, -0.280f }, // connect
				{ -0.015f, -0.040f, -0.060f, -0.120f, -0.040f, -0.295f }, // paint
				{ -0.030f, -0.035f, -0.065f, -0.035f, -0.035f, -0.225f }, // weld
				// Gun offsets are the single calibrated barrel-tip source used by the
				// Chapter 2 projectile bridge. The old visible ray was debug-only.
				{ -0.020f, -0.035f, -0.060f, -0.198f, -0.035f, -0.466f }, // spudgun
				{ -0.020f, -0.035f, -0.060f, -0.199f, -0.035f, -0.503f }, // shotgun
				{ -0.020f, -0.035f, -0.060f, -0.198f, -0.035f, -0.509f }, // gatling
				{ -0.020f, -0.035f, -0.060f, -0.234f, -0.050f, -0.384f }, // scrap spudgun
				{ -0.020f, -0.035f, -0.060f, -0.198f, -0.035f, -0.426f }, // launcher
				{ -0.020f, -0.035f, -0.060f, -0.085f, -0.035f, -0.424f }, // clay gun
				{ -0.010f, -0.030f, -0.060f,  0.000f, -0.035f, -0.180f }, // lift remote
				{ -0.010f, -0.045f, -0.100f,  0.000f, -0.035f, -0.180f }, // handbook
				{ -0.015f, -0.060f, -0.090f,  0.000f, -0.035f, -0.180f }, // bucket
				{ -0.010f, -0.030f, -0.045f,  0.000f, -0.035f, -0.180f }, // glowstick
				{ -0.015f, -0.035f, -0.060f,  0.000f, -0.035f, -0.180f }, // cornade
				{ -0.010f, -0.045f, -0.080f,  0.000f, -0.035f, -0.180f }, // loose clay
				{ -0.020f, -0.045f, -0.075f, -0.090f, -0.035f, -0.310f }, // extinguisher
				{ -0.010f, -0.025f, -0.045f,  0.000f, -0.035f, -0.180f }, // planter
				{ -0.010f, -0.040f, -0.065f,  0.000f, -0.035f, -0.180f }, // fertilizer
				{ -0.010f, -0.035f, -0.055f,  0.000f, -0.035f, -0.180f }, // food/drink
				{ -0.010f, -0.040f, -0.085f,  0.000f, -0.035f, -0.180f }, // long sandwich
				{ -0.010f, -0.040f, -0.075f,  0.000f, -0.035f, -0.180f }, // soil bag
				{ -0.010f, -0.030f, -0.045f,  0.000f, -0.035f, -0.180f }, // key item
				{ -0.010f, -0.035f, -0.055f,  0.000f, -0.035f, -0.180f }, // resource
				{ -0.010f, -0.090f, -0.160f,  0.000f, -0.035f, -0.180f }, // carry proxy
				{ -0.010f, -0.040f, -0.085f,  0.000f, -0.035f, -0.180f }  // logbook
			};
			return values[static_cast<size_t>(tool)];
		}

		bool is_gun(Tool tool)
		{
			switch (tool)
			{
			case Tool::rifle:
			case Tool::shotgun:
			case Tool::gatling:
			case Tool::scrap:
			case Tool::launcher:
			case Tool::clay:
				return true;
			default:
				return false;
			}
		}

		bool has_projectile_aim(Tool tool)
		{
			return is_gun(tool) || tool == Tool::glowstick || tool == Tool::cornade ||
				tool == Tool::extinguisher;
		}

		void update_spinner_animation(bool firing, bool clay)
		{
			const ULONGLONG now = GetTickCount64();
			if (!g_gatling_animation_ms) { g_gatling_animation_ms = now; return; }
			const ULONGLONG elapsed_ms = now - g_gatling_animation_ms;
			// Avoid advancing separately for the two stereo-eye submissions.
			if (elapsed_ms < 6) return;
			g_gatling_animation_ms = now;
			const float dt = std::min(0.050f, static_cast<float>(elapsed_ms) * 0.001f);
			// ClayRifle.lua advances a one-turn animation at 6.6667 cycles/s,
			// blends in over 0.25 s and blends out over 2.6667 s. Preserve those
			// timings for the native mesh instead of spinning the complete gun.
			const float target_speed = firing ? (clay ? 41.887902f : 30.0f) : 0.0f;
			const float acceleration = firing ? (clay ? 167.55161f : 72.0f) : (clay ? 15.707963f : 48.0f);
			if (g_gatling_speed < target_speed) g_gatling_speed = std::min(target_speed, g_gatling_speed + acceleration * dt);
			else g_gatling_speed = std::max(target_speed, g_gatling_speed - acceleration * dt);
			g_gatling_angle = std::fmod(g_gatling_angle + g_gatling_speed * dt, 6.28318530718f);
			if (firing && !g_gatling_spin_logged && g_log)
			{
				g_gatling_spin_logged = true;
				g_log(clay ? "VR CLAY SPINNER ACTIVE: native container and wheel follow the stock claygun spin animation axes" :
					"VR GATLING SPINNER ACTIVE: trigger-driven barrel spin-up and spin-down");
			}
		}

		bool json_bool(const std::string &text, const char *name, bool &value)
		{
			const std::string marker = std::string("\"") + name + "\"";
			auto position = text.find(marker);
			if (position == std::string::npos) return false;
			position = text.find(':', position + marker.size());
			if (position == std::string::npos) return false;
			do { ++position; } while (position < text.size() &&
				(text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n'));
			if (text.compare(position, 4, "true") == 0) { value = true; return true; }
			if (text.compare(position, 5, "false") == 0) { value = false; return true; }
			return false;
		}

		bool json_string(const std::string &text, const char *name, std::string &value)
		{
			const std::string marker = std::string("\"") + name + "\"";
			auto position = text.find(marker);
			if (position == std::string::npos) return false;
			position = text.find(':', position + marker.size());
			if (position == std::string::npos) return false;
			position = text.find('"', position + 1);
			if (position == std::string::npos) return false;
			const auto end = text.find('"', position + 1);
			if (end == std::string::npos) return false;
			value = text.substr(position + 1, end - position - 1);
			return true;
		}

		bool json_uint64(const std::string &text, const char *name, uint64_t &value)
		{
			const std::string marker = std::string("\"") + name + "\"";
			auto position = text.find(marker);
			if (position == std::string::npos) return false;
			position = text.find(':', position + marker.size());
			if (position == std::string::npos) return false;
			do { ++position; } while (position < text.size() &&
				(text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n'));
			if (position >= text.size() || text[position] < '0' || text[position] > '9') return false;
			uint64_t parsed = 0;
			while (position < text.size() && text[position] >= '0' && text[position] <= '9')
			{
				const uint64_t digit = static_cast<uint64_t>(text[position] - '0');
				if (parsed > (UINT64_MAX - digit) / 10) return false;
				parsed = parsed * 10 + digit;
				++position;
			}
			value = parsed;
			return true;
		}

		bool json_number(const std::string &text, const char *name, double &value)
		{
			const std::string marker = std::string("\"") + name + "\"";
			auto position = text.find(marker);
			if (position == std::string::npos) return false;
			position = text.find(':', position + marker.size());
			if (position == std::string::npos) return false;
			do { ++position; } while (position < text.size() &&
				(text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n'));
			if (position >= text.size()) return false;
			const char *begin = text.c_str() + position;
			char *end = nullptr;
			const double parsed = std::strtod(begin, &end);
			if (end == begin || !std::isfinite(parsed)) return false;
			value = parsed;
			return true;
		}

		bool json_waypoints(const std::string &text,
			std::array<WristHudWaypoint, kMaxWristHudWaypoints> &waypoints,
			uint32_t &count)
		{
			waypoints = {};
			count = 0;
			const std::string marker = "\"waypoints\"";
			size_t position = text.find(marker);
			if (position == std::string::npos) return false;
			position = text.find('[', position + marker.size());
			if (position == std::string::npos) return false;
			const size_t array_begin = position;
			unsigned depth = 0;
			size_t array_end = std::string::npos;
			for (; position < text.size(); ++position)
			{
				if (text[position] == '[') ++depth;
				else if (text[position] == ']' && depth > 0 && --depth == 0)
				{
					array_end = position;
					break;
				}
			}
			if (array_end == std::string::npos) return false;

			position = array_begin + 1;
			while (position < array_end && count < kMaxWristHudWaypoints)
			{
				const size_t object_begin = text.find('{', position);
				if (object_begin == std::string::npos || object_begin >= array_end) break;
				const size_t object_end = text.find('}', object_begin + 1);
				if (object_end == std::string::npos || object_end > array_end) break;
				const std::string object = text.substr(object_begin, object_end - object_begin + 1);
				double angle = 0.0, distance = 0.0, kind = 1.0;
				const bool angle_ok = json_number(object, "angle", angle);
				const bool distance_ok = json_number(object, "distance", distance);
				json_number(object, "kind", kind);
				if (angle_ok && distance_ok && std::isfinite(angle) && std::isfinite(distance) &&
					distance >= 0.0 && distance <= 100000.0 && angle >= -6.28318530718 && angle <= 6.28318530718)
				{
					WristHudWaypoint &waypoint = waypoints[count++];
					waypoint.angle = static_cast<float>(angle);
					waypoint.distance = static_cast<float>(distance);
					waypoint.kind = (std::isfinite(kind) && kind >= 1.0 && kind <= 4.0)
						? static_cast<uint32_t>(kind + 0.5) : 1u;
				}
				position = object_end + 1;
			}
			return true;
		}

		std::string json_escape(const char *value)
		{
			std::string result;
			if (!value) return result;
			for (const unsigned char c : std::string(value))
			{
				switch (c)
				{
				case '\\': result += "\\\\"; break;
				case '"': result += "\\\""; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default: if (c >= 32) result.push_back(static_cast<char>(c)); break;
				}
			}
			return result;
		}

		void write_held_status()
		{
			if (g_held_status_path.empty()) return;
			const HeldProfile profile = active_profile();
			const char *item_name = tool_name(g_active_tool);
			if (g_active_tool == Tool::catalog && g_active_catalog_item >= 0 &&
				static_cast<unsigned int>(g_active_catalog_item) < held_item_catalog::item_count)
				item_name = held_item_catalog::items[g_active_catalog_item].name;
			const std::wstring temporary = g_held_status_path + L".tmp";
			std::ofstream stream(temporary.c_str(), std::ios::binary | std::ios::trunc);
			if (!stream) return;
			stream << "{\n"
				<< "  \"active\": " << (g_active_tool != Tool::none ? "true" : "false") << ",\n"
				<< "  \"uuid\": \"" << json_escape(g_active_item_uuid.c_str()) << "\",\n"
				<< "  \"item\": \"" << json_escape(item_name) << "\",\n"
				<< "  \"profile\": \"" << json_escape(profile_name(profile)) << "\",\n"
				<< "  \"section\": \"";
			for (const wchar_t *p = profile_section(profile); *p; ++p)
				stream << static_cast<char>(*p);
			stream << "\",\n"
				<< "  \"catalog\": " << (g_active_tool == Tool::catalog ? "true" : "false") << "\n"
				<< "}\n";
			stream.close();
			MoveFileExW(temporary.c_str(), g_held_status_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}

		void apply_player_state(Tool tool, ItemVariant variant, int catalog_item, const std::string &item_uuid,
			bool seated, bool first_person)
		{
			if (tool != g_active_tool || variant != g_active_variant || catalog_item != g_active_catalog_item ||
				item_uuid != g_active_item_uuid)
			{
				g_active_tool = tool;
				g_active_variant = variant;
				g_active_catalog_item = catalog_item;
				g_active_item_uuid = item_uuid;
				if (g_log)
				{
					if (tool == Tool::catalog && catalog_item >= 0)
						g_log("VR TOOL ACTIVE: %s [%s]", held_item_catalog::items[catalog_item].name,
							profile_name(active_profile()));
					else g_log("VR TOOL ACTIVE: %s [%s]", tool_name(tool), profile_name(active_profile()));
				}
				write_held_status();
			}
			if (seated != g_player_seated)
			{
				g_player_seated = seated;
				if (g_log) g_log("VR SEAT INPUT MODE: %s", seated ? "zoom X/C" : "hotbar X/Y");
			}
			if (first_person != g_player_first_person)
			{
				g_player_first_person = first_person;
				if (g_log) g_log("VR CAMERA VIEW: %s", first_person ? "first person" : "third person");
			}
		}

		void poll_active_tool()
		{
			const ULONGLONG now = GetTickCount64();
			if (now - g_last_poll < 75) return;
			g_last_poll = now;
			std::wstring state_path;
			bool custom_content = false;
			const bool source_available = custom_content_bridge::select_player_state_path(
				state_path, custom_content);
			HANDLE file = source_available ? CreateFileW(state_path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
			bool new_packet = false;
			if (file != INVALID_HANDLE_VALUE)
			{
				// Waypoint-bearing packets are still tiny, but a raid can expose many
				// markers. Read a bounded 8 KiB packet so the JSON cannot be truncated
				// before the native parser reaches the final markers.
				char bytes[8192]{}; DWORD read = 0;
				const bool read_ok = ReadFile(file, bytes, sizeof(bytes) - 1, &read, nullptr) != FALSE;
				CloseHandle(file);
				if (read_ok && read > 0)
				{
					const std::string text(bytes, read);
					bool active = false, seated = false, first_person = false;
					uint64_t sequence = 0;
					std::string item, adapter;
					const bool source_changed = state_path != g_player_state_source_path;
					if (json_uint64(text, "sequence", sequence) &&
						(source_changed || !g_player_state_sequence_valid || sequence != g_player_state_sequence) &&
						json_bool(text, "active", active))
					{
						if (!active)
						{
							apply_player_state(Tool::none, ItemVariant::none, -1, {}, false, false);
							g_wrist_hud_state = WristHudState{};
							new_packet = true;
						}
						else if (json_bool(text, "seated", seated) && json_bool(text, "firstPerson", first_person) &&
							json_string(text, "activeItem", item))
						{
							json_string(text, "activeAdapter", adapter);
							ItemVariant variant = ItemVariant::none;
							Tool tool = parse_tool(item, {}, variant);
							int catalog_item = -1;
							if (tool == Tool::none)
							{
								catalog_item = find_catalog_item(item);
								if (catalog_item >= 0) tool = Tool::catalog;
								else tool = parse_tool(item, adapter, variant);
							}
							apply_player_state(tool, variant, catalog_item, item, seated, first_person);
							// The fields are optional for compatibility with older worlds and
							// custom modes.  A packet without them simply hides the wrist
							// panels instead of showing stale values from a previous world.
							double health = -1.0, max_health = -1.0, breath = -1.0, max_breath = -1.0, time_minutes = -1.0;
							std::array<WristHudWaypoint, kMaxWristHudWaypoints> waypoints{};
							uint32_t waypoint_count = 0;
							json_waypoints(text, waypoints, waypoint_count);
							bool conscious = true;
							const bool health_valid = json_number(text, "health", health) &&
								json_number(text, "maxHealth", max_health) &&
								std::isfinite(health) && std::isfinite(max_health) &&
								max_health > 0.0 &&
								health >= -1.0 && health <= max_health + 1.0;
							const bool time_valid = json_number(text, "timeMinutes", time_minutes) &&
								std::isfinite(time_minutes) && time_minutes >= 0.0 && time_minutes < 1440.0;
							const bool breath_valid = json_number(text, "breath", breath) &&
								json_number(text, "maxBreath", max_breath) &&
								std::isfinite(breath) && std::isfinite(max_breath) &&
								max_breath > 0.0 && breath >= -1.0 && breath <= max_breath + 1.0;
							const bool has_conscious = json_bool(text, "conscious", conscious);
							if (has_conscious && (health_valid || time_valid))
							{
								g_wrist_hud_state.active = true;
								g_wrist_hud_state.conscious = conscious;
								g_wrist_hud_state.health = health_valid
									? static_cast<float>(std::max(0.0, health)) : 0.0f;
								g_wrist_hud_state.max_health = health_valid
									? static_cast<float>(max_health) : 0.0f;
								g_wrist_hud_state.breath = breath_valid
									? static_cast<float>(std::max(0.0, breath)) : 0.0f;
								g_wrist_hud_state.max_breath = breath_valid
									? static_cast<float>(max_breath) : 0.0f;
								g_wrist_hud_state.time_minutes = time_valid
									? static_cast<uint32_t>(time_minutes + 0.5) : 0;
								g_wrist_hud_state.waypoints = waypoints;
								g_wrist_hud_state.waypoint_count = waypoint_count;
							}
							else
								g_wrist_hud_state = WristHudState{};
							new_packet = true;
						}
						if (new_packet)
						{
							g_player_state_sequence = sequence;
							g_player_state_sequence_valid = true;
							g_player_state_last_valid_ms = now;
							g_player_state_source_path = state_path;
							g_player_state_source_custom = custom_content;
							if (custom_content) custom_content_bridge::mirror_world_state(active);
						}
					}
				}
			}
			if (g_player_state_last_valid_ms == 0 || now - g_player_state_last_valid_ms > 1000)
			{
				apply_player_state(Tool::none, ItemVariant::none, -1, {}, false, false);
				g_wrist_hud_state = WristHudState{};
				if (g_player_state_source_custom)
				{
					custom_content_bridge::mirror_world_state(false);
					custom_content_bridge::clear_custom_source();
					g_player_state_source_path.clear();
					g_player_state_source_custom = false;
					g_player_state_sequence_valid = false;
				}
			}
		}

		const PoseCalibration &pose_for_active()
		{
			return g_pose_calibrations[static_cast<size_t>(active_profile())];
		}

		Matrix orientation_for_pose(const PoseCalibration &pose)
		{
			constexpr float radians = 0.01745329251994329577f;
			return multiply(rotation_x(pose.pitch * radians),
				multiply(rotation_y(pose.yaw * radians), rotation_z(pose.roll * radians)));
		}

		XrVector3f transform_direction(const Matrix &matrix, const XrVector3f &value)
		{
			return {
				matrix.m[0] * value.x + matrix.m[4] * value.y + matrix.m[8] * value.z,
				matrix.m[1] * value.x + matrix.m[5] * value.y + matrix.m[9] * value.z,
				matrix.m[2] * value.x + matrix.m[6] * value.y + matrix.m[10] * value.z
			};
		}

		XrVector3f adjusted_pointer_offset(Tool tool, const XrVector3f &original)
		{
			const HeldProfile profile = profile_for_tool(tool, g_active_variant, g_active_catalog_item);
			const PoseCalibration base = default_pose(profile);
			const PoseCalibration &pose = g_pose_calibrations[static_cast<size_t>(profile)];
			const float ratio = base.scale > 0.00001f ? pose.scale / base.scale : 1.0f;
			XrVector3f relative = {
				(original.x - base.x) * ratio,
				(original.y - base.y) * ratio,
				(original.z - base.z) * ratio
			};
			relative = transform_direction(orientation_for_pose(pose), relative);
			return { pose.x + relative.x, pose.y + relative.y, pose.z + relative.z };
		}

		XrVector3f adjusted_gun_direction()
		{
			return transform_direction(orientation_for_pose(pose_for_active()), { 0.0f, 0.0f, -1.0f });
		}

		held_item_asset::MeshRange food_range(ItemVariant variant)
		{
			using namespace held_item_asset;
			switch (variant)
			{
			case ItemVariant::food_sunshake: return food_sunshake;
			case ItemVariant::food_milk: return food_milk;
			case ItemVariant::food_carrotburger: return food_carrotburger;
			case ItemVariant::food_pizzaburger: return food_pizzaburger;
			case ItemVariant::food_banana: return food_banana;
			case ItemVariant::food_blueberry: return food_blueberry;
			case ItemVariant::food_orange: return food_orange;
			case ItemVariant::food_pineapple: return food_pineapple;
			case ItemVariant::food_carrot: return food_carrot;
			case ItemVariant::food_redbeet: return food_redbeet;
			case ItemVariant::food_tomato: return food_tomato;
			case ItemVariant::food_broccoli: return food_broccoli;
			case ItemVariant::food_corn: return food_corn;
			case ItemVariant::food_tea: return food_tea;
			case ItemVariant::food_chili: return food_chili;
			default: return food_carrot;
			}
		}
	}

	bool initialize(ID3D11Device *device, LogFunction log)
	{
		if (g_initialized) return true;
		g_device = device; g_log = log; g_game_root = module_root(); if (!g_device || g_game_root.empty()) return false;
		g_clay_calibration_path = g_game_root + L"\\Release\\ScrapMechanicVR-ClayCalibration.ini";
		g_held_calibration_path = g_game_root + L"\\Release\\ScrapMechanicVR-HeldCalibration.ini";
		g_held_catalog_path = g_game_root + L"\\Release\\ScrapMechanicVR-HeldItems.bin";
		g_held_status_path = g_game_root + L"\\Data\\NativeVR\\held_item_status.json";
		CreateDirectoryW((g_game_root + L"\\Data\\NativeVR").c_str(), nullptr);
		custom_content_bridge::initialize(g_game_root, g_log);
		reset_pose_defaults();
		poll_clay_calibration();
		poll_held_calibration();
		const char *shader = R"(
			cbuffer ToolConstants : register(b0) { float4x4 mvp; float4x4 model; float4 eye_position; };
			struct VSIn { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
			struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float3 world_normal : TEXCOORD1; float3 world_position : TEXCOORD2; };
			VSOut vs_main(VSIn input) { VSOut o; float4 local = float4(input.position, 1); float4 world = mul(model, local);
				o.position = mul(mvp, local); o.uv = input.uv; o.world_position = world.xyz;
				o.world_normal = normalize(mul((float3x3)model, input.normal)); return o; }
			Texture2D tex : register(t0); SamplerState samp : register(s0);
			float4 shade(VSOut input, float4 c) { float3 n = normalize(input.world_normal);
				float3 key_direction = normalize(float3(-0.35, 0.78, -0.52)); float sky = 0.5 + 0.5 * n.y;
				float ambient = lerp(0.32, 0.48, sky); float key = 0.38 * saturate(dot(n, key_direction));
				float fill = 0.06 * saturate(dot(n, normalize(float3(0.65, 0.25, 0.72))));
				float3 view_direction = normalize(eye_position.xyz - input.world_position);
				float specular = 0.035 * pow(saturate(dot(n, normalize(key_direction + view_direction))), 30.0);
				float3 linear_lit = c.rgb * (ambient + key + fill) + specular;
				return float4(saturate((linear_lit - 0.18) * 1.08 + 0.16), c.a); }
			float4 ps_main(VSOut input) : SV_TARGET { return shade(input, tex.Sample(samp, input.uv)); }
			float4 ps_cutout(VSOut input) : SV_TARGET { float4 c = tex.Sample(samp, input.uv);
				clip(c.a - 0.08); return shade(input, c); }
			float4 ps_laser(VSOut input) : SV_TARGET { return float4(1, 1, 1, 1); }
			float4 ps_target(VSOut input) : SV_TARGET { return float4(1.0, 0.69, 0.08, 1.0); }
		)";
		HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
		using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
		auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
		ID3DBlob *vs = nullptr, *ps = nullptr, *cutout_ps = nullptr, *laser_ps = nullptr,
			*target_ps = nullptr, *errors = nullptr;
		if (!compile || FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs, &errors))) { release(errors); if (compiler) FreeLibrary(compiler); return false; }
		release(errors);
		if (FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps, &errors)) ||
			FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_cutout", "ps_5_0", 0, 0, &cutout_ps, &errors)) ||
			FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_laser", "ps_5_0", 0, 0, &laser_ps, &errors)) ||
			FAILED(compile(shader, std::strlen(shader), "vr_tools", nullptr, nullptr, "ps_target", "ps_5_0", 0, 0, &target_ps, &errors)))
		{ release(errors); release(vs); release(ps); release(cutout_ps); release(laser_ps); release(target_ps); if (compiler) FreeLibrary(compiler); return false; }
		if (compiler) FreeLibrary(compiler);
		if (FAILED(g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vertex_shader)) ||
			FAILED(g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_pixel_shader)) ||
			FAILED(g_device->CreatePixelShader(cutout_ps->GetBufferPointer(), cutout_ps->GetBufferSize(), nullptr, &g_cutout_pixel_shader)) ||
			FAILED(g_device->CreatePixelShader(laser_ps->GetBufferPointer(), laser_ps->GetBufferSize(), nullptr, &g_laser_pixel_shader)) ||
			FAILED(g_device->CreatePixelShader(target_ps->GetBufferPointer(), target_ps->GetBufferSize(), nullptr, &g_target_pixel_shader))) return false;
		D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		if (FAILED(g_device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &g_input_layout))) return false;
		release(vs); release(ps); release(cutout_ps); release(laser_ps); release(target_ps); release(errors);
		D3D11_BUFFER_DESC cb = {}; cb.ByteWidth = sizeof(Constants); cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_constant_buffer))) return false;
		// One shared dynamic line buffer holds either the two-vertex tool pointer
		// or the compact hand-aim ring (16 segments plus a center cross).
		D3D11_BUFFER_DESC lb = {}; lb.ByteWidth = 36 * sizeof(Vertex); lb.Usage = D3D11_USAGE_DYNAMIC; lb.BindFlags = D3D11_BIND_VERTEX_BUFFER; lb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(g_device->CreateBuffer(&lb, nullptr, &g_laser_buffer))) return false;
		D3D11_SAMPLER_DESC sampler = {}; sampler.Filter = D3D11_FILTER_ANISOTROPIC; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxAnisotropy = 8; sampler.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(g_device->CreateSamplerState(&sampler, &g_sampler))) return false;
		D3D11_RASTERIZER_DESC raster = {}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
		if (FAILED(g_device->CreateRasterizerState(&raster, &g_rasterizer))) return false;
		D3D11_DEPTH_STENCIL_DESC depth = {}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(g_device->CreateDepthStencilState(&depth, &g_depth_state))) return false;
		D3D11_DEPTH_STENCIL_DESC glass_depth = depth; glass_depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		if (FAILED(g_device->CreateDepthStencilState(&glass_depth, &g_glass_depth_state))) return false;
		D3D11_BLEND_DESC blend = {};
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(g_device->CreateBlendState(&blend, &g_alpha_blend_state))) return false;

		using namespace native_tool_asset;
		using namespace chapter2_tool_asset;
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
			!create_draw(gatling_barrel, gatling_barrel_0_barrel_spinner_mat_vertices, gatling_barrel_0_barrel_spinner_mat_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_spinner\\char_spudgun_barrel_spinner_dif.tga").c_str()) ||
			!create_draw(scrap_barrel, scrap_barrel_0_m_barrel_scrap_vertices, scrap_barrel_0_m_barrel_scrap_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_scrap\\char_spudgun_barrel_scrap_dif.tga").c_str()) ||
			!create_draw(launcher_barrel, launcher_barrel_0_barrel_launcher_vertices, launcher_barrel_0_barrel_launcher_vertex_count, path(L"Char_spudgun\\Barrel\\Barrel_launcher\\char_spudgun_barrel_launcher_dif.tga").c_str()) ||
			!create_draw(clay_body, clay_body_vertices, clay_body_vertex_count, path(L"Char_claygun\\char_claygun_dif.tga").c_str()) ||
			!create_draw(clay_wheel, clay_wheel_vertices, clay_wheel_vertex_count, path(L"Char_claygun\\char_claygun_dif.tga").c_str()) ||
			!create_solid_draw(clay_container_fill, clay_container_fill_vertices, clay_container_fill_vertex_count, 107, 107, 107) ||
			!create_solid_draw(clay_container_glass, clay_container_glass_vertices, clay_container_glass_vertex_count, 18, 99, 137) ||
			!create_draw(clay_grip, clay_grip_vertices, clay_grip_vertex_count, path(L"Char_spudgun\\Base\\char_spudgun_grip_dif.tga").c_str()))
		{ if (g_log) g_log("VR TOOL RENDERER: a native mesh or texture resource failed to initialize"); return false; }
		for (unsigned int index = 0; index < held_item_asset::mesh_count; ++index)
		{
			const auto &source = held_item_asset::meshes[index];
			if (!create_resource(g_held_draws[index], source.vertices, source.count, source.texture, source.rgba))
			{
				if (g_log) g_log("VR HELD ITEM RENDERER: draw resource %u failed to initialize", index);
				return false;
			}
		}
		g_initialized = true;
		write_held_status();
		if (g_log) g_log("VR TOOL RENDERER READY: dedicated tools plus %u catalogued blocks and parts use grouped live calibration",
			held_item_catalog::item_count);
		return true;
	}

	bool render(ID3D11DeviceContext *context, ID3D11RenderTargetView *target, ID3D11DepthStencilView *depth,
		uint32_t width, uint32_t height, const XrView &eye, const XrPosef &right_hand_pose,
		bool right_hand_active, bool right_firing, const XrPosef &right_aim_pose,
		bool right_aim_active, float right_target_distance, bool right_target_active,
		float interaction_target_distance, bool interaction_target_active)
	{
		if (!g_initialized || !context || !target || !depth) return false;
		poll_active_tool();
		const bool target_marker = right_aim_active && right_target_active &&
			std::isfinite(right_target_distance) && right_target_distance >= 0.05f &&
			!has_projectile_aim(g_active_tool) &&
			g_active_tool != Tool::connect && g_active_tool != Tool::paint &&
			g_active_tool != Tool::weld;
		if (g_render_suppressed || !right_hand_active ||
			(g_active_tool == Tool::none && !target_marker)) return false;
		poll_held_calibration();
		if (g_active_tool == Tool::clay) poll_clay_calibration();
		if (g_active_tool == Tool::catalog && !load_catalog_item(g_active_catalog_item)) return false;
		context->OMSetRenderTargets(1, &target, depth);
		D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
		context->RSSetViewports(1, &viewport); context->RSSetState(g_rasterizer); context->OMSetDepthStencilState(g_depth_state, 0);
		context->IASetInputLayout(g_input_layout); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(g_vertex_shader, nullptr, 0); context->VSSetConstantBuffers(0, 1, &g_constant_buffer);
		context->PSSetShader(g_pixel_shader, nullptr, 0); context->PSSetSamplers(0, 1, &g_sampler);
		const Matrix view_projection = multiply(projection(eye.fov), inverse_pose(eye.pose));
		const auto &calibration = calibration_for(g_active_tool);
		const PoseCalibration &pose = pose_for_active();
		const Matrix local = multiply(translation(pose.x, pose.y, pose.z),
			multiply(orientation_for_pose(pose), multiply(tool_basis(), uniform_scale(pose.scale))));
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
			case Tool::lift: draw_held_range(context, held_item_asset::lift); break;
			case Tool::handbook: draw_held_range(context, held_item_asset::handbook); break;
			case Tool::bucket:
				draw_held_range(context, held_item_asset::bucket);
				if (g_active_variant == ItemVariant::bucket_water) draw_held_range(context, held_item_asset::bucket_water);
				else if (g_active_variant == ItemVariant::bucket_oil) draw_held_range(context, held_item_asset::bucket_oil);
				else if (g_active_variant == ItemVariant::bucket_chemical) draw_held_range(context, held_item_asset::bucket_chemical);
				break;
			case Tool::glowstick: draw_held_range(context, held_item_asset::glowstick); break;
			case Tool::cornade: draw_held_range(context, held_item_asset::cornade); break;
			case Tool::loose_clay: draw_held_range(context, held_item_asset::loose_clay); break;
			case Tool::extinguisher: draw_held_range(context, held_item_asset::extinguisher); break;
			case Tool::planter: draw_held_range(context, held_item_asset::planter); break;
			case Tool::fertilizer: draw_held_range(context, held_item_asset::fertilizer); break;
			case Tool::food: draw_held_range(context, food_range(g_active_variant)); break;
			case Tool::feeder: draw_held_range(context, held_item_asset::feeder); break;
			case Tool::soilbag: draw_held_range(context, held_item_asset::soilbag); break;
			case Tool::key:
				draw_held_range(context, g_active_variant == ItemVariant::powercore ?
					held_item_asset::powercore : held_item_asset::keycard);
				break;
			case Tool::resource: draw_held_range(context, held_item_asset::resource); break;
			case Tool::carry: draw_held_range(context, held_item_asset::carry); break;
			case Tool::logbook: draw_held_range(context, held_item_asset::logbook); break;
			case Tool::catalog: draw_catalog_item(context); break;
			case Tool::clay:
			{
				update_spinner_animation(right_firing, true);
				draw_resource(context, clay_body);
				draw_resource(context, clay_grip);

				constexpr float radians = 0.01745329251994329577f;
				const float container_angle = g_gatling_angle * g_clay_calibration.container_speed +
					g_clay_calibration.container_phase * radians;
				const Matrix container_spin = multiply(
					translation(g_clay_calibration.container_pivot_x, g_clay_calibration.container_pivot_y, g_clay_calibration.container_pivot_z),
					multiply(rotation_axis(g_clay_calibration.container_axis_x, g_clay_calibration.container_axis_y,
						g_clay_calibration.container_axis_z, container_angle),
						translation(-g_clay_calibration.container_pivot_x, -g_clay_calibration.container_pivot_y,
							-g_clay_calibration.container_pivot_z)));
				const Matrix container_model = multiply(model, container_spin);
				Constants container_constants = { multiply(view_projection, container_model), container_model,
					{ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f } };
				context->UpdateSubresource(g_constant_buffer, 0, nullptr, &container_constants, 0, 0);
				draw_resource(context, clay_container_fill);
				draw_resource(context, clay_container_glass);

				const float wheel_angle = g_gatling_angle * g_clay_calibration.wheel_speed +
					g_clay_calibration.wheel_phase * radians;
				const Matrix wheel_spin = multiply(
					translation(g_clay_calibration.wheel_pivot_x, g_clay_calibration.wheel_pivot_y, g_clay_calibration.wheel_pivot_z),
					multiply(rotation_axis(g_clay_calibration.wheel_axis_x, g_clay_calibration.wheel_axis_y,
						g_clay_calibration.wheel_axis_z, wheel_angle),
						translation(-g_clay_calibration.wheel_pivot_x, -g_clay_calibration.wheel_pivot_y,
							-g_clay_calibration.wheel_pivot_z)));
				const Matrix wheel_model = multiply(model, wheel_spin);
				Constants wheel_constants = { multiply(view_projection, wheel_model), wheel_model,
					{ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f } };
				context->UpdateSubresource(g_constant_buffer, 0, nullptr, &wheel_constants, 0, 0);
				draw_resource(context, clay_wheel);
				break;
			}
			case Tool::rifle: case Tool::shotgun: case Tool::gatling: case Tool::scrap: case Tool::launcher:
				draw_resource(context, gun_grip); draw_resource(context, gun_body); draw_resource(context, gun_sight_screw);
				draw_resource(context, gun_sight); draw_resource(context, gun_stock); draw_resource(context, gun_tank);
				if (g_active_tool == Tool::rifle) draw_resource(context, rifle_barrel);
				else if (g_active_tool == Tool::scrap) draw_resource(context, scrap_barrel);
				else if (g_active_tool == Tool::launcher) draw_resource(context, launcher_barrel);
				else if (g_active_tool == Tool::shotgun) { draw_resource(context, shotgun_barrel); draw_resource(context, shotgun_oil); }
				else
				{
					update_spinner_animation(right_firing, false);
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

		if (g_active_tool == Tool::connect || g_active_tool == Tool::paint ||
			g_active_tool == Tool::weld)
		{
			const XrVector3f laser_origin = adjusted_pointer_offset(g_active_tool,
				{ calibration.laser_x, calibration.laser_y, calibration.laser_z });
			const XrVector3f laser_direction = transform_direction(orientation_for_pose(pose), { 0.0f, 0.0f, -1.0f });
			const float laser_length = interaction_target_active &&
				std::isfinite(interaction_target_distance)
				? std::clamp(interaction_target_distance, 0.05f, 8.0f)
				: 8.0f;
			Vertex laser[2] = {
				{ laser_origin.x, laser_origin.y, laser_origin.z, 0, 0, 1, 0, 0 },
				{ laser_origin.x + laser_direction.x * laser_length,
					laser_origin.y + laser_direction.y * laser_length,
					laser_origin.z + laser_direction.z * laser_length, 0, 0, 1, 0, 0 }
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
		if (target_marker)
		{
			// The ring sits on Scrap Mechanic's actual world-ray hit. Its physical
			// size scales with distance so it remains visually stable, and it stays
			// a point marker rather than turning world interaction into a laser beam.
			constexpr uint32_t segments = 16;
			constexpr float pi = 3.14159265358979323846f;
			const float distance = std::clamp(right_target_distance, 0.05f, 20.0f);
			const float marker_depth = std::max(0.04f, distance - 0.012f);
			const float radius = distance * 0.010f;
			const float cross_radius = distance * 0.0033f;
			Vertex marker[segments * 2 + 4]{};
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const float a0 = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
				const float a1 = 2.0f * pi * static_cast<float>(segment + 1) / static_cast<float>(segments);
				marker[segment * 2] = {std::cos(a0) * radius, std::sin(a0) * radius,
					-marker_depth, 0, 0, 1, 0, 0};
				marker[segment * 2 + 1] = {std::cos(a1) * radius, std::sin(a1) * radius,
					-marker_depth, 0, 0, 1, 0, 0};
			}
			marker[32] = {-cross_radius, 0, -marker_depth, 0, 0, 1, 0, 0};
			marker[33] = { cross_radius, 0, -marker_depth, 0, 0, 1, 0, 0};
			marker[34] = {0, -cross_radius, -marker_depth, 0, 0, 1, 0, 0};
			marker[35] = {0,  cross_radius, -marker_depth, 0, 0, 1, 0, 0};
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(g_laser_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, marker, sizeof(marker));
				context->Unmap(g_laser_buffer, 0);
				const Matrix marker_model = pose_matrix(right_aim_pose);
				Constants marker_constants = {multiply(view_projection, marker_model), marker_model,
					{eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f}};
				context->UpdateSubresource(g_constant_buffer, 0, nullptr, &marker_constants, 0, 0);
				UINT stride = sizeof(Vertex), offset = 0;
				context->IASetVertexBuffers(0, 1, &g_laser_buffer, &stride, &offset);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
				context->PSSetShader(g_target_pixel_shader, nullptr, 0);
				context->Draw(static_cast<UINT>(std::size(marker)), 0);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->PSSetShader(g_pixel_shader, nullptr, 0);
			}
		}
		ID3D11ShaderResourceView *none = nullptr; context->PSSetShaderResources(0, 1, &none);
		if (!g_render_logged && g_log) { g_render_logged = true; g_log("NATIVE VR TOOLS VISIBLE: selected tool uses the tracked-hand stereo pose and depth buffer; world targeting uses a laser-free OpenXR aim marker; white pointers are limited to interaction tools"); }
		return true;
	}

	bool get_interaction_laser_offset(XrVector3f &offset, XrVector3f *local_direction,
		InteractionLaserKind *kind)
	{
		poll_active_tool();
		poll_held_calibration();
		if (g_active_tool != Tool::connect && g_active_tool != Tool::paint &&
			g_active_tool != Tool::weld)
		{
			if (kind) *kind = InteractionLaserKind::none;
			return false;
		}
		const auto &calibration = calibration_for(g_active_tool);
		offset = adjusted_pointer_offset(g_active_tool,
			{ calibration.laser_x, calibration.laser_y, calibration.laser_z });
		if (local_direction) *local_direction = transform_direction(
			orientation_for_pose(pose_for_active()), { 0.0f, 0.0f, -1.0f });
		if (kind) *kind = g_active_tool == Tool::connect
			? InteractionLaserKind::connection : InteractionLaserKind::surface;
		return true;
	}

	bool get_gun_muzzle_offset(XrVector3f &offset, XrVector3f &local_direction, const char *&item_uuid)
	{
		poll_active_tool();
		poll_held_calibration();
		if (!has_projectile_aim(g_active_tool))
		{
			item_uuid = nullptr;
			return false;
		}
		const auto &calibration = calibration_for(g_active_tool);
		offset = adjusted_pointer_offset(g_active_tool,
			{ calibration.laser_x, calibration.laser_y, calibration.laser_z });
		local_direction = adjusted_gun_direction();
		switch (g_active_tool)
		{
		case Tool::rifle: item_uuid = "c5ea0c2f-185b-48d6-b4df-45c386a575cc"; break;
		case Tool::shotgun: item_uuid = "f6250bf4-9726-406f-a29a-945c06e460e5"; break;
		case Tool::gatling: item_uuid = "9fde0601-c2ba-4c70-8d5c-2a7a9fdd122b"; break;
		case Tool::scrap: item_uuid = "d51ec758-057b-4263-bd16-7a731e149480"; break;
		case Tool::launcher: item_uuid = "a2a2bb33-a841-4b23-88da-b758063d9206"; break;
		case Tool::clay: item_uuid = "6993e5df-6852-4e84-88ae-df49f765e784"; break;
		case Tool::glowstick: item_uuid = "3a3280e4-03b6-4a4d-9e02-e348478213c9"; break;
		case Tool::cornade: item_uuid = "e3bdeea5-d349-4d08-9b5a-5695ea05537e"; break;
		case Tool::extinguisher: item_uuid = "d2fab7ef-21db-4681-a22a-cd4f278fc355"; break;
		default: item_uuid = nullptr; return false;
		}
		return true;
	}

	bool is_hammer_active()
	{
		return g_active_tool == Tool::hammer;
	}

	HapticProfile active_haptic_profile()
	{
		poll_active_tool();
		if (g_active_tool == Tool::hammer) return HapticProfile::hammer;
		if (is_gun(g_active_tool)) return HapticProfile::gun;
		if (g_active_tool != Tool::none) return HapticProfile::tool;
		return HapticProfile::none;
	}

	ContextAction active_context_action()
	{
		poll_active_tool();
		if (g_player_seated) return ContextAction::none;
		if (g_active_tool == Tool::paint) return ContextAction::paint_palette;
		if (g_active_tool == Tool::catalog) return ContextAction::rotate_placement;
		return ContextAction::none;
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

	WristHudState wrist_hud_state()
	{
		poll_active_tool();
		return g_wrist_hud_state;
	}

	void set_render_suppressed(bool suppressed)
	{
		g_render_suppressed = suppressed;
	}

	void shutdown()
	{
		for (auto &draw : g_draws) { release(draw.vertices); release(draw.texture); draw.count = 0; }
		for (auto &draw : g_held_draws) { release(draw.vertices); release(draw.texture); draw.count = 0; }
		release_catalog_draws();
		release(g_alpha_blend_state); release(g_glass_depth_state); release(g_depth_state);
		release(g_rasterizer); release(g_sampler); release(g_input_layout);
		release(g_target_pixel_shader); release(g_laser_pixel_shader); release(g_cutout_pixel_shader); release(g_pixel_shader);
		release(g_vertex_shader); release(g_laser_buffer); release(g_constant_buffer);
		g_device = nullptr; g_log = nullptr; g_game_root.clear(); g_active_tool = Tool::none;
		g_active_variant = ItemVariant::none; g_active_catalog_item = -1; g_active_item_uuid.clear();
		g_clay_calibration = ClayCalibration{}; g_clay_calibration_path.clear();
		g_clay_calibration_poll_ms = 0; g_clay_calibration_write_time = {}; g_clay_calibration_loaded = false;
		reset_pose_defaults(); g_held_calibration_path.clear(); g_held_catalog_path.clear(); g_held_status_path.clear();
		g_held_calibration_poll_ms = 0; g_held_calibration_write_time = {}; g_held_calibration_loaded = false;
		g_player_seated = false; g_player_first_person = false;
		g_wrist_hud_state = WristHudState{};
		g_render_suppressed = false; g_last_poll = 0; g_player_state_last_valid_ms = 0;
		g_player_state_sequence = 0; g_player_state_sequence_valid = false;
		g_player_state_source_path.clear(); g_player_state_source_custom = false;
		custom_content_bridge::shutdown();
		g_gatling_animation_ms = 0; g_gatling_angle = 0.0f; g_gatling_speed = 0.0f; g_gatling_spin_logged = false;
		g_initialized = false; g_render_logged = false;
	}
}
