"""Write a bounded Linux ARM64 install build snapshot (no commands or paths)."""

import os
import platform
import sys
from datetime import datetime, timezone
from pathlib import Path


def read_text(path):
    try:
        return path.read_text(encoding="ascii")
    except (OSError, UnicodeError):
        return ""


def numeric(value):
    if value == "max":
        return value
    if value.isascii() and value.isdecimal() and len(value) <= 20:
        return value
    return "unavailable"


def fields(text, separator=None):
    result = {}
    for line in text.splitlines():
        columns = line.split(separator, 1)
        if len(columns) != 2:
            continue
        value = columns[1].split()
        if value:
            result[columns[0]] = numeric(value[0])
    return result


def current_v2_cgroup(proc, root):
    for row in read_text(proc / "self/cgroup").splitlines():
        if not row.startswith("0::/"):
            continue
        path = Path(row[3:])
        if ".." not in path.parts:
            return root.joinpath(*path.parts[1:])
    return None


def ninja_rows(path):
    try:
        with path.open("rb") as log:
            return str(sum(1 for row in log if row.strip() and not row.startswith(b"#")))
    except OSError:
        return "unavailable"


def snapshot(build, proc, cgroup_root):
    values = {"utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    arch = platform.machine()
    values["arch"] = arch if arch in ("aarch64", "arm64", "x86_64") else "unavailable"
    values["logical_cpus"] = str(os.cpu_count() or 0)
    mem = fields(read_text(proc / "meminfo"), ":")
    for key in ("MemTotal", "MemAvailable", "SwapTotal", "SwapFree"):
        values[key + "_KiB"] = mem.get(key, "unavailable")
    try:
        disk = os.statvfs(build)
        values["disk_available_KiB"] = str(disk.f_bavail * disk.f_frsize // 1024)
    except OSError:
        values["disk_available_KiB"] = "unavailable"
    values["ninja_log_rows"] = ninja_rows(build / ".ninja_log")
    group = current_v2_cgroup(proc, cgroup_root)
    for key in ("memory.current", "memory.high", "memory.max", "memory.swap.current"):
        values[key] = numeric(read_text(group / key).strip()) if group else "unavailable"
    events = fields(read_text(group / "memory.events")) if group else {}
    for key in ("low", "high", "max", "oom", "oom_kill", "oom_group_kill"):
        values["memory.events." + key] = events.get(key, "unavailable")
    return "".join(f"{key}={value}\n" for key, value in values.items())


if __name__ == "__main__":
    data = snapshot(Path(sys.argv[1]), Path("/proc"), Path("/sys/fs/cgroup"))
    encoded = data.encode("ascii")
    if len(encoded) > 4096:
        sys.exit("diagnostic output exceeds 4096 bytes")
    Path(sys.argv[2]).write_bytes(encoded)
