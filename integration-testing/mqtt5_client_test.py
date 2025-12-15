#!/usr/bin/env python3
"""
Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
SPDX-License-Identifier: Apache-2.0.
"""

import os
import subprocess
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional
from urllib.parse import quote

TIMEOUT_SECONDS = 120

COMMAND_PREFIX = sys.argv[1:]
if not COMMAND_PREFIX:
    print("You must pass the mqtt5_socks5_app command prefix, e.g. python mqtt5_client_test.py ./bin/mqtt5_socks5_app/mqtt5_socks5_app")  # noqa: E501
    sys.exit(-1)

PROGRAM = COMMAND_PREFIX[0]

if "bin" in PROGRAM and not Path(PROGRAM).exists():
    print(f"{PROGRAM} not found, skipping MQTT5 SOCKS5 integration tests.")
    sys.exit(0)

# Ensure unittest does not attempt to parse our custom arguments.
sys.argv = sys.argv[:1]

SCRIPT_DIR = Path(__file__).resolve().parent

LOCAL_BROKER_HOST = os.environ.get("MQTT5_LOCAL_BROKER_HOST", "localhost")
LOCAL_CA_FILE = os.environ.get("MQTT5_LOCAL_CA_FILE", "/etc/mosquitto/ca_certificates/ca_certificate.pem")

PROXY_HOST = os.environ.get("MQTT5_PROXY_HOST", "localhost")
PROXY_PORT = os.environ.get("MQTT5_PROXY_PORT", "1080")
PROXY_USER = os.environ.get("MQTT5_PROXY_USER", "testuser")
PROXY_PASSWORD = os.environ.get("MQTT5_PROXY_PASSWORD", "testpass")
USE_PROXY_AUTH = os.environ.get("MQTT5_PROXY_USE_AUTH", "1").lower() not in ("0", "false", "no")


# socks5h (host resolution by proxy)
PROXY_URI_NOAUTH = f"socks5h://{PROXY_HOST}:{PROXY_PORT}"
PROXY_URI_AUTH = f"socks5h://{quote(PROXY_USER)}:{quote(PROXY_PASSWORD)}@{PROXY_HOST}:{PROXY_PORT}"
PROXY_URI = PROXY_URI_AUTH if USE_PROXY_AUTH else PROXY_URI_NOAUTH

# socks5 (client host resolution)
PROXY_SOCKS5_NOAUTH = f"socks5://{PROXY_HOST}:{PROXY_PORT}"
PROXY_SOCKS5_AUTH = f"socks5://{quote(PROXY_USER)}:{quote(PROXY_PASSWORD)}@{PROXY_HOST}:{PROXY_PORT}"
PROXY_SOCKS5_URI = PROXY_SOCKS5_AUTH if USE_PROXY_AUTH else PROXY_SOCKS5_NOAUTH


def run_command(args: List[str], label: Optional[str] = None) -> None:
    """Run the provided command and raise with helpful diagnostics on failure."""
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    timed_out = False
    try:
        output = process.communicate(timeout=TIMEOUT_SECONDS)[0]
    except subprocess.TimeoutExpired:
        timed_out = True
        process.kill()
        output = process.communicate()[0]
    if process.returncode != 0 or timed_out:
        decoded_output = output.decode(errors="replace")
        args_str = subprocess.list2cmdline(args)
        heading = f"[{label}] " if label else ""
        print(f"{heading}{args_str}")
        print(decoded_output)
        if timed_out:
            raise RuntimeError(
                f"{heading}Timeout after {TIMEOUT_SECONDS}s running: {args_str}\n{decoded_output}"
            )
        raise RuntimeError(
            f"{heading}Return code {process.returncode} from: {args_str}\n{decoded_output}"
        )


@dataclass(frozen=True)
class TestCaseConfig:
    name: str
    broker_host: str
    broker_port: int
    websocket: bool = False
    tls_ca_file: Optional[str] = None
    proxy_uri: Optional[str] = None


def _build_cases() -> List[TestCaseConfig]:
    cases: List[TestCaseConfig] = []

    proxy = PROXY_URI

    # Direct connections (no proxy)
    cases.extend(
        [
            TestCaseConfig("direct_mqtt_local", LOCAL_BROKER_HOST, 1883),
            TestCaseConfig("direct_mqtts_local", LOCAL_BROKER_HOST, 8883, tls_ca_file=LOCAL_CA_FILE),
            TestCaseConfig("direct_mqtt_ws_local", LOCAL_BROKER_HOST, 8080, websocket=True),
            TestCaseConfig("direct_mqtts_ws_local", LOCAL_BROKER_HOST, 8081, websocket=True, tls_ca_file=LOCAL_CA_FILE),
        ]
    )

    # Proxy connections
    cases.extend(
        [
            TestCaseConfig("proxy_mqtt_local", LOCAL_BROKER_HOST, 1883, proxy_uri=proxy),
            TestCaseConfig("proxy_mqtts_local", LOCAL_BROKER_HOST, 8883, tls_ca_file=LOCAL_CA_FILE, proxy_uri=proxy),
            TestCaseConfig("proxy_mqtt_ws_local", LOCAL_BROKER_HOST, 8080, websocket=True, proxy_uri=proxy),
            TestCaseConfig(
                "proxy_mqtts_ws_local", LOCAL_BROKER_HOST, 8081, websocket=True, tls_ca_file=LOCAL_CA_FILE, proxy_uri=proxy
            ),
        ]
    )

    # Additional test cases for client host resolution: socks5 (not socks5h)
    cases.extend([
        TestCaseConfig("proxy_mqtt_local_socks5", LOCAL_BROKER_HOST, 1883, proxy_uri=PROXY_SOCKS5_URI),
        TestCaseConfig("proxy_mqtts_local_socks5", LOCAL_BROKER_HOST, 8883, tls_ca_file=LOCAL_CA_FILE, proxy_uri=PROXY_SOCKS5_URI),
        TestCaseConfig("proxy_mqtt_ws_local_socks5", LOCAL_BROKER_HOST, 8080, websocket=True, proxy_uri=PROXY_SOCKS5_URI),
        TestCaseConfig(
            "proxy_mqtts_ws_local_socks5", LOCAL_BROKER_HOST, 8081, websocket=True, tls_ca_file=LOCAL_CA_FILE, proxy_uri=PROXY_SOCKS5_URI
        ),
    ])
    return cases


class Mqtt5Socks5IntegrationTests(unittest.TestCase):
    @staticmethod
    def _build_command(case: TestCaseConfig) -> List[str]:
        args: List[str] = [
            *COMMAND_PREFIX,
            "--broker-host",
            case.broker_host,
            "--broker-port",
            str(case.broker_port),
        ]
        if case.websocket:
            args.append("--websocket")
        if case.tls_ca_file:
            args.extend(["--ca-file", case.tls_ca_file])
        if case.proxy_uri:
            args.extend(["--proxy", case.proxy_uri])
        return args

    def _run_case(self, case: TestCaseConfig) -> None:
        if case.tls_ca_file and not Path(case.tls_ca_file).exists():
            self.skipTest(f"CA file '{case.tls_ca_file}' not found")
        if case.proxy_uri and not PROXY_URI:
            self.skipTest("Proxy URI not configured")
        try:
            run_command(self._build_command(case), label=case.name)
        except RuntimeError as exc:
            self.fail(str(exc))


# Dynamically add one unittest.TestCase method per configuration for easy filtering.
for _case in _build_cases():
    def _make_test(case: TestCaseConfig):
        def _test(self: Mqtt5Socks5IntegrationTests) -> None:
            self._run_case(case)

        _test.__name__ = f"test_{case.name}"
        _test.__doc__ = (
            f"MQTT5 socks5 scenario '{case.name}': "
            f"{case.broker_host}:{case.broker_port}, websocket={case.websocket}, proxy={'yes' if case.proxy_uri else 'no'}, "
            f"tls={'yes' if case.tls_ca_file else 'no'}"
        )
        setattr(Mqtt5Socks5IntegrationTests, _test.__name__, _test)

    _make_test(_case)


if __name__ == "__main__":
    unittest.main(verbosity=2)
