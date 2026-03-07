// Copyright Kelly Rey Wilson. All Rights Reserved.
// Registers Vulkan extensions for io_uring GPU sync integration

#pragma once

#include "Modules/ModuleManager.h"

class FFlightVulkanExtensionsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Returns true if external semaphore extension was requested */
	static bool WasExtensionRequested() { return bExtensionRequested; }

private:
	static bool bExtensionRequested;
};
