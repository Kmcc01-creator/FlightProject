# Flight Controls Overview

_Last updated: October 10, 2025_

Player flight currently relies on `AFlightVehiclePawn` + `UFlightMovementComponent`. Inputs are sampled through Enhanced Input axis mappings (see `Config/DefaultInput.ini`) and smoothed inside the pawn before being applied to the movement component. This note captures how the system works today, the rough edges we have observed (“wonky” response), and how we could adopt quaternion-based steering to stabilise orientation.

## Current Control Flow

1. **Enhanced Input**  
   - `DefaultInputContext` (see `Content/Input/`) maps axes such as `Fly_Throttle`, `Fly_Pitch`, `Fly_Yaw`, `Fly_Roll`, and `Fly_Climb` to keyboard, mouse, and gamepad inputs.  
   - `AFlightVehiclePawn::SetupPlayerInputComponent` binds these axes to mutators that populate `TargetInput` (clamped to [-1, 1]).

2. **Input Smoothing**  
   - Each tick, `ApplyUpdatedInput()` interpolates toward `TargetInput` using `FMath::FInterpTo` with `InputSmoothing` as the rate to avoid abrupt changes.
   - Autopilot toggles reset inputs and hand control to Mass/AI systems.

3. **Movement Component** (`UFlightMovementComponent`)  
   - Calculates thrust, lift, drag, and climb contributions in `UpdateFlightModel()` using the last-applied control input.  
   - Steering uses `ApplySteering()`: computes Euler deltas (Pitch/Yaw/Roll) scaled by `TurnRate * DeltaSeconds`, converts them to a quaternion (`DeltaRot.Quaternion()`), and calls `UpdatedComponent->AddLocalRotation`.
   - Movement integration is handled via `SafeMoveUpdatedComponent`.

4. **Altitude Tracking**  
   - A downward line trace caches altitude (`GetCurrentAltitudeMeters`) for HUD/telemetry.

## Pain Points

- **Euler increment drift**: Steering builds a Delta rotator each frame, converts to a quaternion, and applies it to the component. Large delta times or extreme inputs can lead to gimbal artifacts (e.g., combining 90° pitch with yaw/roll).
- **Coupled axes**: Because pitch/yaw/roll all multiply `TurnRate`, the craft can feel sluggish on one axis or too responsive on another. There is no per-axis responsiveness or damping.
- **Velocity vs. orientation**: Velocity is integrated independently from orientation. Sudden yaw inputs can snap the mesh without blending the velocity vector, leading to “skidding” until acceleration catches up.
- **No angular damping**: Emergency stop zeros inputs but orientation continues to change if residual velocity exists.

## Quaternion-Oriented Update (October 10, 2025)

- `UFlightMovementComponent` now steers using quaternions built from per-axis inputs and blends via `FQuat::Slerp`, reducing gimbal artifacts.
- New editable properties:
  - `TurnRateYawDegPerSec`, `TurnRatePitchDegPerSec`, `TurnRateRollDegPerSec`
  - `SteeringSlerpSpeed`
  - `bAlignVelocityWithOrientation` + `VelocityAlignmentRate`
- Velocity optionally aligns toward the oriented forward vector, reducing lateral drift during sharp turns.

Further tuning (e.g., angular inertia) still follows the plan below.

## Remaining Improvement Ideas

1. **Store orientation explicitly**  
   - Cache `FQuat CurrentOrientation = UpdatedComponent->GetComponentQuat()` and apply steering by constructing per-axis quaternions:  
     ```cpp
     const FQuat PitchQuat = FQuat(UpdatedComponent->GetRightVector(), PitchRadians);
     const FQuat YawQuat   = FQuat(FVector::UpVector, YawRadians);
     const FQuat RollQuat  = FQuat(UpdatedComponent->GetForwardVector(), RollRadians);
     const FQuat TargetQuat = (YawQuat * PitchQuat * RollQuat) * CurrentOrientation;
     UpdatedComponent->SetWorldRotation(FQuat::Slerp(CurrentOrientation, TargetQuat, SteeringAlpha));
     ```
   - Using `FQuat::Slerp` (or `FastLerp` for performance) provides smoother interpolation and avoids gimbal lock.

2. **Decouple axis response**  
   - Introduce per-axis turn rates (`TurnRatePitch`, `TurnRateYaw`, `TurnRateRoll`) and damping to tune responsiveness separately.

3. **Align velocity to orientation**  
   - After steering, align `CurrentVelocity` toward the forward vector (e.g., `CurrentVelocity = FMath::VInterpTo(CurrentVelocity, Forward * CurrentVelocity.Size(), DeltaTime, VelocityAlignmentRate)`) to reduce lateral drift unless we want intentional skids.

4. **Angular inertia**  
   - Track angular velocity (vector) and damp it when inputs cease, giving the craft weight. This works naturally with quaternion math.

5. **Input debugging**  
   - Add `stat flightinput` or log visualisers to track raw vs. smoothed input.  
   - Consider an on-screen widget that shows quaternion vs. Euler deltas to ensure the math behaves in the field.

## Development Workflow Notes

- **Testing**: Use `./Scripts/run_game.sh --map /Game/Maps/PersistentFlightTest -- -Log` to evaluate controls in a staged build (ensures packaged physics match PIE).
- **Tuning**: Expose upcoming turn-rate or damping parameters via `UFlightProjectDeveloperSettings` so designers can tweak in Project Settings without recompiling.
- **Autopilot**: Verify quaternion changes still interact correctly with autopilot agents (`AFlightAIPawn`). They currently call `FlightMovementComponent->SetControlInput`; ensure AI inputs benefit from the same smoothing.

## Next Steps

1. Add debug visualization to confirm orientation and velocity stay coherent during aggressive maneuvers.
2. Gather playtest feedback before finalising turn-rate defaults or exporting tuning to CSV.
3. Consider modelling angular inertia and damping to give the craft additional weight once basic quaternion steering feels solid.

This staged approach keeps current gameplay functional while addressing the “wonky” feel and giving us a path to more physical flight behaviour.
