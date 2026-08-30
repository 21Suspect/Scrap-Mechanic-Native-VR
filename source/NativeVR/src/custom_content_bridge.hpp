#pragma once

#include <cstddef>
#include <string>

namespace scrapvr::custom_content_bridge
{
	using LogFunction = void (*)(const char *format, ...);

	void initialize(const std::wstring &game_root, LogFunction log);
	bool select_player_state_path(std::wstring &path, bool &custom_content);
	bool get_hand_bridge_paths(
		wchar_t *path,
		size_t path_capacity,
		wchar_t *temporary,
		size_t temporary_capacity);
	void mirror_world_state(bool active);
	void clear_custom_source();
	void shutdown();
}
