import json
import os
import re

import unreal


DEFAULT_MANIFEST_RELATIVE_PATH = "Saved/Flight/Schema/requirements_manifest.json"
_VEX_SYMBOL_PATTERN = re.compile(r"^@[A-Za-z_][A-Za-z0-9_]*$")
_VEX_VALUE_TYPES = {"float", "float2", "float3", "float4", "int", "bool"}


def export_manifest(relative_output_path: str = DEFAULT_MANIFEST_RELATIVE_PATH) -> str:
    """
    Export the code-first schema manifest via C++ and return absolute output path.
    """
    output_path = unreal.FlightScriptingLibrary.export_requirement_manifest(relative_output_path)
    if output_path:
        unreal.log(f"SchemaTools: exported manifest -> {output_path}")
    else:
        unreal.log_error("SchemaTools: failed to export requirement manifest")
    return output_path


def get_manifest_data() -> dict:
    """
    Return manifest data directly from C++ in-memory JSON.
    """
    manifest_json = unreal.FlightScriptingLibrary.get_requirement_manifest_json()
    if not manifest_json:
        raise RuntimeError("SchemaTools: manifest JSON is empty")
    return json.loads(manifest_json)


def _object_path_to_asset_path(object_path: str) -> str:
    if not object_path:
        return ""
    return object_path.split(".", 1)[0]


def _split_asset_path(asset_path: str):
    package_path, asset_name = asset_path.rsplit("/", 1)
    return package_path, asset_name


def _create_niagara_system(asset_path: str, template_path: str = "") -> bool:
    template_asset_path = _object_path_to_asset_path(template_path)
    if template_asset_path and unreal.EditorAssetLibrary.does_asset_exist(template_asset_path):
        if unreal.EditorAssetLibrary.duplicate_asset(template_asset_path, asset_path):
            unreal.log(f"SchemaTools: duplicated Niagara template {template_asset_path} -> {asset_path}")
            unreal.EditorAssetLibrary.save_asset(asset_path)
            return True

        unreal.log_warning(
            f"SchemaTools: failed duplicating Niagara template {template_asset_path}, falling back to new asset"
        )

    package_path, asset_name = _split_asset_path(asset_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.NiagaraSystemFactoryNew()
    asset = asset_tools.create_asset(asset_name, package_path, unreal.NiagaraSystem, factory)
    if not asset:
        return False

    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"SchemaTools: created Niagara system {asset_path}")
    return True


def _ensure_asset_requirement(requirement: dict, create_missing: bool) -> list:
    issues = []
    requirement_id = requirement.get("requirementId", "unknown")
    asset_class = requirement.get("assetClass", "Object")
    object_path = requirement.get("assetPath", "")
    template_path = requirement.get("templatePath", "")

    asset_path = _object_path_to_asset_path(object_path)
    if not asset_path:
        issues.append(f"{requirement_id}: missing assetPath")
        return issues

    exists = unreal.EditorAssetLibrary.does_asset_exist(asset_path)
    if not exists and create_missing:
        if asset_class == "NiagaraSystem":
            if not _create_niagara_system(asset_path, template_path):
                issues.append(f"{requirement_id}: failed creating NiagaraSystem {asset_path}")
            else:
                exists = True
        else:
            issues.append(
                f"{requirement_id}: unsupported auto-create class '{asset_class}' for {asset_path}"
            )

    if not exists:
        issues.append(f"{requirement_id}: required asset missing -> {asset_path}")
        return issues

    required_properties = requirement.get("requiredProperties", {})
    for key in required_properties.keys():
        # Property-level validation is intentionally conservative in this PoC.
        # We expose the contract now and tighten checks as coverage expands.
        unreal.log(f"SchemaTools: contract property declared for {requirement_id}: {key}")

    return issues


def _validate_niagara_requirement(requirement: dict, create_missing: bool) -> list:
    issues = []
    requirement_id = requirement.get("requirementId", "unknown")
    system_object_path = requirement.get("systemPath", "")
    system_path = _object_path_to_asset_path(system_object_path)
    if not system_object_path or not system_path:
        issues.append(f"{requirement_id}: missing systemPath")
        return issues

    if not unreal.EditorAssetLibrary.does_asset_exist(system_path):
        issues.append(f"{requirement_id}: Niagara system missing -> {system_path}")
        return issues

    required_user_parameters = requirement.get("requiredUserParameters", [])
    required_data_interfaces = requirement.get("requiredDataInterfaces", [])
    
    if create_missing:
        # Call the new C++ function to author missing parameters/interfaces
        contract_issues = unreal.FlightScriptingLibrary.ensure_niagara_system_contract(
            system_object_path, required_user_parameters, required_data_interfaces
        )
    else:
        contract_issues = unreal.FlightScriptingLibrary.validate_niagara_system_contract(
            system_object_path, required_user_parameters, required_data_interfaces
        )
        
    for issue in contract_issues:
        issues.append(f"{requirement_id}: {issue}")

    return issues


def get_vex_symbol_table(manifest: dict | None = None) -> dict:
    """
    Return symbol->contract map from manifest vexSymbolRequirements.
    """
    if manifest is None:
        manifest = get_manifest_data()

    table = {}
    for requirement in manifest.get("vexSymbolRequirements", []):
        symbol_name = str(requirement.get("symbolName", "")).strip()
        if symbol_name:
            table[symbol_name] = requirement
    return table


def validate_vex_symbol_contracts(manifest: dict | None = None) -> list:
    """
    Validate VEX symbol contracts for format/consistency.
    """
    if manifest is None:
        manifest = get_manifest_data()

    issues = []
    seen_symbols = set()
    for requirement in manifest.get("vexSymbolRequirements", []):
        requirement_id = str(requirement.get("requirementId", "unknown")).strip() or "unknown"
        symbol_name = str(requirement.get("symbolName", "")).strip()
        value_type = str(requirement.get("valueType", "")).strip().lower()
        hlsl_identifier = str(requirement.get("hlslIdentifier", "")).strip()
        verse_identifier = str(requirement.get("verseIdentifier", "")).strip()

        if not symbol_name:
            issues.append(f"{requirement_id}: missing symbolName")
            continue

        if not _VEX_SYMBOL_PATTERN.match(symbol_name):
            issues.append(f"{requirement_id}: invalid symbolName '{symbol_name}'")

        if symbol_name in seen_symbols:
            issues.append(f"{requirement_id}: duplicate symbolName '{symbol_name}'")
        seen_symbols.add(symbol_name)

        if value_type not in _VEX_VALUE_TYPES:
            issues.append(
                f"{requirement_id}: unknown valueType '{value_type}'"
            )

        if not hlsl_identifier:
            issues.append(f"{requirement_id}: missing hlslIdentifier for symbol '{symbol_name}'")
        if not verse_identifier:
            issues.append(f"{requirement_id}: missing verseIdentifier for symbol '{symbol_name}'")

    return issues


def ensure_manifest_requirements(manifest_path: str = "", create_missing: bool = True) -> list:
    """
    Export/read the requirement manifest and ensure required assets exist.
    Safe to run repeatedly; intended for editor startup and CI validation.
    """
    if not manifest_path:
        manifest_path = export_manifest(DEFAULT_MANIFEST_RELATIVE_PATH)
        if not manifest_path:
            return ["manifest export failed"]

    if not os.path.exists(manifest_path):
        return [f"manifest path missing: {manifest_path}"]

    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    issues = []
    for requirement in manifest.get("assetRequirements", []):
        issues.extend(_ensure_asset_requirement(requirement, create_missing))

    for requirement in manifest.get("niagaraRequirements", []):
        issues.extend(_validate_niagara_requirement(requirement, create_missing))

    issues.extend(validate_vex_symbol_contracts(manifest))

    if issues:
        for issue in issues:
            unreal.log_warning(f"SchemaTools: {issue}")
    else:
        unreal.log("SchemaTools: requirement manifest validated cleanly")

    return issues
