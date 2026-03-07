# Platform Audit Script
# Run from Unreal Output Log: py audit_platform.py
import unreal
import os

def audit():
    unreal.log("=== FlightProject Platform Audit ===")
    
    # 1. Environment Variables
    env_vars = [
        "SDL_VIDEODRIVER",
        "XDG_SESSION_TYPE",
        "WAYLAND_DISPLAY",
        "DISPLAY",
        "SDL_DYNAMIC_API",
        "GDK_BACKEND",
        "QT_QPA_PLATFORM",
        "SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR"
    ]
    
    unreal.log("Environment Variables:")
    for var in env_vars:
        val = os.getenv(var, 'NOT SET')
        unreal.log(f"  {var}: {val}")
        if var == "SDL_DYNAMIC_API" and val != 'NOT SET':
            unreal.log("  WARNING: SDL_DYNAMIC_API is active. Engine patches may be bypassed!")

    # 2. SDL Backend Info (if we can infer it)
    # The LogInit usually prints this, but we can verify our current assumption
    if os.getenv("SDL_VIDEODRIVER") == "wayland":
        unreal.log("Intended Backend: Wayland")
    else:
        unreal.log("Intended Backend: X11/Default")

    unreal.log("====================================")
    unreal.log("Recommendation: If popups are still small, ensure 'High DPI Support' is DISABLED")
    unreal.log("and that SDL_DYNAMIC_API is UNSET in your shell before launching.")

if __name__ == "__main__":
    audit()
