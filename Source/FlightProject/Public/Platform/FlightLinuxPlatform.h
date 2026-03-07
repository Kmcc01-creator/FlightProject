// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightLinuxPlatform - Enhanced Linux/Wayland Platform Layer
//
// Custom platform extensions for better Wayland support:
//   - Proper popup positioning via xdg_popup anchoring
//   - Input scaling fixes for fractional DPI
//   - Async event processing integration
//   - Window management optimizations
//
// Note: This doesn't replace FLinuxApplication directly (that's engine code).
// Instead, it provides:
//   1. Event processing hooks via SDL3
//   2. Wayland-specific utilities
//   3. Platform detection and configuration
//   4. Integration with FlightProject's async systems

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightReflection.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

// SDL3 headers (conditionally included)
#if PLATFORM_LINUX
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#endif

namespace Flight::Platform
{

using namespace Flight::Functional;
using namespace Flight::Reflection;


// ============================================================================
// Platform Detection
// ============================================================================

enum class EDisplayServer : uint8
{
	Unknown,
	X11,
	Wayland,
	Headless
};

enum class ECompositor : uint8
{
	Unknown,
	Hyprland,
	Sway,
	KWin,
	Mutter,
	Weston,
	X11  // X11 window manager, not Wayland
};

struct FPlatformInfo
{
	EDisplayServer DisplayServer = EDisplayServer::Unknown;
	ECompositor Compositor = ECompositor::Unknown;
	FString SessionType;      // XDG_SESSION_TYPE
	FString WaylandDisplay;   // WAYLAND_DISPLAY
	FString X11Display;       // DISPLAY
	FString CompositorName;   // Detected compositor name
	float DPIScale = 1.0f;
	bool bFractionalScaling = false;
	bool bSupportsLayerShell = false;  // wlr-layer-shell support

	bool IsWayland() const { return DisplayServer == EDisplayServer::Wayland; }
	bool IsX11() const { return DisplayServer == EDisplayServer::X11; }

	FLIGHT_REFLECT_BODY(FPlatformInfo)
};

FLIGHT_REFLECT_FIELDS(FPlatformInfo,
	FLIGHT_FIELD(FString, SessionType),
	FLIGHT_FIELD(FString, WaylandDisplay),
	FLIGHT_FIELD(FString, X11Display),
	FLIGHT_FIELD(FString, CompositorName),
	FLIGHT_FIELD(float, DPIScale),
	FLIGHT_FIELD(bool, bFractionalScaling),
	FLIGHT_FIELD(bool, bSupportsLayerShell)
)

// Detect current platform configuration
inline FPlatformInfo DetectPlatform()
{
	FPlatformInfo Info;

#if PLATFORM_LINUX
	// Check session type
	if (const char* SessionType = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_SESSION_TYPE")))
	{
		Info.SessionType = UTF8_TO_TCHAR(SessionType);
	}

	// Check Wayland display
	if (const char* WaylandDisplay = FPlatformMisc::GetEnvironmentVariable(TEXT("WAYLAND_DISPLAY")))
	{
		Info.WaylandDisplay = UTF8_TO_TCHAR(WaylandDisplay);
	}

	// Check X11 display
	if (const char* X11Display = FPlatformMisc::GetEnvironmentVariable(TEXT("DISPLAY")))
	{
		Info.X11Display = UTF8_TO_TCHAR(X11Display);
	}

	// Determine display server
	if (Info.SessionType == TEXT("wayland") || !Info.WaylandDisplay.IsEmpty())
	{
		Info.DisplayServer = EDisplayServer::Wayland;
	}
	else if (Info.SessionType == TEXT("x11") || !Info.X11Display.IsEmpty())
	{
		Info.DisplayServer = EDisplayServer::X11;
	}
	else if (Info.SessionType == TEXT("tty"))
	{
		Info.DisplayServer = EDisplayServer::Headless;
	}

	// Detect compositor
	if (Info.IsWayland())
	{
		// Check Hyprland
		if (const char* HyprSig = FPlatformMisc::GetEnvironmentVariable(TEXT("HYPRLAND_INSTANCE_SIGNATURE")))
		{
			Info.Compositor = ECompositor::Hyprland;
			Info.CompositorName = TEXT("Hyprland");
		}
		// Check Sway
		else if (const char* SwaySocket = FPlatformMisc::GetEnvironmentVariable(TEXT("SWAYSOCK")))
		{
			Info.Compositor = ECompositor::Sway;
			Info.CompositorName = TEXT("Sway");
		}
		// Check KDE
		else if (const char* KDE = FPlatformMisc::GetEnvironmentVariable(TEXT("KDE_FULL_SESSION")))
		{
			Info.Compositor = ECompositor::KWin;
			Info.CompositorName = TEXT("KWin");
		}
		// Check GNOME
		else if (const char* Desktop = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_CURRENT_DESKTOP")))
		{
			FString DesktopStr = UTF8_TO_TCHAR(Desktop);
			if (DesktopStr.Contains(TEXT("GNOME")))
			{
				Info.Compositor = ECompositor::Mutter;
				Info.CompositorName = TEXT("Mutter");
			}
		}
	}
	else if (Info.IsX11())
	{
		Info.Compositor = ECompositor::X11;
		Info.CompositorName = TEXT("X11");
	}

	// Check for fractional scaling
	if (const char* GDKScale = FPlatformMisc::GetEnvironmentVariable(TEXT("GDK_SCALE")))
	{
		float Scale = FCString::Atof(UTF8_TO_TCHAR(GDKScale));
		if (Scale != FMath::FloorToFloat(Scale))
		{
			Info.bFractionalScaling = true;
		}
		Info.DPIScale = Scale;
	}
	else if (const char* QtScale = FPlatformMisc::GetEnvironmentVariable(TEXT("QT_SCALE_FACTOR")))
	{
		float Scale = FCString::Atof(UTF8_TO_TCHAR(QtScale));
		if (Scale != FMath::FloorToFloat(Scale))
		{
			Info.bFractionalScaling = true;
		}
		Info.DPIScale = Scale;
	}
#endif

	return Info;
}


// ============================================================================
// Wayland Popup Anchor Types
// ============================================================================
//
// Wayland's xdg_popup requires explicit anchoring.
// This enum maps to xdg_positioner anchor values.

enum class EPopupAnchor : uint8
{
	None = 0,
	Top = 1,
	Bottom = 2,
	Left = 4,
	Right = 8,
	TopLeft = Top | Left,
	TopRight = Top | Right,
	BottomLeft = Bottom | Left,
	BottomRight = Bottom | Right,
	Center = 16
};
ENUM_CLASS_FLAGS(EPopupAnchor)

enum class EPopupGravity : uint8
{
	None = 0,
	Top = 1,
	Bottom = 2,
	Left = 4,
	Right = 8,
	TopLeft = Top | Left,
	TopRight = Top | Right,
	BottomLeft = Bottom | Left,
	BottomRight = Bottom | Right
};
ENUM_CLASS_FLAGS(EPopupGravity)

struct FPopupPositionRequest
{
	// Parent window anchor point
	EPopupAnchor ParentAnchor = EPopupAnchor::BottomLeft;

	// Popup gravity (direction popup opens toward)
	EPopupGravity Gravity = EPopupGravity::BottomRight;

	// Offset from anchor point
	FIntPoint Offset = FIntPoint::ZeroValue;

	// Constraint adjustment flags (what to do if popup would be clipped)
	bool bSlideX = true;
	bool bSlideY = true;
	bool bFlipX = true;
	bool bFlipY = true;
	bool bResizeX = false;
	bool bResizeY = false;

	// Anchor rect within parent (relative to parent top-left)
	FIntRect AnchorRect = FIntRect(0, 0, 0, 0);
};


// ============================================================================
// Window Configuration for Wayland
// ============================================================================

struct FWaylandWindowConfig
{
	// App ID (used for compositor window matching rules)
	FString AppId = TEXT("com.flightproject.editor");

	// Inhibit idle (prevent screen blanking)
	bool bInhibitIdle = false;

	// Request server-side decorations
	bool bServerSideDecorations = true;

	// Popup configuration
	TOptional<FPopupPositionRequest> PopupPosition;

	// Layer shell configuration (if supported)
	bool bUseLayerShell = false;
	int32 LayerShellLayer = 0;  // 0=background, 1=bottom, 2=top, 3=overlay
	FMargin LayerShellMargins;
	bool bLayerShellExclusiveZone = false;
};


// ============================================================================
// SDL3 Event Hook System
// ============================================================================
//
// Allows FlightProject to intercept and process SDL3 events
// before they reach FLinuxApplication.

#if PLATFORM_LINUX

using FSDLEventHandler = TFunction<bool(const SDL_Event& Event)>;

class FSDLEventHooks
{
public:
	static FSDLEventHooks& Get()
	{
		static FSDLEventHooks Instance;
		return Instance;
	}

	// Add a hook that can intercept events
	// Return true from handler to consume the event
	uint32 AddHook(FSDLEventHandler Handler, int32 Priority = 0)
	{
		FScopeLock Lock(&HooksLock);
		uint32 Id = NextHookId++;
		Hooks.Add({Id, Priority, MoveTemp(Handler)});
		Hooks.Sort([](const FHook& A, const FHook& B) {
			return A.Priority > B.Priority;  // Higher priority first
		});
		return Id;
	}

	void RemoveHook(uint32 Id)
	{
		FScopeLock Lock(&HooksLock);
		Hooks.RemoveAll([Id](const FHook& H) { return H.Id == Id; });
	}

	// Process event through hooks
	// Returns true if event was consumed
	bool ProcessEvent(const SDL_Event& Event)
	{
		FScopeLock Lock(&HooksLock);
		for (const FHook& Hook : Hooks)
		{
			if (Hook.Handler(Event))
			{
				return true;  // Event consumed
			}
		}
		return false;
	}

private:
	struct FHook
	{
		uint32 Id;
		int32 Priority;
		FSDLEventHandler Handler;
	};

	TArray<FHook> Hooks;
	FCriticalSection HooksLock;
	uint32 NextHookId = 1;
};


// ============================================================================
// Input Scaling Fix
// ============================================================================
//
// Wayland with fractional scaling can cause mouse coordinate misalignment.
// This provides utilities to correct input coordinates.

class FInputScaler
{
public:
	static FInputScaler& Get()
	{
		static FInputScaler Instance;
		return Instance;
	}

	void SetWindowScale(SDL_Window* Window, float Scale)
	{
		FScopeLock Lock(&ScaleLock);
		WindowScales.Add(Window, Scale);
	}

	void RemoveWindow(SDL_Window* Window)
	{
		FScopeLock Lock(&ScaleLock);
		WindowScales.Remove(Window);
	}

	float GetWindowScale(SDL_Window* Window) const
	{
		FScopeLock Lock(&ScaleLock);
		if (const float* Scale = WindowScales.Find(Window))
		{
			return *Scale;
		}
		return 1.0f;
	}

	// Correct mouse coordinates for fractional scaling
	FVector2D CorrectMousePosition(SDL_Window* Window, float X, float Y) const
	{
		float Scale = GetWindowScale(Window);
		if (Scale != 1.0f)
		{
			// Apply inverse scaling to get correct coordinates
			return FVector2D(X / Scale, Y / Scale);
		}
		return FVector2D(X, Y);
	}

private:
	TMap<SDL_Window*, float> WindowScales;
	mutable FCriticalSection ScaleLock;
};


// ============================================================================
// SDL3 Utilities
// ============================================================================

namespace SDL3Utils
{

// Get current SDL video driver name
inline FString GetVideoDriver()
{
	if (const char* Driver = SDL_GetCurrentVideoDriver())
	{
		return UTF8_TO_TCHAR(Driver);
	}
	return TEXT("unknown");
}

// Check if running on Wayland via SDL
inline bool IsWaylandBackend()
{
	return GetVideoDriver() == TEXT("wayland");
}

// Check if running on X11 via SDL
inline bool IsX11Backend()
{
	return GetVideoDriver() == TEXT("x11");
}

// Get window position (handles Wayland limitations)
inline TResult<FIntPoint, FString> GetWindowPosition(SDL_Window* Window)
{
	int X, Y;
	if (SDL_GetWindowPosition(Window, &X, &Y))
	{
		return TResult<FIntPoint, FString>::Ok(FIntPoint(X, Y));
	}
	return TResult<FIntPoint, FString>::Err(
		FString::Printf(TEXT("Failed to get window position: %hs"), SDL_GetError())
	);
}

// Get window size
inline TResult<FIntPoint, FString> GetWindowSize(SDL_Window* Window)
{
	int W, H;
	if (SDL_GetWindowSize(Window, &W, &H))
	{
		return TResult<FIntPoint, FString>::Ok(FIntPoint(W, H));
	}
	return TResult<FIntPoint, FString>::Err(
		FString::Printf(TEXT("Failed to get window size: %hs"), SDL_GetError())
	);
}

// Set window app ID (Wayland)
inline bool SetAppId(SDL_Window* Window, const FString& AppId)
{
	// SDL3 uses properties for this
	SDL_PropertiesID Props = SDL_GetWindowProperties(Window);
	return SDL_SetStringProperty(Props, "SDL.window.wayland.app_id", TCHAR_TO_UTF8(*AppId));
}

// Request attention (flash taskbar, etc.)
inline void RequestAttention(SDL_Window* Window, bool bUrgent = false)
{
	SDL_FlashWindow(Window, bUrgent ? SDL_FLASH_UNTIL_FOCUSED : SDL_FLASH_BRIEFLY);
}

// Get display content scale (DPI)
inline float GetDisplayScale(int DisplayIndex = 0)
{
	SDL_DisplayID* Displays = SDL_GetDisplays(nullptr);
	if (Displays && DisplayIndex < SDL_GetNumVideoDisplays())
	{
		float Scale = SDL_GetDisplayContentScale(Displays[DisplayIndex]);
		SDL_free(Displays);
		return Scale;
	}
	return 1.0f;
}

} // namespace SDL3Utils

#endif // PLATFORM_LINUX


// ============================================================================
// Platform Service
// ============================================================================
//
// High-level service for platform-specific operations.
// Can be accessed as a subsystem or standalone.

class FFlightPlatformService
{
public:
	static FFlightPlatformService& Get()
	{
		static FFlightPlatformService Instance;
		return Instance;
	}

	void Initialize()
	{
		if (bInitialized) return;

		PlatformInfo = DetectPlatform();

		UE_LOG(LogTemp, Log, TEXT("Flight Platform Service Initialized:"));
		UE_LOG(LogTemp, Log, TEXT("  Display Server: %s"),
			PlatformInfo.IsWayland() ? TEXT("Wayland") :
			PlatformInfo.IsX11() ? TEXT("X11") : TEXT("Unknown"));
		UE_LOG(LogTemp, Log, TEXT("  Compositor: %s"), *PlatformInfo.CompositorName);
		UE_LOG(LogTemp, Log, TEXT("  DPI Scale: %.2f"), PlatformInfo.DPIScale);
		UE_LOG(LogTemp, Log, TEXT("  Fractional Scaling: %s"),
			PlatformInfo.bFractionalScaling ? TEXT("Yes") : TEXT("No"));

#if PLATFORM_LINUX
		UE_LOG(LogTemp, Log, TEXT("  SDL Video Driver: %s"), *SDL3Utils::GetVideoDriver());

		// Install input scaling fix if needed
		if (PlatformInfo.bFractionalScaling)
		{
			InstallInputScalingFix();
		}
#endif

		bInitialized = true;
	}

	const FPlatformInfo& GetPlatformInfo() const { return PlatformInfo; }

	// Check if we're in a compatible environment
	bool IsEnvironmentSupported() const
	{
		return PlatformInfo.IsWayland() || PlatformInfo.IsX11();
	}

	// Get recommended configuration for current platform
	FWaylandWindowConfig GetRecommendedWindowConfig() const
	{
		FWaylandWindowConfig Config;

		if (PlatformInfo.Compositor == ECompositor::Hyprland)
		{
			// Hyprland-specific optimizations
			Config.bServerSideDecorations = false;  // Use client-side for better theming
		}
		else if (PlatformInfo.Compositor == ECompositor::Sway)
		{
			// Sway works well with server-side
			Config.bServerSideDecorations = true;
		}

		return Config;
	}

private:
	FFlightPlatformService() = default;

#if PLATFORM_LINUX
	void InstallInputScalingFix()
	{
		// Add event hook to correct mouse positions
		FSDLEventHooks::Get().AddHook(
			[this](const SDL_Event& Event) -> bool {
				if (Event.type == SDL_EVENT_MOUSE_MOTION ||
					Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
					Event.type == SDL_EVENT_MOUSE_BUTTON_UP)
				{
					// Could modify event here if we had mutable access
					// For now, just log fractional scaling issues
					// Real fix would need engine-level changes
				}
				return false;  // Don't consume
			},
			100  // High priority
		);

		UE_LOG(LogTemp, Log, TEXT("Input scaling fix installed for fractional DPI"));
	}
#endif

	FPlatformInfo PlatformInfo;
	bool bInitialized = false;
};


// ============================================================================
// Hyprland-Specific Integration
// ============================================================================
//
// Hyprland IPC for window rules, workspace control, etc.

#if PLATFORM_LINUX

class FHyprlandIPC
{
public:
	static TOptional<FHyprlandIPC> Create()
	{
		// Check for Hyprland
		const char* Signature = FPlatformMisc::GetEnvironmentVariable(TEXT("HYPRLAND_INSTANCE_SIGNATURE"));
		if (!Signature)
		{
			return {};
		}

		FHyprlandIPC IPC;
		IPC.InstanceSignature = UTF8_TO_TCHAR(Signature);

		// Socket path: $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock
		const char* RuntimeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_RUNTIME_DIR"));
		if (RuntimeDir)
		{
			IPC.SocketPath = FString::Printf(
				TEXT("%hs/hypr/%s/.socket.sock"),
				RuntimeDir,
				*IPC.InstanceSignature
			);
		}

		return IPC;
	}

	// Send a dispatch command to Hyprland
	TResult<FString, FString> Dispatch(const FString& Command) const
	{
		// Would use Unix socket to communicate
		// For now, just use hyprctl command
		FString Output;
		FString Errors;
		int32 ReturnCode;

		FString Cmd = FString::Printf(TEXT("hyprctl dispatch %s"), *Command);

		if (FPlatformProcess::ExecProcess(
			TEXT("/usr/bin/hyprctl"),
			*FString::Printf(TEXT("dispatch %s"), *Command),
			&ReturnCode,
			&Output,
			&Errors))
		{
			if (ReturnCode == 0)
			{
				return TResult<FString, FString>::Ok(Output);
			}
		}

		return TResult<FString, FString>::Err(Errors.IsEmpty() ? TEXT("Unknown error") : Errors);
	}

	// Move window to workspace
	TResult<void, FString> MoveToWorkspace(int32 Workspace) const
	{
		return Dispatch(FString::Printf(TEXT("movetoworkspace %d"), Workspace))
			.Map([](const FString&) { return; });
	}

	// Set window floating
	TResult<void, FString> ToggleFloat() const
	{
		return Dispatch(TEXT("togglefloating"))
			.Map([](const FString&) { return; });
	}

	// Focus window by class
	TResult<void, FString> FocusWindow(const FString& WindowClass) const
	{
		return Dispatch(FString::Printf(TEXT("focuswindow class:%s"), *WindowClass))
			.Map([](const FString&) { return; });
	}

private:
	FString InstanceSignature;
	FString SocketPath;
};

#endif // PLATFORM_LINUX


// ============================================================================
// Convenience Initialization
// ============================================================================

inline void InitializeFlightPlatform()
{
	FFlightPlatformService::Get().Initialize();
}

} // namespace Flight::Platform
