#include "custom_content_bridge.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace scrapvr::custom_content_bridge
{
	namespace
	{
		constexpr wchar_t kPlayerStateName[] = L"ScrapMechanicVR_player_state.json";
		constexpr wchar_t kHandStateName[] = L"ScrapMechanicVR_hand_physics.json";
		constexpr wchar_t kHandTemporaryName[] = L"ScrapMechanicVR_hand_physics.tmp";
		constexpr ULONGLONG kFreshFileAge100ns = 10'000'000ull;
		constexpr ULONGLONG kRootRefreshMilliseconds = 30'000ull;
		constexpr ULONGLONG kCustomScanMilliseconds = 500ull;

		std::mutex g_mutex;
		std::wstring g_game_root;
		std::wstring g_base_player_state_path;
		std::wstring g_base_world_state_path;
		std::wstring g_custom_player_state_path;
		std::wstring g_custom_root;
		std::wstring g_custom_hand_path;
		std::wstring g_custom_hand_temporary;
		std::wstring g_selected_path;
		std::vector<std::wstring> g_content_roots;
		LogFunction g_log = nullptr;
		ULONGLONG g_root_refresh_ms = 0;
		ULONGLONG g_custom_scan_ms = 0;
		bool g_selected_custom = false;
		bool g_world_state_known = false;
		bool g_world_state_active = false;

		ULONGLONG file_time_value(const FILETIME &value)
		{
			ULARGE_INTEGER converted{};
			converted.LowPart = value.dwLowDateTime;
			converted.HighPart = value.dwHighDateTime;
			return converted.QuadPart;
		}

		bool file_is_fresh(const std::wstring &path, ULONGLONG &write_time)
		{
			WIN32_FILE_ATTRIBUTE_DATA attributes{};
			if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
				(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				return false;
			FILETIME current{};
			GetSystemTimeAsFileTime(&current);
			const ULONGLONG now = file_time_value(current);
			write_time = file_time_value(attributes.ftLastWriteTime);
			if (write_time > now) return write_time - now <= kFreshFileAge100ns;
			return now - write_time <= kFreshFileAge100ns;
		}

		void add_directory_children(const std::filesystem::path &parent)
		{
			std::error_code error;
			if (!std::filesystem::is_directory(parent, error)) return;
			for (std::filesystem::directory_iterator iterator(parent,
				std::filesystem::directory_options::skip_permission_denied, error), end;
				iterator != end; iterator.increment(error))
			{
				if (error) { error.clear(); continue; }
				if (!iterator->is_directory(error)) { error.clear(); continue; }
				const std::wstring root = iterator->path().wstring();
				if (std::find(g_content_roots.begin(), g_content_roots.end(), root) == g_content_roots.end())
					g_content_roots.push_back(root);
			}
		}

		void refresh_content_roots(ULONGLONG now)
		{
			if (g_root_refresh_ms && now - g_root_refresh_ms < kRootRefreshMilliseconds) return;
			g_root_refresh_ms = now;
			g_content_roots.clear();

			wchar_t appdata[MAX_PATH]{};
			if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0)
			{
				const std::filesystem::path users = std::filesystem::path(appdata) /
					L"Axolot Games" / L"Scrap Mechanic" / L"User";
				std::error_code error;
				if (std::filesystem::is_directory(users, error))
				{
					for (std::filesystem::directory_iterator iterator(users,
						std::filesystem::directory_options::skip_permission_denied, error), end;
						iterator != end; iterator.increment(error))
					{
						if (error) { error.clear(); continue; }
						if (iterator->is_directory(error)) add_directory_children(iterator->path() / L"Mods");
						error.clear();
					}
				}
			}

			std::error_code canonical_error;
			const std::filesystem::path workshop = std::filesystem::weakly_canonical(
				std::filesystem::path(g_game_root) / L".." / L".." / L"workshop" / L"content" / L"387990",
				canonical_error);
			if (!canonical_error) add_directory_children(workshop);
		}

		bool write_atomic(const std::wstring &path, const std::wstring &temporary,
			const char *bytes, size_t length)
		{
			if (length > MAXDWORD) return false;
			HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool wrote = WriteFile(file, bytes, static_cast<DWORD>(length), &written, nullptr) != FALSE &&
				written == static_cast<DWORD>(length);
			CloseHandle(file);
			if (!wrote)
			{
				DeleteFileW(temporary.c_str());
				return false;
			}
			if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(temporary.c_str());
				return false;
			}
			return true;
		}

		void set_custom_source_locked(const std::wstring &state_path)
		{
			const std::filesystem::path root = std::filesystem::path(state_path).parent_path();
			g_custom_player_state_path = state_path;
			g_custom_root = root.wstring();
			g_custom_hand_path = (root / kHandStateName).wstring();
			g_custom_hand_temporary = (root / kHandTemporaryName).wstring();
		}
	}

	void initialize(const std::wstring &game_root, LogFunction log)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_game_root = game_root;
		g_log = log;
		g_base_player_state_path = (std::filesystem::path(game_root) /
			L"Data" / L"NativeVR" / L"player_state.json").wstring();
		g_base_world_state_path = (std::filesystem::path(game_root) /
			L"Data" / L"NativeVR" / L"world_state.json").wstring();
		g_root_refresh_ms = 0;
		g_custom_scan_ms = 0;
		g_world_state_known = false;
		g_selected_path.clear();
		g_selected_custom = false;
	}

	bool select_player_state_path(std::wstring &path, bool &custom_content)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_game_root.empty()) return false;
		const ULONGLONG now = GetTickCount64();

		ULONGLONG base_write = 0;
		const bool base_fresh = file_is_fresh(g_base_player_state_path, base_write);
		ULONGLONG custom_write = 0;
		bool custom_fresh = !g_custom_player_state_path.empty() &&
			file_is_fresh(g_custom_player_state_path, custom_write);

		if ((!custom_fresh || g_custom_player_state_path.empty()) &&
			(!g_custom_scan_ms || now - g_custom_scan_ms >= kCustomScanMilliseconds))
		{
			g_custom_scan_ms = now;
			refresh_content_roots(now);
			std::wstring newest_path;
			ULONGLONG newest_write = 0;
			for (const std::wstring &root : g_content_roots)
			{
				const std::wstring candidate = (std::filesystem::path(root) / kPlayerStateName).wstring();
				ULONGLONG candidate_write = 0;
				if (file_is_fresh(candidate, candidate_write) && candidate_write >= newest_write)
				{
					newest_write = candidate_write;
					newest_path = candidate;
				}
			}
			if (!newest_path.empty())
			{
				set_custom_source_locked(newest_path);
				custom_fresh = true;
				custom_write = newest_write;
			}
		}

		bool selected_custom = false;
		std::wstring selected;
		if (base_fresh && (!custom_fresh || base_write >= custom_write))
		{
			selected = g_base_player_state_path;
		}
		else if (custom_fresh)
		{
			selected = g_custom_player_state_path;
			selected_custom = true;
		}
		else
		{
			return false;
		}

		if (selected != g_selected_path || selected_custom != g_selected_custom)
		{
			g_selected_path = selected;
			g_selected_custom = selected_custom;
			if (g_log)
				g_log(selected_custom ?
					"VR CUSTOM CONTENT BRIDGE ACTIVE: player state and hand physics use the caller's content root" :
					"VR PLAYER STATE BRIDGE ACTIVE: base game content root");
		}
		if (!selected_custom)
		{
			g_custom_player_state_path.clear();
			g_custom_root.clear();
			g_custom_hand_path.clear();
			g_custom_hand_temporary.clear();
		}
		path = selected;
		custom_content = selected_custom;
		return true;
	}

	bool get_hand_bridge_paths(wchar_t *path, size_t path_capacity,
		wchar_t *temporary, size_t temporary_capacity)
	{
		if (!path || !temporary || path_capacity == 0 || temporary_capacity == 0) return false;
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_custom_hand_path.empty() || g_custom_hand_temporary.empty()) return false;
		return wcsncpy_s(path, path_capacity, g_custom_hand_path.c_str(), _TRUNCATE) == 0 &&
			wcsncpy_s(temporary, temporary_capacity, g_custom_hand_temporary.c_str(), _TRUNCATE) == 0;
	}

	void mirror_world_state(bool active)
	{
		std::wstring path;
		bool should_write = false;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_base_world_state_path.empty()) return;
			if (!g_world_state_known || g_world_state_active != active)
			{
				g_world_state_known = true;
				g_world_state_active = active;
				path = g_base_world_state_path;
				should_write = true;
			}
		}
		if (!should_write) return;
		const std::wstring temporary = path + L".tmp";
		char json[160]{};
		const int length = std::snprintf(json, sizeof(json),
			"{\"version\":1,\"active\":%s,\"source\":\"custom-content-native-bridge\"}",
			active ? "true" : "false");
		if (length > 0 && static_cast<size_t>(length) < sizeof(json) &&
			write_atomic(path, temporary, json, static_cast<size_t>(length)) && g_log)
			g_log("VR CUSTOM CONTENT WORLD STATE: active=%u", active ? 1u : 0u);
	}

	void clear_custom_source()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_custom_player_state_path.clear();
		g_custom_root.clear();
		g_custom_hand_path.clear();
		g_custom_hand_temporary.clear();
		g_selected_path.clear();
		g_selected_custom = false;
		g_custom_scan_ms = 0;
	}

	void shutdown()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_game_root.clear();
		g_base_player_state_path.clear();
		g_base_world_state_path.clear();
		g_custom_player_state_path.clear();
		g_custom_root.clear();
		g_custom_hand_path.clear();
		g_custom_hand_temporary.clear();
		g_selected_path.clear();
		g_content_roots.clear();
		g_log = nullptr;
		g_root_refresh_ms = 0;
		g_custom_scan_ms = 0;
		g_selected_custom = false;
		g_world_state_known = false;
		g_world_state_active = false;
	}
}
