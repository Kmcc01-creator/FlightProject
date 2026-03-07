// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Swarm/NiagaraDataInterfaceSwarm.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "NiagaraShaderParametersBuilder.h"
#include "Engine/World.h"
#include "RenderGraphBuilder.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSwarm"

struct FNDISwarmInstanceData
{
	TWeakObjectPtr<UFlightSwarmSubsystem> SwarmSubsystem;
};

/** 
 * GPU parameters for our NDI
 */
BEGIN_SHADER_PARAMETER_STRUCT(FNDISwarmParametersCS, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FDroidState>, DroidStates)
	SHADER_PARAMETER(uint32, NumEntities)
END_SHADER_PARAMETER_STRUCT()

class FNDISwarmProxy : public FNiagaraDataInterfaceProxy
{
public:
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		FNDISwarmInstanceData* Config = static_cast<FNDISwarmInstanceData*>(PerInstanceData);
		InstanceDataMap.FindOrAdd(InstanceID) = *Config;
	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDISwarmInstanceData); }

	TMap<FNiagaraSystemInstanceID, FNDISwarmInstanceData> InstanceDataMap;
};

UNiagaraDataInterfaceSwarm::UNiagaraDataInterfaceSwarm()
{
	Proxy.Reset(new FNDISwarmProxy());
}

void UNiagaraDataInterfaceSwarm::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = TEXT("GetDroidPosition");
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Swarm")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = TEXT("GetDroidVelocity");
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Swarm")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = TEXT("GetDroidShield");
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Swarm")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Shield")));
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSwarm::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
}

bool UNiagaraDataInterfaceSwarm::Equals(const UNiagaraDataInterface* Other) const
{
	return Super::Equals(Other) && Cast<UNiagaraDataInterfaceSwarm>(Other) != nullptr;
}

bool UNiagaraDataInterfaceSwarm::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination)) return false;
	return true;
}

void UNiagaraDataInterfaceSwarm::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, FString& OutHLSL)
{
	OutHLSL.Appendf(TEXT("StructuredBuffer<FDroidState> DroidStates_%s;\n"), *ParameterInfo.DataInterfaceHLSLSymbol);
	OutHLSL.Appendf(TEXT("uint NumEntities_%s;\n"), *ParameterInfo.DataInterfaceHLSLSymbol);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceSwarm::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == TEXT("GetDroidPosition"))
	{
		OutHLSL.Appendf(TEXT("Position = DroidStates_%s[Index].Position;\n"), *ParameterInfo.DataInterfaceHLSLSymbol);
		return true;
	}
	if (FunctionInfo.DefinitionName == TEXT("GetDroidVelocity"))
	{
		OutHLSL.Appendf(TEXT("Velocity = DroidStates_%s[Index].Velocity;\n"), *ParameterInfo.DataInterfaceHLSLSymbol);
		return true;
	}
	if (FunctionInfo.DefinitionName == TEXT("GetDroidShield"))
	{
		OutHLSL.Appendf(TEXT("Shield = DroidStates_%s[Index].Shield;\n"), *ParameterInfo.DataInterfaceHLSLSymbol);
		return true;
	}
	return false;
}
#endif

void UNiagaraDataInterfaceSwarm::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FNDISwarmParametersCS>();
}

void UNiagaraDataInterfaceSwarm::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FNDISwarmProxy& DIProxy = Context.GetProxy<FNDISwarmProxy>();
	FNDISwarmParametersCS* Parameters = Context.GetParameterNestedStruct<FNDISwarmParametersCS>();
	
	const FNDISwarmInstanceData* Data = DIProxy.InstanceDataMap.Find(Context.GetSystemInstanceID());
	if (Data && Data->SwarmSubsystem.IsValid())
	{
		auto DroidBuffer = Data->SwarmSubsystem->GetDroidStateBuffer();
		if (DroidBuffer.IsValid())
		{
			// Direct access to persistent buffer in RDG
			FRDGBufferRef DroidBufferRDG = Context.GetGraphBuilder().RegisterExternalBuffer(DroidBuffer);
			Parameters->DroidStates = Context.GetGraphBuilder().CreateSRV(DroidBufferRDG);
			Parameters->NumEntities = Data->SwarmSubsystem->GetNumEntities();
		}
	}
}

bool UNiagaraDataInterfaceSwarm::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISwarmInstanceData* InstanceData = new (PerInstanceData) FNDISwarmInstanceData();
	InstanceData->SwarmSubsystem = SystemInstance->GetWorld()->GetSubsystem<UFlightSwarmSubsystem>();
	return true;
}

void UNiagaraDataInterfaceSwarm::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISwarmInstanceData* InstanceData = static_cast<FNDISwarmInstanceData*>(PerInstanceData);
	InstanceData->~FNDISwarmInstanceData();
}

int32 UNiagaraDataInterfaceSwarm::PerInstanceDataSize() const
{
	return sizeof(FNDISwarmInstanceData);
}

#undef LOCTEXT_NAMESPACE
