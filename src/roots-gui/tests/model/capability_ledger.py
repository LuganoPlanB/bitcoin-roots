# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Cross-check the shipped capability ledger against source and configured CTest."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_source(root, relative):
    path = (root / relative).resolve(strict=True)
    require(path.is_relative_to(root), f"outside the GUI source tree: {relative}")
    return path.read_text(encoding="utf-8")


def verify_binding(source, binding):
    text = read_source(source, binding["file"])
    symbol = re.escape(binding["symbol"])
    if binding["kind"] == "contract":
        pattern = rf"\b(?:class|struct)\s+{symbol}\s*(?:\{{|:)"
    elif binding["kind"] == "definition":
        # Bound to a definition, not merely a comment, call or forward declaration.
        pattern = rf"^\s*(?:inline\s+)?[\w:<>,*&\s]+\b{symbol}\s*\((?:[^;{{}}]|\{{\}})*\)(?:\s+const)?\s*\{{"
    else:
        raise ValueError(f"unknown binding kind: {binding}")
    require(re.search(pattern, text, re.MULTILINE), f"no {binding['kind']}: {binding}")


def verify_case(source, entry, command):
    text = read_source(source, entry["file"])
    case_id = re.escape(entry["id"])
    if entry["kind"] == "model":
        main = text.partition("int main(")[2]
        pattern = rf"\b{case_id}\([^;{{}}]*\);"
        require(
            re.search(pattern, main)
            and re.search(rf"\bvoid {case_id}\([^;{{}}]*\)\s*\{{", text),
            f"model case is not defined and run from main: {entry}",
        )
    elif entry["kind"] == "engine":
        require(
            re.search(
                rf'\b(?:Test\(engine,\s*|IM_REGISTER_TEST\(engine,\s*"[^"]+",\s*)"{case_id}"\)',
                text,
            ),
            f"Engine case not registered: {entry}",
        )
    elif entry["kind"] == "native":
        require(
            re.search(rf'\bCapture\([^;]+?"{case_id}"\s*[,)]', text),
            f"native journey not captured: {entry}",
        )
    else:
        raise ValueError(f"unknown case kind: {entry}")
    if command is not None:
        require(
            any(Path(arg).name == entry["commandArgument"] for arg in command),
            f"CTest command does not use expected case target: {entry}",
        )


def declared_command(declared, name):
    # CTest omits the command from its JSON when a direct executable has not
    # been built. A model-only build still audits the configured UI tests;
    # bind those cases to their exact CMake declaration without building them.
    match = re.search(
        r"add_test\s*\(\s*NAME\s+" + re.escape(name) + r"\s+COMMAND\s+([^)]*)\)",
        declared,
    )
    require(match, f"missing declared command: {name}")
    return [
        re.sub(r"\$<TARGET_FILE:([^>]+)>", r"\1", arg)
        for arg in shlex.split(match[1])
    ]


def feature_conditions(build):
    def option(key):
        match = re.search(rf"^{key}:[A-Z]+=([^\n]+)$", cache, re.MULTILINE)
        return bool(
            match
            and match[1] not in ("OFF", "FALSE", "0")
            and not match[1].endswith("-NOTFOUND")
        )

    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    version = re.search(
        r"^CMAKE_CACHE_(?:MAJOR_VERSION):INTERNAL=(\d+)$", cache, re.MULTILINE
    )
    require(version, "missing generated CMake version")
    system = sorted((build / "CMakeFiles").glob(f"{version[1]}.*/*System.cmake"))
    require(system, "missing CMake target-system configuration")
    systems = set()
    for file in system:
        result = re.search(
            r'^set\(CMAKE_SYSTEM_NAME "([^"]+)"\)$', file.read_text(), re.MULTILINE
        )
        if result:
            systems.add(result[1])
    require(
        len(systems) == 1, f"ambiguous CMake target-system configuration: {systems}"
    )
    linux = systems == {"Linux"}
    openssl = option("ROOTS_GUI_TEST_OPENSSL")
    return dict(
        tests=True,
        linux=linux,
        openssl=openssl,
        **{
            "linux-openssl": linux and openssl,
            "owned-roots": option("ROOTS_GUI_TEST_ROOTS_DAEMON"),
            "pinned-gnupg": option("ROOTS_GUI_DOWNLOAD_FIXTURES"),
        },
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    source, build = args.source.resolve(strict=True), args.build.resolve(strict=True)
    ledger = json.loads(read_source(source, "docs/capability-ledger.json"))
    require(
        ledger["schemaVersion"] == 1 and ledger["profile"] == "rpc",
        "wrong ledger schema or shipped profile",
    )
    inventory = json.loads(
        subprocess.check_output(
            [args.ctest, "--test-dir", build, "--show-only=json-v1"],
            text=True,
            timeout=10,
        )
    )["tests"]
    registration = {test["name"]: test.get("command") for test in inventory}
    require(len(registration) == len(inventory), "duplicate configured CTest names")
    declared = "\n".join(
        read_source(source, path)
        for path in (
            "CMakeLists.txt",
            "tests/CMakeLists.txt",
            "tests/engine/CMakeLists.txt",
        )
    )
    compiled = subprocess.check_output(
        [args.probe, "--shipped-ports"], text=True, timeout=5
    )
    states = [line.split() for line in compiled.splitlines()]
    actual_ports = dict(states)
    require(
        states and len(actual_ports) == len(states),
        "duplicate or empty compiled optional registry",
    )
    conditions = feature_conditions(build)
    ids, ports, test_refs, case_refs, bindings = set(), set(), set(), set(), set()
    absent_tests = set()
    unbuilt_case_commands = set()
    for entry in ledger["entries"]:
        feature = entry["id"]
        require(
            bool(re.fullmatch(r"[a-z0-9-]+", feature)) and feature not in ids,
            f"invalid or duplicate feature: {feature}",
        )
        ids.add(feature)
        require(
            entry["bindings"]
            and entry["tests"]
            and entry["runtimePrerequisites"]
            and entry["qualification"],
            f"incomplete entry: {feature}",
        )
        require(
            entry["compiledSurface"]
            in {
                "implemented",
                "contract-only",
                "platform-adapter",
                "build-pinned-adapter",
                "boundary-only",
            }
            and entry["shippedProfile"]
            in {"available", "conditional", "opt-in", "unavailable"},
            f"invalid state: {feature}",
        )
        port = entry.get("optionalRegistry")
        if port:
            require(
                port == feature and port not in ports and port in actual_ports,
                f"unknown or duplicate compiled port: {port}",
            )
            expected = (
                ("contract-only", "unavailable")
                if actual_ports[port] == "0"
                else ("implemented", "conditional")
            )
            require(
                actual_ports[port] in ("0", "1")
                and (entry["compiledSurface"], entry["shippedProfile"]) == expected,
                f"ledger disagrees with the compiled RPC registry: {port}",
            )
            ports.add(port)
        for binding in entry["bindings"]:
            if (key := tuple(sorted(binding.items()))) not in bindings:
                verify_binding(source, binding)
                bindings.add(key)
        for evidence in entry["tests"]:
            name = evidence["ctest"]
            require(
                re.search(
                    r"add_test\s*\(\s*NAME\s+" + re.escape(name) + r"\s+COMMAND\b",
                    declared,
                ),
                f"no CMake registration: {name}",
            )
            command = registration.get(name)
            require(
                (name in registration) == conditions[evidence["when"]],
                f"unexpected configured registration: {name}",
            )
            test_refs.add(name)
            if name not in registration:
                absent_tests.add(name)
            if concrete := evidence.get("case"):
                if name in registration and command is None:
                    command = declared_command(declared, name)
                    unbuilt_case_commands.add(name)
                verify_case(source, concrete, command)
                case_refs.add((name, concrete["id"]))
    require(ports == set(actual_ports), "ledger omits a compiled optional port")
    print(
        json.dumps(
            dict(
                features=len(ids),
                compiledOptionalPorts=actual_ports,
                bindings=len(bindings),
                testReferences=len(test_refs),
                caseReferences=len(case_refs),
                configuredReferences=len(test_refs - absent_tests),
                conditionallyAbsent=sorted(absent_tests),
                configuredUnbuiltCaseCommands=sorted(unbuilt_case_commands),
            )
        )
    )


if __name__ == "__main__":
    main()
