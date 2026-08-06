"""Classic, deterministic OLLVM transformations implemented in Python.

This example intentionally uses only the public generated NeverC plugin ABI.
It is verbose enough to serve as a reference for raw IR pass authors while the
small helpers below keep every native call lifetime-checked by ``Scope``.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
import fnmatch
import hashlib
import random

from neverc_plugin import InterfaceRequirement, Plugin
from neverc_plugin import abi, phases
from neverc_plugin.domains.ir import PASS
from neverc_plugin.ffi import (
    CheckedPointer,
    NevercError,
    Scope,
    StringView,
    bind_callbacks,
)


def _handle_key(value: abi.NevercHandle) -> tuple[int, int]:
    return int(value.Owner), int(value.Value)


def _pointer_address(value) -> int:
    return int(ctypes.cast(value, ctypes.c_void_p).value or 0)


def _string_view(text: str):
    data = text.encode("utf-8")
    buffer = ctypes.create_string_buffer(data, len(data) + 1)
    view = abi.NevercStringView()
    view.Data = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_char))
    view.Length = len(data)
    return buffer, view


def _header(record, major: int, minor: int) -> None:
    record.Header.StructSize = ctypes.sizeof(record)
    record.Header.Major = major
    record.Header.Minor = minor
    record.Header.Flags = 0


@dataclass(frozen=True, slots=True)
class _Config:
    sub: bool = False
    bcf: bool = False
    fla: bool = False
    probability: int = 100
    iterations: int = 1
    seed: int = 0
    include: tuple[str, ...] = ("*",)
    exclude: tuple[str, ...] = ()

    @classmethod
    def from_context(cls, context) -> "_Config":
        def values(spelling: str) -> tuple[str, ...]:
            return tuple(context.option_values(spelling))

        def number(spelling: str, default: int, maximum: int) -> int:
            items = values(spelling)
            if not items:
                return default
            try:
                result = int(items[-1], 10)
            except ValueError as error:
                raise ValueError(f"{spelling} requires an unsigned integer") from error
            if result < 0 or result > maximum:
                raise ValueError(f"{spelling} must be between 0 and {maximum}")
            return result

        include = values("--ollvm-include") or ("*",)
        return cls(
            sub=bool(values("--ollvm-sub")),
            bcf=bool(values("--ollvm-bcf")),
            fla=bool(values("--ollvm-fla")),
            probability=number("--ollvm-probability", 100, 100),
            iterations=number("--ollvm-iterations", 1, 8),
            seed=number("--ollvm-seed", 0, 0xFFFFFFFFFFFFFFFF),
            include=include,
            exclude=values("--ollvm-exclude"),
        )

    def enables(self, mode: str) -> bool:
        return bool(getattr(self, mode))

    def accepts(self, function_name: str) -> bool:
        if function_name.startswith(("__neverc_", "llvm.")):
            return False
        return any(
            fnmatch.fnmatchcase(function_name, item) for item in self.include
        ) and not any(
            fnmatch.fnmatchcase(function_name, item) for item in self.exclude
        )

    def random(self, mode: str, function_name: str, iteration: int = 0) -> random.Random:
        material = f"{self.seed}:{mode}:{function_name}:{iteration}".encode("utf-8")
        derived = int.from_bytes(hashlib.sha256(material).digest()[:8], "little")
        return random.Random(derived)


class _Table:
    def __init__(self, scope: Scope, record_type, address: int):
        if not address:
            raise RuntimeError(f"NeverC returned a null {record_type.__name__}")
        self.view = scope.table(record_type, address)
        self.context = ctypes.c_void_p(int(self.view.copy().Context or 0))

    def call(self, name: str, *arguments, checked: bool = True):
        return self.view.call(name, self.context, *arguments, checked=checked)


class _Mutation:
    def __init__(self, ir: "_IR", function: abi.NevercIRValueHandle):
        self.ir = ir
        self.function = function
        self.handle = abi.NevercIRMutationHandle()
        self.builder = abi.NevercIRBuilderHandle()
        self.resolved = False

    def __enter__(self) -> "_Mutation":
        self.ir.builder.call(
            "BeginMutation",
            self.ir.task,
            abi.NEVERC_IR_MUTATION_SCOPE_FUNCTION,
            self.function,
            ctypes.byref(self.handle),
        )
        try:
            self.ir.builder.call(
                "CreateBuilder", self.ir.task, self.handle, ctypes.byref(self.builder)
            )
        except Exception:
            self.ir.builder.call("AbortMutation", self.ir.task, self.handle, checked=False)
            self.ir.builder.call("DestroyMutation", self.ir.task, self.handle, checked=False)
            raise
        return self

    def commit(self) -> None:
        if self.resolved:
            raise RuntimeError("IR mutation was already resolved")
        status = self.ir.builder.call(
            "CommitMutation", self.ir.task, self.handle, checked=False
        )
        self.resolved = True
        if int(status.Code) != int(abi.NEVERC_STATUS_OK):
            raise NevercError(status, "NevercIRBuilderAPI.CommitMutation")

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        try:
            if not self.resolved:
                self.ir.builder.call(
                    "AbortMutation", self.ir.task, self.handle, checked=False
                )
        finally:
            self.ir.builder.call(
                "DestroyBuilder", self.ir.task, self.builder, checked=False
            )
            self.ir.builder.call(
                "DestroyMutation", self.ir.task, self.handle, checked=False
            )
        return False


class _IR:
    _CHUNK = 128

    def __init__(self, scope: Scope, invocation_address: int):
        self.scope = scope
        self.invocation = CheckedPointer(
            scope, abi.NevercIRPassInvocation, invocation_address
        ).copy()
        if not self.invocation.Core or not self.invocation.Builder:
            raise RuntimeError("NeverC IR pass invocation has no mutable IR tables")
        self.task = self.invocation.Task
        self.function = self.invocation.Function
        self.core = _Table(
            scope, abi.NevercIRCoreAPI, _pointer_address(self.invocation.Core)
        )
        self.builder = _Table(
            scope, abi.NevercIRBuilderAPI, _pointer_address(self.invocation.Builder)
        )

    def collect(self, container: abi.NevercHandle, collection: int):
        cursor = abi.NevercIRValueCursor()
        self.core.call(
            "BeginValueCursor",
            self.task,
            container,
            collection,
            ctypes.byref(cursor),
        )
        result = []
        while True:
            storage = (abi.NevercIRValueHandle * self._CHUNK)()
            count = ctypes.c_uint64()
            self.core.call(
                "CollectValueCursor",
                self.task,
                ctypes.byref(cursor),
                storage,
                self._CHUNK,
                ctypes.byref(count),
            )
            result.extend(storage[index] for index in range(int(count.value)))
            if count.value < self._CHUNK:
                return result

    def blocks(self):
        return self.collect(self.function, abi.NEVERC_IR_COLLECTION_FUNCTION_BLOCKS)

    def instructions(self, block):
        return self.collect(block, abi.NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS)

    def value_name(self, value) -> str:
        out = abi.NevercStringView()
        self.core.call("GetValueName", self.task, value, ctypes.byref(out))
        return StringView(self.scope, out).text

    def opcode(self, instruction) -> int:
        out = ctypes.c_uint32()
        self.core.call(
            "GetInstructionOpcode", self.task, instruction, ctypes.byref(out)
        )
        return int(out.value)

    def value_kind(self, value) -> int:
        out = ctypes.c_uint32()
        self.core.call("GetValueKind", self.task, value, ctypes.byref(out))
        return int(out.value)

    def value_type(self, value):
        out = abi.NevercIRTypeHandle()
        self.core.call("GetValueType", self.task, value, ctypes.byref(out))
        return out

    def operands(self, instruction):
        count = ctypes.c_uint64()
        self.core.call("GetOperandCount", self.task, instruction, ctypes.byref(count))
        result = []
        for index in range(int(count.value)):
            value = abi.NevercIRValueHandle()
            self.core.call(
                "GetOperand", self.task, instruction, index, ctypes.byref(value)
            )
            result.append(value)
        return result

    def successors(self, block):
        count = ctypes.c_uint64()
        self.core.call("GetSuccessorCount", self.task, block, ctypes.byref(count))
        result = []
        for index in range(int(count.value)):
            value = abi.NevercIRValueHandle()
            self.core.call(
                "GetSuccessor", self.task, block, index, ctypes.byref(value)
            )
            result.append(value)
        return result

    def terminator(self, block):
        out = abi.NevercIRValueHandle()
        self.core.call("GetTerminator", self.task, block, ctypes.byref(out))
        return out

    def has_phi(self, block) -> bool:
        instructions = self.instructions(block)
        return bool(instructions and self.opcode(instructions[0]) == abi.NEVERC_IR_OPCODE_PHI)

    def has_wrap_flags(self, instruction) -> bool:
        for property_id in (abi.NEVERC_IR_PROPERTY_NUW, abi.NEVERC_IR_PROPERTY_NSW):
            value = abi.NevercIRPropertyValue()
            _header(value, abi.NEVERC_IR_CORE_API_MAJOR, abi.NEVERC_IR_CORE_API_MINOR)
            status = self.core.call(
                "GetInstructionProperty",
                self.task,
                instruction,
                property_id,
                ctypes.byref(value),
                checked=False,
            )
            if int(status.Code) == int(abi.NEVERC_STATUS_OK) and value.UnsignedValue:
                return True
        return False

    def set_insert_block(self, mutation: _Mutation, block) -> None:
        self.builder.call("SetInsertBlock", self.task, mutation.builder, block)

    def set_insert_before(self, mutation: _Mutation, instruction) -> None:
        self.builder.call(
            "SetInsertBefore", self.task, mutation.builder, instruction
        )

    def build_binary(self, mutation: _Mutation, opcode: int, left, right, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildBinary",
            self.task,
            mutation.builder,
            opcode,
            left,
            right,
            view,
            ctypes.byref(out),
        )
        return out

    def build_compare(self, mutation: _Mutation, predicate: int, left, right, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildCompare",
            self.task,
            mutation.builder,
            predicate,
            left,
            right,
            view,
            ctypes.byref(out),
        )
        return out

    def build_select(self, mutation: _Mutation, condition, true_value, false_value, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildSelect",
            self.task,
            mutation.builder,
            condition,
            true_value,
            false_value,
            view,
            ctypes.byref(out),
        )
        return out

    def build_alloca(self, mutation: _Mutation, value_type, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildAlloca",
            self.task,
            mutation.builder,
            value_type,
            0,
            abi.NevercIRValueHandle(),
            view,
            ctypes.byref(out),
        )
        return out

    def build_load(self, mutation: _Mutation, value_type, pointer, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildLoad",
            self.task,
            mutation.builder,
            value_type,
            pointer,
            view,
            ctypes.byref(out),
        )
        return out

    def build_store(self, mutation: _Mutation, value, pointer):
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildStore",
            self.task,
            mutation.builder,
            value,
            pointer,
            ctypes.byref(out),
        )
        return out

    def build_branch(self, mutation: _Mutation, destination):
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildBranch",
            self.task,
            mutation.builder,
            destination,
            ctypes.byref(out),
        )
        return out

    def build_conditional_branch(self, mutation: _Mutation, condition, true_block, false_block):
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "BuildConditionalBranch",
            self.task,
            mutation.builder,
            condition,
            true_block,
            false_block,
            ctypes.byref(out),
        )
        return out

    def create_block(self, mutation: _Mutation, name: str):
        storage, view = _string_view(name)
        out = abi.NevercIRValueHandle()
        self.builder.call(
            "CreateBasicBlock",
            self.task,
            mutation.handle,
            self.function,
            view,
            ctypes.byref(out),
        )
        return out

    def integer_type(self, width: int):
        out = abi.NevercIRTypeHandle()
        self.core.call("GetIntegerType", self.task, width, ctypes.byref(out))
        return out

    def integer_constant(self, value_type, value: int):
        words = (ctypes.c_uint64 * 1)(value & 0xFFFFFFFFFFFFFFFF)
        out = abi.NevercIRValueHandle()
        self.core.call(
            "CreateIntegerConstant",
            self.task,
            value_type,
            words,
            1,
            ctypes.byref(out),
        )
        return out

    def null_constant(self, value_type):
        out = abi.NevercIRValueHandle()
        self.core.call("GetNullConstant", self.task, value_type, ctypes.byref(out))
        return out

    def replace_and_erase(self, original, replacement) -> None:
        self.core.call("ReplaceAllUsesWith", self.task, original, replacement)
        self.core.call("EraseValue", self.task, original)

    def conditional_operand(self, terminator, successors):
        successor_keys = {_handle_key(value) for value in successors}
        candidates = [
            value
            for value in self.operands(terminator)
            if _handle_key(value) not in successor_keys
        ]
        if len(candidates) != 1:
            raise RuntimeError("conditional branch has an unexpected operand layout")
        return candidates[0]


def _selected(rng: random.Random, probability: int) -> bool:
    return probability == 100 or (probability != 0 and rng.randrange(100) < probability)


def _substitute(ir: _IR, config: _Config, function_name: str) -> bool:
    changed_any = False
    supported = {
        abi.NEVERC_IR_OPCODE_ADD,
        abi.NEVERC_IR_OPCODE_SUB,
        abi.NEVERC_IR_OPCODE_AND,
        abi.NEVERC_IR_OPCODE_OR,
        abi.NEVERC_IR_OPCODE_XOR,
    }
    for iteration in range(config.iterations):
        rng = config.random("sub", function_name, iteration)
        candidates = []
        for block in ir.blocks():
            for instruction in ir.instructions(block):
                opcode = ir.opcode(instruction)
                if opcode in supported and not ir.has_wrap_flags(instruction):
                    if _selected(rng, config.probability):
                        candidates.append((instruction, opcode))
        if not candidates:
            continue
        with _Mutation(ir, ir.function) as mutation:
            for instruction, opcode in candidates:
                operands = ir.operands(instruction)
                if len(operands) != 2:
                    continue
                left, right = operands
                ir.set_insert_before(mutation, instruction)
                if opcode in (abi.NEVERC_IR_OPCODE_ADD, abi.NEVERC_IR_OPCODE_SUB):
                    zero = ir.null_constant(ir.value_type(instruction))
                    negative = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_SUB, zero, right, "ollvm.neg"
                    )
                    replacement_opcode = (
                        abi.NEVERC_IR_OPCODE_SUB
                        if opcode == abi.NEVERC_IR_OPCODE_ADD
                        else abi.NEVERC_IR_OPCODE_ADD
                    )
                    replacement = ir.build_binary(
                        mutation, replacement_opcode, left, negative, "ollvm.sub"
                    )
                elif opcode == abi.NEVERC_IR_OPCODE_XOR:
                    union = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_OR, left, right, "ollvm.union"
                    )
                    intersection = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_AND, left, right, "ollvm.intersection"
                    )
                    replacement = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_SUB, union, intersection, "ollvm.sub"
                    )
                elif opcode == abi.NEVERC_IR_OPCODE_AND:
                    total = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_ADD, left, right, "ollvm.total"
                    )
                    union = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_OR, left, right, "ollvm.union"
                    )
                    replacement = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_SUB, total, union, "ollvm.sub"
                    )
                else:
                    total = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_ADD, left, right, "ollvm.total"
                    )
                    intersection = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_AND, left, right, "ollvm.intersection"
                    )
                    replacement = ir.build_binary(
                        mutation, abi.NEVERC_IR_OPCODE_SUB, total, intersection, "ollvm.sub"
                    )
                ir.replace_and_erase(instruction, replacement)
            mutation.commit()
        changed_any = True
    return changed_any


def _bogus_control_flow(ir: _IR, config: _Config, function_name: str) -> bool:
    changed_any = False
    for iteration in range(config.iterations):
        rng = config.random("bcf", function_name, iteration)
        candidates = []
        for block in ir.blocks():
            terminator = ir.terminator(block)
            successors = ir.successors(block)
            if len(successors) != 2 or _handle_key(successors[0]) == _handle_key(successors[1]):
                continue
            if any(ir.has_phi(target) for target in successors):
                continue
            if _selected(rng, config.probability):
                candidates.append((block, terminator, successors, rng.randrange(2)))
        if not candidates:
            continue
        patches = []
        with _Mutation(ir, ir.function) as mutation:
            for index, (block, terminator, successors, target_index) in enumerate(candidates):
                condition = ir.conditional_operand(terminator, successors)
                target = successors[target_index]
                gate = ir.create_block(mutation, f"ollvm.bcf.gate.{iteration}.{index}")
                bogus = ir.create_block(mutation, f"ollvm.bcf.bogus.{iteration}.{index}")
                ir.set_insert_block(mutation, gate)
                if target_index == 0:
                    ir.build_conditional_branch(mutation, condition, target, bogus)
                else:
                    ir.build_conditional_branch(mutation, condition, bogus, target)
                ir.set_insert_block(mutation, bogus)
                ir.build_branch(mutation, target)
                operand_indices = [
                    operand_index
                    for operand_index, operand in enumerate(ir.operands(terminator))
                    if _handle_key(operand) == _handle_key(target)
                ]
                if len(operand_indices) != 1:
                    raise RuntimeError("branch successor has an unexpected operand layout")
                patches.append((terminator, operand_indices[0], gate))
            for terminator, operand_index, gate in patches:
                ir.core.call(
                    "SetOperand", ir.task, terminator, operand_index, gate
                )
            mutation.commit()
        changed_any = True
    return changed_any


def _flatten(ir: _IR, config: _Config, function_name: str) -> bool:
    blocks = ir.blocks()
    if len(blocks) < 3 or any(ir.has_phi(block) for block in blocks):
        return False
    entry_key = _handle_key(blocks[0])
    models = []
    block_keys = {_handle_key(block) for block in blocks}
    for block in blocks:
        instructions = ir.instructions(block)
        if not instructions:
            return False
        terminator = instructions[-1]
        opcode = ir.opcode(terminator)
        successors = ir.successors(block)
        if successors:
            if opcode != abi.NEVERC_IR_OPCODE_BR or len(successors) not in (1, 2):
                return False
            if any(_handle_key(target) not in block_keys for target in successors):
                return False
            if any(_handle_key(target) == entry_key for target in successors):
                return False
        elif opcode not in (
            abi.NEVERC_IR_OPCODE_RET,
            abi.NEVERC_IR_OPCODE_UNREACHABLE,
        ):
            return False
        condition = ir.conditional_operand(terminator, successors) if len(successors) == 2 else None
        models.append((block, terminator, successors, condition))
    if not models[0][2]:
        return False

    cases = blocks[1:]
    rng = config.random("fla", function_name)
    codes = list(range(0x1001, 0x1001 + len(cases)))
    rng.shuffle(codes)
    code_for = {_handle_key(block): codes[index] for index, block in enumerate(cases)}

    with _Mutation(ir, ir.function) as mutation:
        dispatcher = ir.create_block(mutation, "ollvm.fla.dispatch")
        checks = [dispatcher]
        for index in range(1, len(cases)):
            checks.append(ir.create_block(mutation, f"ollvm.fla.check.{index}"))
        i32 = ir.integer_type(32)
        ir.set_insert_before(mutation, models[0][1])
        state = ir.build_alloca(mutation, i32, "ollvm.fla.state")

        for block, terminator, successors, condition in models:
            if not successors:
                continue
            ir.set_insert_before(mutation, terminator)
            if len(successors) == 1:
                next_state = ir.integer_constant(i32, code_for[_handle_key(successors[0])])
            else:
                true_state = ir.integer_constant(i32, code_for[_handle_key(successors[0])])
                false_state = ir.integer_constant(i32, code_for[_handle_key(successors[1])])
                next_state = ir.build_select(
                    mutation,
                    condition,
                    true_state,
                    false_state,
                    "ollvm.fla.next",
                )
            ir.build_store(mutation, next_state, state)
            ir.core.call("EraseValue", ir.task, terminator)
            ir.set_insert_block(mutation, block)
            ir.build_branch(mutation, dispatcher)

        for index, (check, case) in enumerate(zip(checks, cases)):
            ir.set_insert_block(mutation, check)
            if index + 1 == len(cases):
                ir.build_branch(mutation, case)
                continue
            loaded = ir.build_load(mutation, i32, state, "ollvm.fla.current")
            expected = ir.integer_constant(i32, code_for[_handle_key(case)])
            matches = ir.build_compare(
                mutation,
                abi.NEVERC_IR_PREDICATE_ICMP_EQ,
                loaded,
                expected,
                "ollvm.fla.matches",
            )
            ir.build_conditional_branch(mutation, matches, case, checks[index + 1])
        mutation.commit()
    return True


@Plugin(
    id="org.neverc.example.ollvm-python",
    name="NeverC classic OLLVM Python example",
    version="1.0.0",
    required_interfaces=(
        InterfaceRequirement("IR_CORE"),
        InterfaceRequirement("IR_BUILDER"),
        InterfaceRequirement("IR_PASS"),
    ),
)
class OLLVMPlugin:
    def __init__(self):
        self._task_configs: dict[tuple[int, int], _Config] = {}
        self._bindings = []

    def register(self, context) -> None:
        context.option("--ollvm-sub", help="enable instruction substitution")
        context.option("--ollvm-bcf", help="enable bogus control flow")
        context.option("--ollvm-fla", help="enable control-flow flattening")
        context.option(
            "--ollvm-probability",
            kind="separate",
            value_type="uint",
            multiplicity="last_wins",
            help="transform probability from 0 to 100 (default: 100)",
            metavar="PERCENT",
        )
        context.option(
            "--ollvm-iterations",
            kind="separate",
            value_type="uint",
            multiplicity="last_wins",
            help="SUB/BCF rounds from 0 to 8 (default: 1)",
            metavar="COUNT",
        )
        context.option(
            "--ollvm-seed",
            kind="separate",
            value_type="uint",
            multiplicity="last_wins",
            help="deterministic uint64 seed (default: 0)",
            metavar="SEED",
        )
        context.option(
            "--ollvm-include",
            kind="separate",
            value_type="string",
            multiplicity="append",
            help="include function glob (repeatable; default: *)",
            metavar="GLOB",
        )
        context.option(
            "--ollvm-exclude",
            kind="separate",
            value_type="string",
            multiplicity="append",
            help="exclude function glob (repeatable)",
            metavar="GLOB",
        )

        scope = context.ffi
        pass_api = PASS.query(scope)
        pass_context = ctypes.c_void_p(int(pass_api.copy().Context or 0))
        for mode, phase in (
            ("sub", phases.IR_PASS_OPTIMIZER_LAST),
            ("bcf", phases.IR_PASS_POST_OPT),
            ("fla", phases.IR_PASS_PRE_CODEGEN),
        ):
            descriptor = abi.NevercIRPassDescriptor()
            _header(
                descriptor,
                abi.NEVERC_IR_PASS_API_MAJOR,
                abi.NEVERC_IR_PASS_API_MINOR,
            )
            pass_id_buffer, descriptor.PassID = _string_view(f"ollvm.{mode}")
            descriptor.Phase.High = phase.high
            descriptor.Phase.Low = phase.low
            descriptor.Level = abi.NEVERC_IR_PASS_LEVEL_FUNCTION
            descriptor.Deterministic = abi.NEVERC_TRUE
            descriptor.Cacheable = abi.NEVERC_FALSE

            def callback(
                callback_scope, invocation, preserved, selected=mode
            ):
                return self._run_pass(
                    selected, callback_scope, invocation, preserved
                )

            bound = bind_callbacks(scope, descriptor, {"Run": callback})
            result = pass_api.function("RegisterPass")(
                pass_context,
                ctypes.c_void_p(scope.registrar_context_address),
                bound.pointer,
            )
            if int(result.Code) != int(abi.NEVERC_STATUS_OK):
                bound.release()
                raise NevercError(result, "NevercIRPassAPI.RegisterPass")
            bound.transfer()
            self._bindings.append(bound)

    def on_task_begin(self, context) -> None:
        self._task_configs[tuple(context.handle)] = _Config.from_context(context)

    def on_task_end(self, context) -> None:
        self._task_configs.pop(tuple(context.handle), None)

    def _run_pass(
        self,
        mode: str,
        scope: Scope,
        invocation_address: int,
        preserved_address: int,
    ):
        ctypes.memset(preserved_address, 0, ctypes.sizeof(abi.NevercIRPreservedAnalyses))
        preserved = ctypes.cast(
            preserved_address, ctypes.POINTER(abi.NevercIRPreservedAnalyses)
        ).contents
        _header(preserved, abi.NEVERC_IR_PASS_API_MAJOR, abi.NEVERC_IR_PASS_API_MINOR)

        ir = _IR(scope, invocation_address)
        config = self._task_configs.get(_handle_key(ir.task), _Config())
        if not config.enables(mode):
            preserved.Flags = abi.NEVERC_IR_PRESERVE_ALL
            return None
        function_name = ir.value_name(ir.function)
        if not config.accepts(function_name):
            preserved.Flags = abi.NEVERC_IR_PRESERVE_ALL
            return None

        if mode == "sub":
            changed = _substitute(ir, config, function_name)
        elif mode == "bcf":
            changed = _bogus_control_flow(ir, config, function_name)
        else:
            changed = _flatten(ir, config, function_name)
        preserved.Flags = (
            abi.NEVERC_IR_PRESERVE_NONE if changed else abi.NEVERC_IR_PRESERVE_ALL
        )
        return None
