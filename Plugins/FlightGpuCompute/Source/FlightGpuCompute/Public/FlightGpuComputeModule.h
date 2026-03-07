// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightGpuCompute - RDG compute shaders with PostConfigInit shader registration

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FFlightGpuComputeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Check if compute shaders are available (registered successfully) */
	static bool AreShadersAvailable() { return bShadersRegistered; }

private:
	static bool bShadersRegistered;
};
