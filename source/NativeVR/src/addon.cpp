#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <reshade.hpp>
#include <openxr/openxr.h>
#include "vr_runtime.hpp"
#include "engine_hooks.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
	HMODULE g_module = nullptr;
	HMODULE g_openxr_loader = nullptr;
	XrInstance g_instance = XR_NULL_HANDLE;
	XrSystemId g_system_id = XR_NULL_SYSTEM_ID;
	SRWLOCK g_lock = SRWLOCK_INIT;
	volatile LONG g_initializing = 0;
	volatile LONG g_ready = 0;
	ULONGLONG g_last_attempt_ms = 0;
	ID3D11Device *g_d3d11_device = nullptr;
	volatile LONG64 g_draw_calls = 0;
	volatile LONG64 g_indexed_draw_calls = 0;
	bool g_draw_callbacks_logged = false;
	thread_local bool g_rebinding_eye_target = false;

	PFN_xrGetInstanceProcAddr p_xrGetInstanceProcAddr = nullptr;
	PFN_xrEnumerateInstanceExtensionProperties p_xrEnumerateInstanceExtensionProperties = nullptr;
	PFN_xrCreateInstance p_xrCreateInstance = nullptr;
	PFN_xrDestroyInstance p_xrDestroyInstance = nullptr;
	PFN_xrGetInstanceProperties p_xrGetInstanceProperties = nullptr;
	PFN_xrGetSystem p_xrGetSystem = nullptr;
	PFN_xrGetSystemProperties p_xrGetSystemProperties = nullptr;
	PFN_xrEnumerateViewConfigurationViews p_xrEnumerateViewConfigurationViews = nullptr;
	PFN_xrEnumerateEnvironmentBlendModes p_xrEnumerateEnvironmentBlendModes = nullptr;
	PFN_xrStringToPath p_xrStringToPath = nullptr;
	bool g_hand_tracking_extension_available = false;

	void get_log_path(char (&path)[MAX_PATH])
	{
		path[0] = '\0';
		GetModuleFileNameA(g_module, path, MAX_PATH);
		char *slash = std::strrchr(path, '\\');
		if (slash != nullptr)
			std::strcpy(slash + 1, "ScrapNativeVR.log");
		else
			std::strcpy(path, "ScrapNativeVR.log");
	}

	void log_line(const char *format, ...)
	{
		char line[2048] = {};
		SYSTEMTIME time = {};
		GetLocalTime(&time);
		int prefix = std::snprintf(
			line,
			sizeof(line),
			"%04u-%02u-%02u %02u:%02u:%02u.%03u ",
			time.wYear,
			time.wMonth,
			time.wDay,
			time.wHour,
			time.wMinute,
			time.wSecond,
			time.wMilliseconds);

		va_list args;
		va_start(args, format);
		int written = std::vsnprintf(line + prefix, sizeof(line) - static_cast<size_t>(prefix) - 3, format, args);
		va_end(args);
		if (written < 0)
			written = 0;

		size_t length = std::strlen(line);
		line[length++] = '\r';
		line[length++] = '\n';
		line[length] = '\0';

		char path[MAX_PATH] = {};
		get_log_path(path);
		AcquireSRWLockExclusive(&g_lock);
		HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE)
		{
			DWORD bytes_written = 0;
			WriteFile(file, line, static_cast<DWORD>(length), &bytes_written, nullptr);
			CloseHandle(file);
		}
		ReleaseSRWLockExclusive(&g_lock);
	}

	template <typename T>
	bool resolve_global(const char *name, T &target)
	{
		PFN_xrVoidFunction function = nullptr;
		const XrResult result = p_xrGetInstanceProcAddr(XR_NULL_HANDLE, name, &function);
		target = reinterpret_cast<T>(function);
		if (XR_FAILED(result) || target == nullptr)
		{
			log_line("OpenXR global resolve failed: %s result=%d", name, static_cast<int>(result));
			return false;
		}
		return true;
	}

	template <typename T>
	bool resolve_instance(const char *name, T &target)
	{
		PFN_xrVoidFunction function = nullptr;
		const XrResult result = p_xrGetInstanceProcAddr(g_instance, name, &function);
		target = reinterpret_cast<T>(function);
		if (XR_FAILED(result) || target == nullptr)
		{
			log_line("OpenXR instance resolve failed: %s result=%d", name, static_cast<int>(result));
			return false;
		}
		return true;
	}

	bool has_d3d11_extension()
	{
		uint32_t count = 0;
		XrResult result = p_xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
		if (XR_FAILED(result) || count == 0)
		{
			log_line("xrEnumerateInstanceExtensionProperties(count) failed: result=%d count=%u", static_cast<int>(result), count);
			return false;
		}

		XrExtensionProperties properties[128] = {};
		if (count > 128)
			count = 128;
		for (uint32_t i = 0; i < count; ++i)
			properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		result = p_xrEnumerateInstanceExtensionProperties(nullptr, count, &count, properties);
		if (XR_FAILED(result))
		{
			log_line("xrEnumerateInstanceExtensionProperties(data) failed: result=%d", static_cast<int>(result));
			return false;
		}

		bool d3d11_available = false;
		g_hand_tracking_extension_available = false;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (std::strcmp(properties[i].extensionName, "XR_KHR_D3D11_enable") == 0)
			{
				log_line("OpenXR extension available: XR_KHR_D3D11_enable version=%u", properties[i].extensionVersion);
				d3d11_available = true;
			}
			else if (std::strcmp(properties[i].extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0)
			{
				g_hand_tracking_extension_available = true;
				log_line("OpenXR extension available: XR_EXT_hand_tracking version=%u", properties[i].extensionVersion);
			}
		}

		if (!d3d11_available)
			log_line("Required OpenXR extension is unavailable: XR_KHR_D3D11_enable");
		if (!g_hand_tracking_extension_available)
			log_line("OpenXR optical hand tracking unavailable; Quest Touch remains active");
		return d3d11_available;
	}

	void shutdown_openxr()
	{
		InterlockedExchange(&g_ready, 0);
		scrapvr::shutdown();
		if (g_instance != XR_NULL_HANDLE && p_xrDestroyInstance != nullptr)
		{
			const XrResult result = p_xrDestroyInstance(g_instance);
			log_line("xrDestroyInstance result=%d", static_cast<int>(result));
		}
		g_instance = XR_NULL_HANDLE;
		g_system_id = XR_NULL_SYSTEM_ID;
		if (g_openxr_loader != nullptr)
		{
			FreeLibrary(g_openxr_loader);
			g_openxr_loader = nullptr;
		}
		p_xrGetInstanceProcAddr = nullptr;
	}

	bool initialize_openxr()
	{
		if (InterlockedCompareExchange(&g_initializing, 1, 0) != 0)
			return false;
		if (InterlockedCompareExchange(&g_ready, 0, 0) != 0)
		{
			InterlockedExchange(&g_initializing, 0);
			return true;
		}

		g_last_attempt_ms = GetTickCount64();
		log_line("OpenXR bootstrap attempt started");

		g_openxr_loader = LoadLibraryW(L"openxr_loader.dll");
		if (g_openxr_loader == nullptr)
		{
			log_line("LoadLibrary(openxr_loader.dll) failed: win32=%lu", GetLastError());
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		p_xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
			GetProcAddress(g_openxr_loader, "xrGetInstanceProcAddr"));
		if (p_xrGetInstanceProcAddr == nullptr ||
			!resolve_global("xrEnumerateInstanceExtensionProperties", p_xrEnumerateInstanceExtensionProperties) ||
			!resolve_global("xrCreateInstance", p_xrCreateInstance))
		{
			shutdown_openxr();
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		if (!has_d3d11_extension())
		{
			shutdown_openxr();
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		const char *extensions[] = {
			"XR_KHR_D3D11_enable",
			XR_EXT_HAND_TRACKING_EXTENSION_NAME
		};
		XrInstanceCreateInfo create_info = { XR_TYPE_INSTANCE_CREATE_INFO };
		std::strncpy(create_info.applicationInfo.applicationName, "Scrap Mechanic Native VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
		create_info.applicationInfo.applicationVersion = 1;
		std::strncpy(create_info.applicationInfo.engineName, "ScrapNativeVR", XR_MAX_ENGINE_NAME_SIZE - 1);
		create_info.applicationInfo.engineVersion = 1;
		// Meta's active PC runtime currently rejects an OpenXR 1.1 application request.
		// The D3D11 extension used by this integration is available in OpenXR 1.0.
		create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
		create_info.enabledExtensionCount = g_hand_tracking_extension_available ? 2u : 1u;
		create_info.enabledExtensionNames = extensions;

		XrResult result = p_xrCreateInstance(&create_info, &g_instance);
		if (XR_FAILED(result))
		{
			log_line("xrCreateInstance failed: result=%d", static_cast<int>(result));
			shutdown_openxr();
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		if (!resolve_instance("xrDestroyInstance", p_xrDestroyInstance) ||
			!resolve_instance("xrGetInstanceProperties", p_xrGetInstanceProperties) ||
			!resolve_instance("xrGetSystem", p_xrGetSystem) ||
			!resolve_instance("xrGetSystemProperties", p_xrGetSystemProperties) ||
			!resolve_instance("xrEnumerateViewConfigurationViews", p_xrEnumerateViewConfigurationViews) ||
			!resolve_instance("xrEnumerateEnvironmentBlendModes", p_xrEnumerateEnvironmentBlendModes) ||
			!resolve_instance("xrStringToPath", p_xrStringToPath))
		{
			shutdown_openxr();
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		XrInstanceProperties instance_properties = { XR_TYPE_INSTANCE_PROPERTIES };
		result = p_xrGetInstanceProperties(g_instance, &instance_properties);
		if (XR_SUCCEEDED(result))
			log_line("OpenXR runtime: %s version=%u.%u.%u",
				instance_properties.runtimeName,
				XR_VERSION_MAJOR(instance_properties.runtimeVersion),
				XR_VERSION_MINOR(instance_properties.runtimeVersion),
				XR_VERSION_PATCH(instance_properties.runtimeVersion));

		XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
		system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		result = p_xrGetSystem(g_instance, &system_info, &g_system_id);
		if (XR_FAILED(result))
		{
			log_line("xrGetSystem(HMD) failed: result=%d. Start Quest Link inside the headset and keep it awake.", static_cast<int>(result));
			shutdown_openxr();
			InterlockedExchange(&g_initializing, 0);
			return false;
		}

		XrSystemProperties system_properties = { XR_TYPE_SYSTEM_PROPERTIES };
		result = p_xrGetSystemProperties(g_instance, g_system_id, &system_properties);
		if (XR_SUCCEEDED(result))
			log_line("OpenXR HMD: %s vendor=%u maxSwapchain=%ux%u maxLayers=%u tracking(position=%u orientation=%u)",
				system_properties.systemName,
				system_properties.vendorId,
				system_properties.graphicsProperties.maxSwapchainImageWidth,
				system_properties.graphicsProperties.maxSwapchainImageHeight,
				system_properties.graphicsProperties.maxLayerCount,
				system_properties.trackingProperties.positionTracking,
				system_properties.trackingProperties.orientationTracking);

		uint32_t view_count = 0;
		result = p_xrEnumerateViewConfigurationViews(
			g_instance,
			g_system_id,
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			0,
			&view_count,
			nullptr);
		if (XR_SUCCEEDED(result) && view_count <= 8)
		{
			XrViewConfigurationView views[8] = {};
			for (uint32_t i = 0; i < view_count; ++i)
				views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
			result = p_xrEnumerateViewConfigurationViews(
				g_instance,
				g_system_id,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
				view_count,
				&view_count,
				views);
			if (XR_SUCCEEDED(result))
			{
				log_line("Primary stereo view count=%u", view_count);
				for (uint32_t i = 0; i < view_count; ++i)
					log_line("Eye %u recommended=%ux%u samples=%u max=%ux%u",
						i,
						views[i].recommendedImageRectWidth,
						views[i].recommendedImageRectHeight,
						views[i].recommendedSwapchainSampleCount,
						views[i].maxImageRectWidth,
						views[i].maxImageRectHeight);
			}
		}

		XrPath left_hand = XR_NULL_PATH;
		XrPath right_hand = XR_NULL_PATH;
		const XrResult left_result = p_xrStringToPath(g_instance, "/user/hand/left", &left_hand);
		const XrResult right_result = p_xrStringToPath(g_instance, "/user/hand/right", &right_hand);
		log_line("Controller paths: left(result=%d path=%llu) right(result=%d path=%llu)",
			static_cast<int>(left_result),
			static_cast<unsigned long long>(left_hand),
			static_cast<int>(right_result),
			static_cast<unsigned long long>(right_hand));

		InterlockedExchange(&g_ready, 1);
		InterlockedExchange(&g_initializing, 0);
		log_line("MILESTONE 1 READY: OpenXR instance, HMD system, stereo views, and controller paths discovered");
		scrapvr::initialize_session(
			g_d3d11_device,
			g_instance,
			g_system_id,
			p_xrGetInstanceProcAddr,
			log_line,
			g_hand_tracking_extension_available);
		return true;
	}

	void on_init_device(reshade::api::device *device)
	{
		log_line("ReShade device initialized: api=0x%x native=0x%llx",
			static_cast<unsigned int>(device->get_api()),
			static_cast<unsigned long long>(device->get_native()));
		if (device->get_api() == reshade::api::device_api::d3d11)
		{
			g_d3d11_device = reinterpret_cast<ID3D11Device *>(static_cast<uintptr_t>(device->get_native()));
			engine_hooks::install(log_line);
			initialize_openxr();
		}
		else
			log_line("Unsupported graphics API for native VR bootstrap");
	}

	void on_destroy_device(reshade::api::device *)
	{
		log_line("ReShade device destroyed");
		engine_hooks::uninstall();
		shutdown_openxr();
		g_d3d11_device = nullptr;
	}

	bool on_draw(
		reshade::api::command_list *,
		uint32_t,
		uint32_t,
		uint32_t,
		uint32_t)
	{
		InterlockedIncrement64(&g_draw_calls);
		return engine_hooks::suppress_viewmodel_draw();
	}

	bool on_draw_indexed(
		reshade::api::command_list *,
		uint32_t,
		uint32_t,
		uint32_t,
		int32_t,
		uint32_t)
	{
		InterlockedIncrement64(&g_indexed_draw_calls);
		return engine_hooks::suppress_viewmodel_draw();
	}

	void on_bind_render_targets(
		reshade::api::command_list *command_list,
		uint32_t count,
		const reshade::api::resource_view *render_targets,
		reshade::api::resource_view depth_stencil)
	{
		if (g_rebinding_eye_target || command_list == nullptr || render_targets == nullptr ||
			count == 0 || count > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
			return;
		ID3D11RenderTargetView *native_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		for (uint32_t i = 0; i < count; ++i)
			native_targets[i] = reinterpret_cast<ID3D11RenderTargetView *>(
				static_cast<uintptr_t>(render_targets[i].handle));
		auto *context = reinterpret_cast<ID3D11DeviceContext *>(
			static_cast<uintptr_t>(command_list->get_native()));
		auto *native_depth = reinterpret_cast<ID3D11DepthStencilView *>(
			static_cast<uintptr_t>(depth_stencil.handle));
		g_rebinding_eye_target = true;
		scrapvr::redirect_eye_render_target(
			context,
			count,
			native_targets,
			native_depth);
		g_rebinding_eye_target = false;
	}

	void on_present(
		reshade::api::command_queue *,
		reshade::api::swapchain *swapchain,
		const reshade::api::rect *,
		const reshade::api::rect *,
		uint32_t,
		const reshade::api::rect *)
	{
		if (swapchain != nullptr)
			scrapvr::set_game_swapchain(reinterpret_cast<IDXGISwapChain *>(
				static_cast<uintptr_t>(swapchain->get_native())));
		const LONG64 draws = InterlockedExchange64(&g_draw_calls, 0);
		const LONG64 indexed_draws = InterlockedExchange64(&g_indexed_draw_calls, 0);
		engine_hooks::on_present();
		if (!g_draw_callbacks_logged && draws + indexed_draws > 0)
		{
			g_draw_callbacks_logged = true;
			log_line(
				"D3D11 draw interception ready: draw=%lld indexed=%lld in one frame",
				static_cast<long long>(draws),
				static_cast<long long>(indexed_draws));
		}
		if (InterlockedCompareExchange(&g_ready, 0, 0) == 0 &&
			InterlockedCompareExchange(&g_initializing, 0, 0) == 0 &&
			GetTickCount64() - g_last_attempt_ms >= 5000)
			initialize_openxr();
		else if (InterlockedCompareExchange(&g_ready, 0, 0) != 0)
		{
			if (!scrapvr::is_initialized())
				scrapvr::initialize_session(
					g_d3d11_device,
					g_instance,
					g_system_id,
					p_xrGetInstanceProcAddr,
					log_line,
					g_hand_tracking_extension_available);
			scrapvr::on_present();
		}
	}
}

extern "C" __declspec(dllexport) const char *NAME = "Scrap Mechanic Native VR Bootstrap";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Build-specific D3D11/OpenXR bootstrap for Scrap Mechanic and Meta Quest.";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_module = module;
		DisableThreadLibraryCalls(module);
		if (!reshade::register_addon(module))
			return FALSE;
		reshade::register_event<reshade::addon_event::init_device>(on_init_device);
		reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
		reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets);
		reshade::register_event<reshade::addon_event::draw>(on_draw);
		reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed);
		reshade::register_event<reshade::addon_event::present>(on_present);
		log_line("Native VR add-on registered");
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		reshade::unregister_event<reshade::addon_event::present>(on_present);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(on_draw_indexed);
		reshade::unregister_event<reshade::addon_event::draw>(on_draw);
		reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets);
		reshade::unregister_event<reshade::addon_event::destroy_device>(on_destroy_device);
		reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
		reshade::unregister_addon(module);
	}
	return TRUE;
}
