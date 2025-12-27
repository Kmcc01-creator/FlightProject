// FlightMeshIR.cpp

#include "Modeling/FlightMeshIR.h"

int64 FFlightMeshIR::ComputeHash() const
{
	// Start with version to invalidate on schema changes
	uint64 Hash = static_cast<uint64>(Version);

	// Hash each operation
	for (const FFlightMeshOpDescriptor& Op : Ops)
	{
		// Pack operation metadata
		uint32 OpMeta = 0;
		OpMeta |= static_cast<uint32>(Op.Primitive);
		OpMeta |= static_cast<uint32>(Op.Operation) << 8;
		OpMeta |= static_cast<uint32>(Op.Origin) << 16;
		OpMeta |= static_cast<uint32>(Op.Flags.Pack()) << 24;

		Hash = HashCombineFast(Hash, GetTypeHash(OpMeta));

		// Hash primitive parameters
		Hash = HashCombineFast(Hash, GetTypeHash(Op.Params.X));
		Hash = HashCombineFast(Hash, GetTypeHash(Op.Params.Y));
		Hash = HashCombineFast(Hash, GetTypeHash(Op.Params.Z));
		Hash = HashCombineFast(Hash, GetTypeHash(Op.Params.W));

		// Hash transform components
		// Use IsNearlyEqual-friendly precision by quantizing
		const FVector Location = Op.Transform.GetLocation();
		const FRotator Rotation = Op.Transform.GetRotation().Rotator();
		const FVector Scale = Op.Transform.GetScale3D();

		// Quantize to 0.001 precision for stable hashing
		auto Quantize = [](double V) -> int64 {
			return static_cast<int64>(V * 1000.0);
		};

		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Location.X)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Location.Y)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Location.Z)));

		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Rotation.Pitch)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Rotation.Yaw)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Rotation.Roll)));

		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Scale.X)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Scale.Y)));
		Hash = HashCombineFast(Hash, GetTypeHash(Quantize(Scale.Z)));
	}

	return static_cast<int64>(Hash);
}
