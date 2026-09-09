#!/usr/bin/env python3
"""Prove COFF weak fallback closure using the pinned llvm-readobj text schema.

llvm-nm does not expose auxiliary TagIndex fallback edges. In LLVM 20.1.8,
COFFDumper::printSymbol emits these in AuxWeakExternal blocks. Read the original
LLVM text: that release's COFF dumper does not implement a valid JSON file scope.
"""

from dataclasses import dataclass
import os
import re
import subprocess
import tempfile
import time


MAX_LINE = 1024 * 1024
MAX_OUTPUT = 4 * 1024 * 1024 * 1024
MAX_STDERR = 1024 * 1024
MAX_SYMBOLS_PER_MEMBER = 1000000
MAX_ALIASES = 1000000
MAX_CHAIN = 256
READ_TIMEOUT = 900


@dataclass
class Symbol:
    name: str
    storage: int
    section: int
    value: int
    aux_count: int
    linked: object = None
    search: object = None


@dataclass(frozen=True)
class Edge:
    target: str
    local_definition: bool
    member: str
    search: int


def _integer(value):
    if not re.fullmatch(r"[0-9]+", value):
        raise ValueError("Invalid COFF decimal field: " + value)
    return int(value)


def _named_number(value, hexadecimal=False):
    number = r"0x[0-9A-Fa-f]+" if hexadecimal else r"-?[0-9]+"
    match = re.fullmatch(r"(.*) \((" + number + r")\)", value)
    if not match:
        raise ValueError("Invalid COFF named numeric field: " + value)
    return match[1], int(match[2], 16 if hexadecimal else 10)


def _enum(value):
    if re.fullmatch(r"0x[0-9A-Fa-f]+", value):
        return int(value, 16)
    return _named_number(value, True)[1]


def _symbol(fields, auxiliaries):
    expected = {"Name", "Value", "Section", "BaseType", "ComplexType",
                "StorageClass", "AuxSymbolCount"}
    if set(fields) != expected:
        raise ValueError("Incomplete or unknown COFF Symbol fields")
    symbol = Symbol(fields["Name"], _enum(fields["StorageClass"]),
                    _named_number(fields["Section"])[1],
                    _integer(fields["Value"]), _integer(fields["AuxSymbolCount"]))
    _enum(fields["BaseType"])
    _enum(fields["ComplexType"])
    if symbol.aux_count > 255:
        raise ValueError("Invalid COFF auxiliary symbol count")
    weak = [data for kind, data in auxiliaries if kind == "AuxWeakExternal"]
    if symbol.storage == 105 or weak:
        if (symbol.storage != 105 or symbol.section != 0
                or symbol.value != 0 or symbol.aux_count != 1
                or len(auxiliaries) != 1 or len(weak) != 1
                or set(weak[0]) != {"Linked", "Search"} or not symbol.name):
            raise ValueError("Malformed COFF weak external: " + symbol.name)
        symbol.linked = _named_number(weak[0]["Linked"])
        search_name, symbol.search = _named_number(weak[0]["Search"], True)
        if {1: "NoLibrary", 2: "Library", 3: "Alias"}.get(symbol.search) != search_name:
            raise ValueError("Unsupported COFF weak search policy: " + weak[0]["Search"])
    elif bool(symbol.aux_count) != bool(auxiliaries):
        raise ValueError("Missing or unexpected COFF auxiliary record")
    return symbol


def _field(line, indent, fields):
    if not line.startswith(" " * indent) or line.startswith(" " * (indent + 1)):
        raise ValueError("Unexpected indentation in llvm-readobj symbols")
    name, separator, value = line[indent:].partition(": ")
    if not separator or not name or name in fields:
        raise ValueError("Malformed or duplicate llvm-readobj field: " + line)
    fields[name] = value


def _member_edges(member, symbols, definitions, real_definitions, definition_sources,
                  edges, edge_count):
    for symbol in symbols.values():
        if (symbol.storage == 2 and symbol.linked is None
                and (symbol.section > 0 or (symbol.section == 0 and symbol.value > 0))
                and symbol.name in definitions):
            real_definitions.add(symbol.name)
            definition_sources.setdefault(symbol.name, member)
        if symbol.linked is None:
            continue
        target_name, target_index = symbol.linked
        target = symbols.get(target_index)
        if target is None or target.name != target_name:
            raise ValueError("Invalid COFF fallback TagIndex/name for " + symbol.name +
                             " in " + member)
        if not target_name:
            raise ValueError("Unnamed COFF fallback target in " + member)
        local = target.storage == 3 and target.section > 0 and target.linked is None
        if not local and target.storage not in (2, 105):
            raise ValueError("COFF fallback has no external or local definition: " +
                             symbol.name + " -> " + target_name + " in " + member)
        edge = Edge(target_name, local, member, symbol.search)
        previous = edges.setdefault(symbol.name, [])
        if previous and (previous[0].target, previous[0].local_definition) != (
                edge.target, edge.local_definition):
            raise ValueError("Conflicting COFF fallback targets: " + symbol.name)
        previous.append(edge)
        edge_count[0] += 1
        if edge_count[0] > MAX_ALIASES:
            raise ValueError("COFF weak alias inventory exceeds its limit")


def parse_resolved_aliases(lines, definitions, private_names=(), *, expected_fallbacks=None):
    """Consume complete llvm-readobj --symbols output without retaining its text."""
    state = "header"
    header = {}
    symbols = {}
    fields = {}
    auxiliaries = []
    aux_fields = {}
    aux_kind = ""
    symbol_index = 0
    member_count = 0
    real_definitions = set()
    definition_sources = {}
    edges = {}
    edge_count = [0]
    aux_schema = {
        "AuxFunctionDef": {"TagIndex", "TotalSize", "PointerToLineNumber", "PointerToNextFunction"},
        "AuxWeakExternal": {"Linked", "Search"},
        "AuxFileRecord": {"FileName"},
        "AuxSectionDef": {"Length", "RelocationCount", "LineNumberCount", "Checksum",
                          "Number", "Selection", "AssocSection"},
        "AuxCLRToken": {"AuxType", "Reserved", "SymbolTableIndex"},
    }
    for raw_line in lines:
        if len(raw_line) > MAX_LINE:
            raise ValueError("llvm-readobj symbol line exceeds its limit")
        line = raw_line.rstrip("\r\n")
        if any(ord(char) < 32 or ord(char) == 127 for char in line):
            raise ValueError("Control character in llvm-readobj symbol inventory")
        if not line:
            continue
        if state == "header":
            if line == "Symbols [":
                if (set(header) != {"File", "Format", "Arch", "AddressSize"}
                        or not header["File"] or not header["Format"].startswith("COFF-")
                        or "ARM64EC" in header["Format"]):
                    raise ValueError("Missing or unsupported COFF member header")
                state = "symbols"
                symbols = {}
                symbol_index = 0
            else:
                _field(line, 0, header)
        elif state == "symbols":
            if line == "  Symbol {":
                fields, auxiliaries = {}, []
                state = "symbol"
            elif line == "]":
                _member_edges(header["File"], symbols, definitions, real_definitions,
                              definition_sources, edges, edge_count)
                member_count += 1
                state, header, symbols = "header", {}, {}
            else:
                raise ValueError("Unexpected COFF Symbols entry: " + line)
        elif state == "symbol":
            if line == "  }":
                symbol = _symbol(fields, auxiliaries)
                symbols[symbol_index] = symbol
                symbol_index += 1 + symbol.aux_count
                if len(symbols) > MAX_SYMBOLS_PER_MEMBER:
                    raise ValueError("COFF member symbol inventory exceeds its limit")
                state = "symbols"
            elif line.startswith("    Aux") and line.endswith(" {"):
                aux_kind = line[4:-2]
                if aux_kind not in aux_schema:
                    raise ValueError("Unknown COFF auxiliary block: " + aux_kind)
                aux_fields = {}
                state = "aux"
            else:
                _field(line, 4, fields)
        else:
            if line == "    }":
                if not aux_fields or not set(aux_fields) <= aux_schema[aux_kind]:
                    raise ValueError("Unknown or empty COFF auxiliary fields")
                auxiliaries.append((aux_kind, aux_fields))
                state = "symbol"
            else:
                _field(line, 6, aux_fields)
    if state != "header" or header or not member_count:
        raise ValueError("Truncated or empty COFF symbol inventory")

    # A runtime wrapper exception needs the specific ABI fallback, not merely
    # a chain that happens to end in some private symbol. Verify every member's
    # edge and require an actual external body, independently of nm's index.
    for name, target in (expected_fallbacks or {}).items():
        alternatives = edges.get(name)
        if (not alternatives or name in real_definitions or
                target not in real_definitions or any(
                    edge.target != target or edge.local_definition
                    for edge in alternatives)):
            raise ValueError("Invalid exact COFF fallback: " + name + " -> " + target)

    # nm reports Search=Alias as W/V. Only actual non-alias COFF records above
    # seed this set. An independently emitted real definition does override a
    # weak alias of the same name, as it does in the COFF linker.
    resolved = set()
    failed = {}

    def resolve(name, active):
        if name in real_definitions or name in resolved:
            return True
        if name in failed:
            return False
        if name in active:
            failed[name] = "fallback cycle"
            return False
        if len(active) >= MAX_CHAIN:
            failed[name] = "fallback chain exceeds its limit"
            return False
        alternatives = edges.get(name)
        if not alternatives:
            failed[name] = "no actual private definition for " + name
            return False
        active.add(name)
        for edge in alternatives:
            if not edge.local_definition and not resolve(edge.target, active):
                failed[name] = edge.member + ": " + name + " -> " + edge.target + ": " + failed[edge.target]
                active.remove(name)
                return False
        active.remove(name)
        resolved.add(name)
        return True

    for name in sorted(edges):
        if resolve(name, set()):
            resolved.add(name)
    unresolved_private = (set(private_names) & edges.keys()) - resolved
    if unresolved_private:
        raise ValueError("Unresolved private COFF fallback:\n" + "\n".join(
            name + ": " + failed[name] for name in sorted(unresolved_private)[:20]))
    relevant = sorted(set(private_names) & resolved)
    for name in relevant[:16]:
        if name in real_definitions:
            print("Actual private COFF definition overrides weak alias: " + name +
                  " [" + definition_sources[name] + "]")
            continue
        edge = edges[name][0]
        print("Proven private COFF fallback: " + name + " -> " + edge.target +
              " [" + edge.member + ", search=" + str(edge.search) + "]")
    if relevant:
        print("Proven private COFF fallbacks: " + str(len(relevant)))
    return resolved


def read_resolved_aliases(readobj, archive, definitions, private_names=(), *, expected_fallbacks=None):
    """Run a bounded inspection; diagnostics or an unreadable member fail closed."""
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(readobj), "--symbols", "--no-demangle", str(archive)],
                                   stdout=output, stderr=errors)
        deadline = time.monotonic() + READ_TIMEOUT
        try:
            while True:
                if (os.fstat(output.fileno()).st_size > MAX_OUTPUT
                        or os.fstat(errors.fileno()).st_size > MAX_STDERR):
                    raise ValueError("llvm-readobj output exceeds its limit")
                if time.monotonic() >= deadline:
                    raise ValueError("llvm-readobj inspection timed out")
                try:
                    status = process.wait(timeout=0.2)
                    break
                except subprocess.TimeoutExpired:
                    pass
            if (os.fstat(output.fileno()).st_size > MAX_OUTPUT
                    or os.fstat(errors.fileno()).st_size > MAX_STDERR):
                raise ValueError("llvm-readobj output exceeds its limit")
            errors.seek(0)
            diagnostic = errors.read(MAX_STDERR + 1).decode("utf-8", "strict").strip()
            if status or diagnostic:
                raise ValueError("llvm-readobj inspection failed: " + diagnostic)
            output.seek(0)

            def lines():
                while True:
                    line = output.readline(MAX_LINE + 1)
                    if not line:
                        return
                    if len(line) > MAX_LINE:
                        raise ValueError("llvm-readobj symbol line exceeds its limit")
                    yield line.decode("utf-8", "strict")

            return parse_resolved_aliases(lines(), definitions, private_names,
                                          expected_fallbacks=expected_fallbacks)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
