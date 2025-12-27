# Data-Driven Design

This document covers strategies for keeping game parameters outside of code, enabling rapid iteration without recompilation.

## Philosophy: Separate Data from Logic

```
┌─────────────────────────────────────────────────────────────┐
│  TRADITIONAL (Hardcoded)                                    │
│                                                             │
│  void SpawnSwarm() {                                        │
│      int Count = 100;      // <-- Change requires rebuild  │
│      float Speed = 1500.f; // <-- Change requires rebuild  │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  DATA-DRIVEN                                                │
│                                                             │
│  CSV File (Content/Data/SwarmConfig.csv):                   │
│  ┌─────────────────────────────────────┐                   │
│  │ Count,Speed,Formation               │                   │
│  │ 100,1500,Circle                     │ ← Edit in text    │
│  └─────────────────────────────────────┘   No rebuild!     │
│                                                             │
│  void SpawnSwarm(const FSwarmConfig& Config) {              │
│      // Uses Config.Count, Config.Speed                     │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
```

## The Data Hierarchy

| Layer | Format | Use Case | Hot-Reload? |
|-------|--------|----------|-------------|
| **Developer Settings** | C++ `UDeveloperSettings` | Build-time constants, paths | No |
| **Data Assets** | `.uasset` (UDataAsset) | Designer-tunable presets | Editor restart |
| **CSV Files** | `.csv` text files | Runtime parameters, batch configs | Yes |
| **INI Files** | `.ini` in Config/ | Engine/game configuration | Sometimes |

## CSV Workflow

### File Structure

```
Content/Data/
├── FlightAutopilotConfig.csv    # Drone behavior parameters
├── FlightLightingConfig.csv     # Visual lighting settings
├── FlightSpatialLayout.csv      # Obstacle/landmark placement
└── FlightSpatialProcedural.csv  # Procedural generation params
```

### CSV Schema Example

**FlightAutopilotConfig.csv:**
```csv
ConfigName,DroneCount,Speed,LightColor,LightIntensity,PathLoop
Default,100,1500.0,"(R=0.5,G=0.8,B=1.0)",8000,true
Aggressive,50,2500.0,"(R=1.0,G=0.3,B=0.1)",12000,false
Patrol,200,800.0,"(R=0.2,G=1.0,B=0.2)",5000,true
```

### Loading CSV in C++

```cpp
// FlightDataSubsystem.h
UCLASS()
class UFlightDataSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Data")
    bool LoadAutopilotConfig(const FString& ConfigName, FFlightAutopilotConfig& OutConfig);

    UFUNCTION(BlueprintCallable, Category = "Data")
    void ReloadAllConfigs();

private:
    TMap<FName, FFlightAutopilotConfig> AutopilotConfigs;

    void ParseAutopilotCSV();
};
```

```cpp
// FlightDataSubsystem.cpp
void UFlightDataSubsystem::ParseAutopilotCSV()
{
    FString CSVPath = FPaths::ProjectContentDir() / TEXT("Data/FlightAutopilotConfig.csv");
    TArray<FString> Lines;

    if (FFileHelper::LoadFileToStringArray(Lines, *CSVPath))
    {
        // Skip header row
        for (int32 i = 1; i < Lines.Num(); ++i)
        {
            TArray<FString> Columns;
            Lines[i].ParseIntoArray(Columns, TEXT(","));

            if (Columns.Num() >= 6)
            {
                FFlightAutopilotConfig Config;
                Config.ConfigName = FName(*Columns[0]);
                Config.DroneCount = FCString::Atoi(*Columns[1]);
                Config.Speed = FCString::Atof(*Columns[2]);
                // Parse color, intensity, etc.

                AutopilotConfigs.Add(Config.ConfigName, Config);
            }
        }
    }
}
```

### Hot-Reload Support

```cpp
void UFlightDataSubsystem::ReloadAllConfigs()
{
    AutopilotConfigs.Empty();
    ParseAutopilotCSV();

    // Notify listeners
    OnConfigsReloaded.Broadcast();

    UE_LOG(LogFlight, Log, TEXT("Reloaded %d autopilot configs"), AutopilotConfigs.Num());
}
```

**Python trigger:**
```python
unreal.FlightDataSubsystem.reload_all_configs()
```

## Data Assets

### When to Use Data Assets

- Complex structures that benefit from editor UI
- Assets referenced by multiple systems
- Designer-facing configuration with validation
- Preset libraries (Easy/Medium/Hard swarm configs)

### Creating a Data Asset

**1. Define the C++ class:**
```cpp
// FlightSwarmConfig.h
UCLASS(BlueprintType)
class UFlightSwarmConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
    int32 DroneCount = 100;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
    float DefaultSpeed = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    FLinearColor LightColor = FLinearColor(0.5f, 0.8f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    float LightIntensity = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
    bool bLoopPath = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
    UCurveFloat* SpeedCurve;  // Optional: speed over path distance
};
```

**2. Create instances in Editor:**
- Right-click in Content Browser
- Miscellaneous → Data Asset
- Select `FlightSwarmConfig`
- Name: `DA_SwarmDefault`, `DA_SwarmAggressive`, etc.

**3. Reference in code:**
```cpp
UPROPERTY(EditAnywhere, Category = "Config")
TObjectPtr<UFlightSwarmConfig> SwarmConfig;

void SpawnSwarm()
{
    if (SwarmConfig)
    {
        UFlightMassSpawning::SpawnFlightEntities(this, SwarmConfig);
    }
}
```

## Developer Settings

### For Build-Time Constants

```cpp
// FlightProjectDeveloperSettings.h
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Flight Project"))
class UFlightProjectDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    // Paths (don't change at runtime)
    UPROPERTY(config, EditAnywhere, Category = "Paths")
    FString CSVDataPath = TEXT("Data/");

    // Performance tuning
    UPROPERTY(config, EditAnywhere, Category = "Performance")
    int32 MaxEntitiesPerBatch = 1000;

    UPROPERTY(config, EditAnywhere, Category = "Performance")
    float LODTransitionDistance = 5000.f;

    // Debug
    UPROPERTY(config, EditAnywhere, Category = "Debug")
    bool bEnableSwarmDebugDraw = false;

    // Access from anywhere
    static const UFlightProjectDeveloperSettings* Get()
    {
        return GetDefault<UFlightProjectDeveloperSettings>();
    }
};
```

**Access:**
```cpp
int32 BatchSize = UFlightProjectDeveloperSettings::Get()->MaxEntitiesPerBatch;
```

**Editor location:** Project Settings → Game → Flight Project

## Combining Approaches

### Layered Configuration

```
┌─────────────────────────────────────────────────────────────┐
│  Developer Settings (C++)                                   │
│  └── MaxEntitiesPerBatch = 1000                             │
│      └── Hard limit, affects memory allocation              │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  Data Asset (DA_SwarmDefault.uasset)                        │
│  └── DroneCount = 100, Speed = 1500                         │
│      └── Designer preset, selectable in editor              │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  CSV Override (FlightAutopilotConfig.csv)                   │
│  └── Row "TestOverride": DroneCount = 500                   │
│      └── Hot-reloadable for rapid iteration                 │
└─────────────────────────────────────────────────────────────┘
```

### Resolution Order

```cpp
FFlightAutopilotConfig GetEffectiveConfig(UFlightSwarmConfig* BaseConfig, FName OverrideName)
{
    FFlightAutopilotConfig Result;

    // 1. Start with Data Asset defaults
    if (BaseConfig)
    {
        Result.DroneCount = BaseConfig->DroneCount;
        Result.Speed = BaseConfig->DefaultSpeed;
    }

    // 2. Apply CSV override if specified
    FFlightAutopilotConfig CSVOverride;
    if (DataSubsystem->LoadAutopilotConfig(OverrideName, CSVOverride))
    {
        Result = CSVOverride;  // CSV takes precedence
    }

    // 3. Clamp to developer settings limits
    Result.DroneCount = FMath::Min(Result.DroneCount,
        UFlightProjectDeveloperSettings::Get()->MaxEntitiesPerBatch);

    return Result;
}
```

## Validation

### CSV Validation Script

```python
# Content/Python/FlightProject/Validation.py
import unreal
import csv
import os

def validate_autopilot_csv():
    """Validate FlightAutopilotConfig.csv for common issues."""
    csv_path = os.path.join(unreal.Paths.project_content_dir(), "Data/FlightAutopilotConfig.csv")
    issues = []

    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row_num, row in enumerate(reader, start=2):  # Header is row 1
            # Check required fields
            if not row.get('ConfigName'):
                issues.append(f"Row {row_num}: Missing ConfigName")

            # Validate ranges
            try:
                count = int(row.get('DroneCount', 0))
                if count <= 0 or count > 10000:
                    issues.append(f"Row {row_num}: DroneCount {count} out of range (1-10000)")
            except ValueError:
                issues.append(f"Row {row_num}: Invalid DroneCount")

            try:
                speed = float(row.get('Speed', 0))
                if speed <= 0:
                    issues.append(f"Row {row_num}: Speed must be positive")
            except ValueError:
                issues.append(f"Row {row_num}: Invalid Speed")

    if issues:
        for issue in issues:
            unreal.log_warning(issue)
        return False
    else:
        unreal.log("CSV validation passed")
        return True
```

## Best Practices

| Do | Don't |
|----|-------|
| Use CSV for frequently-tweaked values | Hardcode spawn counts in C++ |
| Use Data Assets for preset libraries | Store complex configs in INI files |
| Validate CSV on load | Trust all CSV data blindly |
| Provide sensible defaults | Require all fields to be specified |
| Document CSV schema in comments | Leave undocumented magic columns |
| Hot-reload during development | Restart editor to see changes |
