#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json"
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginPhaseSchema.inc"

POLICIES = {
    "OBSERVABLE": "NEVERC_PHASE_OBSERVABLE",
    "INTERCEPTABLE": "NEVERC_PHASE_INTERCEPTABLE",
    "REPLACEABLE": "NEVERC_PHASE_REPLACEABLE",
    "SKIPPABLE_WITH_PROOF": "NEVERC_PHASE_SKIPPABLE_WITH_PROOF",
    "SEALED_HOST_GATE": "NEVERC_PHASE_SEALED_HOST_GATE",
}
OBSERVER_POINTS = {
    "BEFORE": "NEVERC_OBSERVER_BEFORE",
    "AFTER": "NEVERC_OBSERVER_AFTER",
    "AFTER_COMMIT": "NEVERC_OBSERVER_AFTER_COMMIT",
}
GATES = {
    "transition": "NEVERC_PHASE_GATE_TRANSITION",
    "sealed_verifier": "NEVERC_PHASE_GATE_SEALED_VERIFIER",
    "sealed_commit": "NEVERC_PHASE_GATE_SEALED_COMMIT",
}
STABILITIES = {
    "stable": "NEVERC_PHASE_STABILITY_STABLE",
    "experimental": "NEVERC_PHASE_STABILITY_EXPERIMENTAL",
}
PHASE_KINDS = {
    "transition": "NEVERC_PHASE_KIND_TRANSITION",
    "event": "NEVERC_PHASE_KIND_EVENT",
}


def canonical_name(value):
    return isinstance(value, str) and re.fullmatch(
        r"[a-z0-9]+(?:[._-][a-z0-9]+)*", value
    )


def parse_id(value, field):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{field} must contain two 64-bit values")
    parsed = []
    for item in value:
        if not isinstance(item, str):
            raise ValueError(f"{field} values must be hexadecimal strings")
        number = int(item, 0)
        if number < 0 or number > 0xFFFFFFFFFFFFFFFF:
            raise ValueError(f"{field} value is outside uint64_t")
        parsed.append(number)
    if parsed == [0, 0]:
        raise ValueError(f"{field} must be nonzero")
    return tuple(parsed)


def parse_uint64(value, field):
    if not isinstance(value, str):
        raise ValueError(f"{field} must be a hexadecimal string")
    number = int(value, 0)
    if number < 0 or number > 0xFFFFFFFFFFFFFFFF:
        raise ValueError(f"{field} is outside uint64_t")
    return number


def load_and_validate(schema_path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported phase schema version")
    phases = document.get("phases")
    if not isinstance(phases, list) or not phases:
        raise ValueError("phase schema must contain phases")

    names = set()
    symbols = set()
    ids = set()
    validated = []
    for index, phase in enumerate(phases):
        name = phase.get("name")
        symbol = phase.get("symbol")
        if not canonical_name(name):
            raise ValueError(f"phase {index} has a non-canonical name")
        if not isinstance(symbol, str) or not re.fullmatch(
            r"[A-Z][A-Z0-9_]*", symbol
        ):
            raise ValueError(f"phase {name} has an invalid symbol")
        phase_id = parse_id(phase.get("id"), f"phase {name} ID")
        input_id = parse_id(
            phase.get("input_artifact"), f"phase {name} input artifact"
        )
        output_id = parse_id(
            phase.get("output_artifact"), f"phase {name} output artifact"
        )
        if name in names:
            raise ValueError(f"duplicate phase name: {name}")
        if symbol in symbols:
            raise ValueError(f"duplicate phase symbol: {symbol}")
        if phase_id in ids:
            raise ValueError(f"duplicate phase ID: {name}")
        names.add(name)
        symbols.add(symbol)
        ids.add(phase_id)

        domain = phase.get("domain")
        if not canonical_name(domain):
            raise ValueError(f"phase {name} has an invalid domain")
        kind = phase.get("kind")
        if kind not in PHASE_KINDS:
            raise ValueError(f"phase {name} has an invalid kind")
        verifier = phase.get("verifier")
        if not canonical_name(verifier):
            raise ValueError(
                f"phase {name} has no canonical verifier"
            )

        policy = phase.get("policy")
        if not isinstance(policy, list) or not policy:
            raise ValueError(f"phase {name} has no policy")
        if len(policy) != len(set(policy)) or any(
            item not in POLICIES for item in policy
        ):
            raise ValueError(f"phase {name} has an invalid policy")
        if policy != sorted(policy, key=list(POLICIES).index):
            raise ValueError(f"phase {name} policy order is unstable")
        points = phase.get("observer_points")
        if not isinstance(points, list) or len(points) != len(set(points)):
            raise ValueError(f"phase {name} has invalid observer points")
        if any(item not in OBSERVER_POINTS for item in points):
            raise ValueError(f"phase {name} has an unknown observer point")
        if points != sorted(points, key=list(OBSERVER_POINTS).index):
            raise ValueError(
                f"phase {name} observer point order is unstable"
            )
        if "OBSERVABLE" not in policy and points:
            raise ValueError(
                f"phase {name} exposes observer points without OBSERVABLE"
            )
        if "SEALED_HOST_GATE" in policy and any(
            item in policy
            for item in (
                "INTERCEPTABLE",
                "REPLACEABLE",
                "SKIPPABLE_WITH_PROOF",
            )
        ):
            raise ValueError(f"sealed phase {name} has replaceable policy")
        if kind == "event" and policy != ["OBSERVABLE"]:
            raise ValueError(
                f"event phase {name} must be observer-only"
            )
        if "REPLACEABLE" in policy and not phase.get("builtin_fallback"):
            raise ValueError(
                f"replaceable phase {name} has no builtin fallback"
            )
        gate = phase.get("gate")
        if gate not in GATES:
            raise ValueError(f"phase {name} has an invalid gate kind")
        if gate.startswith("sealed_") != ("SEALED_HOST_GATE" in policy):
            raise ValueError(f"phase {name} gate and policy disagree")
        if phase.get("stability") not in STABILITIES:
            raise ValueError(f"phase {name} has invalid stability")
        if not isinstance(phase.get("builtin_fallback"), bool):
            raise ValueError(
                f"phase {name} has invalid builtin fallback marker"
            )
        if kind == "event" and phase.get("builtin_fallback"):
            raise ValueError(
                f"event phase {name} must not have a builtin fallback"
            )
        if "AFTER_COMMIT" in points and gate != "sealed_commit":
            raise ValueError(
                f"phase {name} exposes AFTER_COMMIT outside sealed commit"
            )

        validated.append(
            {
                **phase,
                "id": phase_id,
                "input_artifact": input_id,
                "output_artifact": output_id,
            }
        )
    if [phase["id"] for phase in validated] != sorted(
        phase["id"] for phase in validated
    ):
        raise ValueError("phase schema order must be stable by phase ID")

    families = document.get("extension_families")
    if not isinstance(families, list) or not families:
        raise ValueError("phase schema must declare extension families")
    family_names = set()
    family_highs = set()
    validated_families = []
    for index, family in enumerate(families):
        namespace = family.get("namespace")
        if not canonical_name(namespace):
            raise ValueError(
                f"extension family {index} has invalid namespace"
            )
        high = parse_uint64(
            family.get("id_high"), f"extension family {namespace} high ID"
        )
        low_min = parse_uint64(
            family.get("id_low_min"),
            f"extension family {namespace} minimum low ID",
        )
        low_max = parse_uint64(
            family.get("id_low_max"),
            f"extension family {namespace} maximum low ID",
        )
        if (
            high <= 0
            or high > 0xFFFFFFFFFFFFFFFF
            or low_min <= 0
            or low_max > 0xFFFFFFFFFFFFFFFF
            or low_min > low_max
        ):
            raise ValueError(
                f"extension family {namespace} has invalid ID range"
            )
        if namespace in family_names or high in family_highs:
            raise ValueError(
                f"duplicate extension family: {namespace}"
            )
        if any(
            phase["id"][0] == high
            and low_min <= phase["id"][1] <= low_max
            for phase in validated
        ):
            raise ValueError(
                f"extension family {namespace} overlaps builtin phases"
            )
        family_names.add(namespace)
        family_highs.add(high)
        validated_families.append(
            {
                "namespace": namespace,
                "id_high": high,
                "id_low_min": low_min,
                "id_low_max": low_max,
            }
        )
    if [family["id_high"] for family in validated_families] != sorted(
        family["id_high"] for family in validated_families
    ):
        raise ValueError("extension family order must be stable by high ID")
    return validated, validated_families


def uint64(value):
    return f"UINT64_C(0x{value:016x})"


def joined_flags(values, mapping, zero):
    if not values:
        return zero
    return " | ".join(mapping[value] for value in values)


def generate(phases, families):
    domains = {}
    for phase in phases:
        domains.setdefault(phase["domain"], []).append(phase)
    lines = [
        "/* Generated by utils/plugin-api/gen-phase-schema.py. */",
        "/* Do not edit manually. */",
        "",
        f"#define NEVERC_BUILTIN_PHASE_COUNT UINT32_C({len(phases)})",
        f"#define NEVERC_EXTENSION_FAMILY_COUNT UINT32_C({len(families)})",
        "",
    ]
    for domain, domain_phases in sorted(domains.items()):
        symbol = domain.upper().replace("-", "_")
        lines.append(
            f"#define NEVERC_BUILTIN_{symbol}_PHASE_COUNT "
            f"UINT32_C({len(domain_phases)})"
        )
    lines.append("")
    for index, family in enumerate(families):
        prefix = f"NEVERC_EXTENSION_FAMILY_{index}"
        lines.extend(
            [
                f'#define {prefix}_NAMESPACE "{family["namespace"]}"',
                f"#define {prefix}_ID_HIGH {uint64(family['id_high'])}",
                f"#define {prefix}_ID_LOW_MIN "
                f"{uint64(family['id_low_min'])}",
                f"#define {prefix}_ID_LOW_MAX "
                f"{uint64(family['id_low_max'])}",
                "",
            ]
        )
    for phase in phases:
        prefix = f"NEVERC_PHASE_{phase['symbol']}"
        lines.extend(
            [
                f'#define {prefix}_NAME "{phase["name"]}"',
                f'#define {prefix}_DOMAIN "{phase["domain"]}"',
                f'#define {prefix}_VERIFIER "{phase["verifier"]}"',
                f"#define {prefix}_KIND {PHASE_KINDS[phase['kind']]}",
                f"#define {prefix}_HIGH {uint64(phase['id'][0])}",
                f"#define {prefix}_LOW {uint64(phase['id'][1])}",
                f"#define {prefix}_INPUT_HIGH "
                f"{uint64(phase['input_artifact'][0])}",
                f"#define {prefix}_INPUT_LOW "
                f"{uint64(phase['input_artifact'][1])}",
                f"#define {prefix}_OUTPUT_HIGH "
                f"{uint64(phase['output_artifact'][0])}",
                f"#define {prefix}_OUTPUT_LOW "
                f"{uint64(phase['output_artifact'][1])}",
                f"#define {prefix}_POLICY "
                f"({joined_flags(phase['policy'], POLICIES, 'UINT64_C(0)')})",
                f"#define {prefix}_OBSERVER_POINTS "
                f"({joined_flags(phase['observer_points'], OBSERVER_POINTS, 'UINT32_C(0)')})",
                f"#define {prefix}_GATE {GATES[phase['gate']]}",
                f"#define {prefix}_STABILITY "
                f"{STABILITIES[phase['stability']]}",
                f"#define {prefix}_BUILTIN_FALLBACK "
                f"{'NEVERC_TRUE' if phase['builtin_fallback'] else 'NEVERC_FALSE'}",
                "",
            ]
        )
    lines.append("#define NEVERC_FOR_EACH_BUILTIN_PHASE(M) \\")
    for index, phase in enumerate(phases):
        suffix = " \\" if index + 1 != len(phases) else ""
        lines.append(f"  M({phase['symbol']}){suffix}")
    lines.append("")
    for domain, domain_phases in sorted(domains.items()):
        symbol = domain.upper().replace("-", "_")
        lines.append(f"#define NEVERC_FOR_EACH_BUILTIN_{symbol}_PHASE(M) \\")
        for index, phase in enumerate(domain_phases):
            suffix = " \\" if index + 1 != len(domain_phases) else ""
            lines.append(f"  M({phase['symbol']}){suffix}")
        lines.append("")
    if lines[-1] == "":
        lines.pop()
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check", action="store_true", help="fail if generated output differs"
    )
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    arguments = parser.parse_args()
    try:
        phases, families = load_and_validate(arguments.schema)
        generated = generate(phases, families)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"phase schema error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        try:
            existing = arguments.output.read_text(encoding="utf-8")
        except OSError:
            existing = ""
        if existing != generated:
            print(f"{arguments.output} is out of date", file=sys.stderr)
            return 1
        return 0
    arguments.output.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
