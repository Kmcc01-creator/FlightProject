// FlightLogCategories.cpp
// Log category definitions for FlightProject

#include "FlightLogCategories.h"
#include "UI/FlightLogTypes.h"

// Recursion guard for FlightProject logging system
thread_local bool Flight::Log::bIsLoggingInternal = false;

// Core flight simulation
DEFINE_LOG_CATEGORY(LogFlight);

// Movement and physics
DEFINE_LOG_CATEGORY(LogFlightMovement);
DEFINE_LOG_CATEGORY(LogFlightPhysics);

// Navigation and pathfinding
DEFINE_LOG_CATEGORY(LogFlightNav);
DEFINE_LOG_CATEGORY(LogFlightPath);

// Mass Entity ECS
DEFINE_LOG_CATEGORY(LogFlightMass);
DEFINE_LOG_CATEGORY(LogFlightMassSpawn);
DEFINE_LOG_CATEGORY(LogFlightMassProcessor);

// Data loading and CSV
DEFINE_LOG_CATEGORY(LogFlightData);

// Swarm systems
DEFINE_LOG_CATEGORY(LogFlightSwarm);

// Autopilot and AI
DEFINE_LOG_CATEGORY(LogFlightAI);
DEFINE_LOG_CATEGORY(LogFlightAutopilot);

// Input handling
DEFINE_LOG_CATEGORY(LogFlightInput);

// Networking
DEFINE_LOG_CATEGORY(LogFlightNet);

// GPU and compute
DEFINE_LOG_CATEGORY(LogFlightGPU);
DEFINE_LOG_CATEGORY(LogFlightCompute);

// io_uring integration
DEFINE_LOG_CATEGORY(LogFlightIoUring);

// UI and Slate
DEFINE_LOG_CATEGORY(LogFlightUI);

// Platform (Linux/Wayland)
DEFINE_LOG_CATEGORY(LogFlightPlatform);

// Subsystems
DEFINE_LOG_CATEGORY(LogFlightSubsystem);

// Debug and development
DEFINE_LOG_CATEGORY(LogFlightDebug);

// Performance profiling
DEFINE_LOG_CATEGORY(LogFlightPerf);
