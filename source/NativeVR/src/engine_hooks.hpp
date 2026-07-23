#pragma once

namespace engine_hooks
{
	using LogFunction = void (*)(const char *format, ...);

	bool install(LogFunction log);
	void on_present();
	void uninstall();
	bool is_installed();
	bool suppress_viewmodel_draw();
}
