#!/usr/bin/env python3
"""Classify the VBS enclave runtime probe's ordered execution evidence."""

import argparse
import json
import pathlib
import re


EXPECTED_STAGES = (
    "IsEnclaveTypeSupported", "CreateEnclave", "LoadEnclaveImage",
    "InitializeEnclave", "GetProcAddress",
    "CallEnclave", "VerifyResult", "CallEnclave", "VerifyResult",
    "CallEnclave", "VerifyResult", "CallEnclave", "VerifyResult",
    "TerminateEnclave", "DeleteEnclave", "Complete",
)
STAGE_LINE = re.compile(r"VBS_STAGE=(\w+) STATUS=(PASS|FAIL) ERROR=(0|[1-9][0-9]*)")
ENVIRONMENT_FAILURES = {
    ("IsEnclaveTypeSupported", 50),
    ("CreateEnclave", 50),
    ("CreateEnclave", 120),
    ("LoadEnclaveImage", 50),
    ("LoadEnclaveImage", 577),
}


def result(status, stage, error, message):
    return {"status": status, "stage": stage, "error": error, "message": message}


def protocol_failure(message):
    return result("FAIL", "HostProtocol", 13, message)


def classify(log, exit_code, reference=False, require_runtime=False):
    """Return a verdict; optional references may skip only known environment failures."""
    if exit_code == 124:
        return result("FAIL", "HostTimeout", 1460, "Runtime host timed out.")
    if exit_code not in (0, 1):
        return result("FAIL", "HostProcess", exit_code,
                      "Runtime host exited abnormally with code %d." % exit_code)

    position = 0
    failure = None
    for line_number, raw_line in enumerate(log.splitlines(), 1):
        line = raw_line.strip()
        if line.startswith("COMMAND:") or "VBS_STAGE" not in line:
            continue
        match = STAGE_LINE.fullmatch(line)
        if not match:
            return protocol_failure("Malformed stage evidence at line %d." % line_number)
        stage, status, error_text = match.groups()
        if failure is not None:
            return protocol_failure("Stage evidence follows a terminal failure.")
        if position == len(EXPECTED_STAGES) or stage != EXPECTED_STAGES[position]:
            return protocol_failure("Unexpected stage %s at line %d." % (stage, line_number))
        if len(error_text) > 10 or int(error_text) > 0xffffffff:
            return protocol_failure("Error value is outside the Windows DWORD range.")
        error = int(error_text)
        if status == "FAIL":
            if exit_code != 1 or error == 0:
                return protocol_failure("Failure evidence conflicts with the host result.")
            failure = (stage, error)
            continue
        if error != 0:
            return protocol_failure("Success evidence has a nonzero error at stage %s." % stage)
        position += 1
    if failure is not None:
        stage, error = failure
        if reference and not require_runtime and failure in ENVIRONMENT_FAILURES:
            return result("SKIP", stage, error,
                          "Microsoft reference encountered a recognized VBS environment limitation.")
        return result("FAIL", stage, error,
                      "Runtime probe failed at %s with error %d." % failure)
    if exit_code != 0 or position != len(EXPECTED_STAGES):
        return protocol_failure("Host did not provide a complete successful execution trace.")
    return result("PASS", "Complete", 0,
                  "Loaded, initialized, verified four enclave calls, and released the enclave.")


def self_test():
    count = 0

    def check(name, log, exit_code, expected, **options):
        nonlocal count
        actual = classify(log, exit_code, **options)
        observed = (actual["status"], actual["stage"], actual["error"])
        if observed != expected or not actual["message"]:
            raise AssertionError("%s: expected %r, got %r" % (name, expected, actual))
        count += 1

    old_probe = "\n".join(
        "VBS_STAGE=%s STATUS=PASS ERROR=0" % stage
        for stage in ("IsEnclaveTypeSupported", "CreateEnclave",
                      "LoadEnclaveImage", "InitializeEnclave", "Complete"))
    check("old load/init-only probe", old_probe, 0, ("FAIL", "HostProtocol", 13))
    check("unsupported optional reference",
          "VBS_STAGE=IsEnclaveTypeSupported STATUS=FAIL ERROR=50", 1,
          ("SKIP", "IsEnclaveTypeSupported", 50), reference=True)

    # Keep the fixture independent of EXPECTED_STAGES so a parser edit cannot
    # silently weaken the minimum execution evidence under test.
    stages = ("IsEnclaveTypeSupported CreateEnclave LoadEnclaveImage "
              "InitializeEnclave GetProcAddress CallEnclave VerifyResult "
              "CallEnclave VerifyResult CallEnclave VerifyResult "
              "CallEnclave VerifyResult TerminateEnclave DeleteEnclave Complete").split()
    lines = ["VBS_STAGE=%s STATUS=PASS ERROR=0" % stage for stage in stages]
    complete = "\n".join(lines)
    invalid = ("FAIL", "HostProtocol", 13)
    for reference in (False, True):
        for required in (False, True):
            check("complete execution", complete, 0, ("PASS", "Complete", 0),
                  reference=reference, require_runtime=required)
    check("ordinary logging and CRLF",
          "COMMAND: host.exe VBS_STAGE=path\r\nordinary output\r\n" +
          "\r\n".join("  " + line + "  " for line in lines) + "\r\nfinished\r\n",
          0, ("PASS", "Complete", 0))
    check("no evidence", "COMMAND: host.exe image.dll\n", 0, invalid)
    check("nonzero after complete trace", complete, 1, invalid)
    check("extra stage after complete", complete + "\n" + lines[-1], 0, invalid)

    for index, stage in enumerate(stages):
        check("deleted stage %d" % index,
              "\n".join(lines[:index] + lines[index + 1:]), 0, invalid)
        check("duplicated stage %d" % index,
              "\n".join(lines[:index] + [lines[index]] + lines[index:]), 0, invalid)
        check("truncated before stage %d" % index,
              "\n".join(lines[:index]), 0, invalid)
        check("unknown stage %d" % index,
              "\n".join(lines[:index] + ["VBS_STAGE=Unknown STATUS=PASS ERROR=0"] +
                        lines[index + 1:]), 0, invalid)
        if index + 1 < len(stages):
            reordered = lines[:index] + [lines[index + 1], lines[index]] + lines[index + 2:]
            check("reordered stages %d" % index, "\n".join(reordered), 0, invalid)

        failure = lines[:index] + ["VBS_STAGE=%s STATUS=FAIL ERROR=87" % stage]
        failed_log = "\n".join(failure)
        for reference in (False, True):
            for required in (False, True):
                check("failure at stage %d" % index, failed_log, 1,
                      ("FAIL", stage, 87), reference=reference, require_runtime=required)
        check("failure with zero process exit %d" % index, failed_log, 0, invalid)
        check("stage follows failure %d" % index,
              failed_log + "\n" + lines[index], 1, invalid)
        check("failure with success error %d" % index,
              "\n".join(lines[:index] + ["VBS_STAGE=%s STATUS=FAIL ERROR=0" % stage]),
              1, invalid)
        check("success with failure error %d" % index,
              "\n".join(lines[:index] +
                        ["VBS_STAGE=%s STATUS=PASS ERROR=87" % stage] + lines[index + 1:]),
              0, invalid)

    environment_errors = (
        ("IsEnclaveTypeSupported", 50), ("CreateEnclave", 50),
        ("CreateEnclave", 120), ("LoadEnclaveImage", 50), ("LoadEnclaveImage", 577),
    )
    for stage, error in environment_errors:
        prefix = lines[:stages.index(stage)]
        failed_log = "\n".join(prefix + ["VBS_STAGE=%s STATUS=FAIL ERROR=%d" % (stage, error)])
        for reference in (False, True):
            for required in (False, True):
                status = "SKIP" if reference and not required else "FAIL"
                check("environment failure %s/%d" % (stage, error), failed_log, 1,
                      (status, stage, error), reference=reference, require_runtime=required)

    for index, stage in enumerate(stages[3:], 3):
        failed_log = "\n".join(lines[:index] + ["VBS_STAGE=%s STATUS=FAIL ERROR=50" % stage])
        check("functional error is not skippable %s" % stage, failed_log, 1,
              ("FAIL", stage, 50), reference=True)
    for malformed in (
        "VBS_STAGE", "VBS_STAGE=", "VBS_STAGE=IsEnclaveTypeSupported",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=SKIP ERROR=50",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=pass ERROR=0",
        "VBS_STAGE=IsEnclaveTypeSupported ERROR=0 STATUS=PASS",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=-1",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=+0",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=00",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=0 extra",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=0x0",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=4294967296",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=" + "9" * 5000,
        "prefix VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=0",
        "VBS_STAGE=IsEnclaveTypeSupported STATUS=PASS ERROR=0 VBS_STAGE=Complete",
    ):
        check("malformed evidence", "\n".join([malformed] + lines[1:]), 0, invalid)
    check("malformed evidence after complete", complete + "\nVBS_STAGE=bad", 0, invalid)
    check("late failure after complete", complete + "\nVBS_STAGE=Complete STATUS=FAIL ERROR=87",
          1, invalid)
    check("out of order failure", "VBS_STAGE=CallEnclave STATUS=FAIL ERROR=50", 1,
          invalid, reference=True)
    check("DWORD failure value",
          "VBS_STAGE=IsEnclaveTypeSupported STATUS=FAIL ERROR=4294967295", 1,
          ("FAIL", "IsEnclaveTypeSupported", 4294967295), reference=True)

    for evidence in ("", complete, "\n".join(lines[:4]),
                     "VBS_STAGE=IsEnclaveTypeSupported STATUS=FAIL ERROR=50"):
        for reference in (False, True):
            check("host timeout", evidence, 124, ("FAIL", "HostTimeout", 1460),
                  reference=reference)
            for exit_code in (-1073741819, 2, 139, 255):
                check("abnormal host exit", evidence, exit_code,
                      ("FAIL", "HostProcess", exit_code), reference=reference)
    print("PASS: VBS enclave runtime verifier self-test (%d cases)" % count)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("self-test")
    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("--log", required=True, type=pathlib.Path)
    inspect.add_argument("--exit-code", required=True, type=int)
    inspect.add_argument("--reference", action="store_true")
    inspect.add_argument("--require-runtime", action="store_true")
    args = parser.parse_args()
    if args.command == "self-test":
        self_test()
    else:
        try:
            log = args.log.read_text(encoding="utf-8-sig")
        except (OSError, UnicodeError) as error:
            verdict = protocol_failure("Cannot read runtime evidence: %s" % error)
        else:
            verdict = classify(log, args.exit_code, args.reference,
                               args.require_runtime)
        print(json.dumps(verdict))


if __name__ == "__main__":
    main()
