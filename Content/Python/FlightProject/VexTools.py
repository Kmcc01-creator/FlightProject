"""Schema-driven VEX helper tools for editor scripting and diagnostics."""

from __future__ import annotations

import json
import re
from pathlib import Path

import unreal

from . import SchemaTools


DEFAULT_REPORT_RELATIVE_PATH = "Saved/Flight/Schema/vex_validation_report.json"
_VEX_USAGE_PATTERN = re.compile(r"@[A-Za-z_][A-Za-z0-9_]*")


def extract_symbols(source: str) -> list[str]:
    """Return sorted unique VEX symbols referenced in source text."""
    symbols = {match.group(0) for match in _VEX_USAGE_PATTERN.finditer(source or "")}
    return sorted(symbols)


def get_contract_symbols() -> list[str]:
    """Return sorted symbol names declared in schema manifest."""
    return sorted(SchemaTools.get_vex_symbol_table().keys())


def validate_source(source: str, require_all_required_symbols: bool = False) -> dict:
    """
    Validate VEX source against schema contract.
    Returns a structured report.
    """
    manifest = SchemaTools.get_manifest_data()
    contract_issues = SchemaTools.validate_vex_symbol_contracts(manifest)
    symbol_table = SchemaTools.get_vex_symbol_table(manifest)

    used_symbols = extract_symbols(source)
    known_symbols = set(symbol_table.keys())
    unknown_symbols = sorted(symbol for symbol in used_symbols if symbol not in known_symbols)

    missing_required_symbols = []
    if require_all_required_symbols:
        for symbol_name, contract in symbol_table.items():
            if bool(contract.get("required", True)) and symbol_name not in used_symbols:
                missing_required_symbols.append(symbol_name)
        missing_required_symbols.sort()

    is_valid = not contract_issues and not unknown_symbols and not missing_required_symbols
    return {
        "valid": is_valid,
        "usedSymbols": used_symbols,
        "knownSymbols": sorted(known_symbols),
        "unknownSymbols": unknown_symbols,
        "missingRequiredSymbols": missing_required_symbols,
        "contractIssues": contract_issues,
    }


def lower_source(source: str, target: str = "hlsl") -> str:
    """
    Replace VEX symbols using schema mappings for requested target.
    target: 'hlsl' or 'verse'
    """
    normalized_target = (target or "hlsl").strip().lower()
    if normalized_target not in {"hlsl", "verse"}:
        raise ValueError(f"Unsupported target '{target}', expected 'hlsl' or 'verse'")

    field_name = "hlslIdentifier" if normalized_target == "hlsl" else "verseIdentifier"
    lowered = source or ""
    symbol_table = SchemaTools.get_vex_symbol_table()
    for symbol_name, contract in symbol_table.items():
        lowered_identifier = str(contract.get(field_name, "")).strip()
        if lowered_identifier:
            lowered = lowered.replace(symbol_name, lowered_identifier)
    return lowered


def export_validation_report(
    source: str,
    relative_output_path: str = DEFAULT_REPORT_RELATIVE_PATH,
    require_all_required_symbols: bool = False,
) -> str:
    """
    Validate source and write a json report under Saved/.
    Returns absolute path.
    """
    report = validate_source(source, require_all_required_symbols=require_all_required_symbols)
    report["loweredHlsl"] = lower_source(source, target="hlsl")
    report["loweredVerse"] = lower_source(source, target="verse")

    output_path = Path(unreal.Paths.project_dir()) / relative_output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log(f"VexTools: exported validation report -> {output_path}")
    return str(output_path)


def compile_vex(source: str, behavior_id: int = 0) -> tuple[bool, str]:
    """
    Compile VEX source via the C++ Verse Subsystem.
    Returns (success, errors_or_verse).
    """
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        return False, "No editor world"
    
    success, errors = unreal.FlightScriptingLibrary.compile_vex(world, behavior_id, source)
    return success, errors


def recompile_file(file_path: str) -> bool:
    """Read a .vex file and compile it."""
    path = Path(file_path)
    if not path.exists():
        unreal.log_error(f"VexTools: File not found {file_path}")
        return False
    
    # Try to extract BehaviorID from filename (e.g. Behavior_1.vex)
    behavior_id = 0
    match = re.search(r"Behavior_(\d+)", path.name, re.IGNORECASE)
    if match:
        behavior_id = int(match.group(1))

    source = path.read_text(encoding="utf-8")
    unreal.log(f"VexTools: Recompiling {path.name} (ID={behavior_id})...")
    
    success, result = compile_vex(source, behavior_id)
    if success:
        unreal.log(f"VexTools: Successfully compiled {path.name}")
        # result contains the generated verse
        return True
    else:
        unreal.log_error(f"VexTools: Failed to compile {path.name}:\n{result}")
        return False
