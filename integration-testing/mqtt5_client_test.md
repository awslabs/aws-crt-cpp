# MQTT5 Client Integration Test Documentation

This document is intended to guide you through testing the changes introduced in this pull request. It provides setup and execution instructions for the integration tests. You may remove this document after the PR has been reviewed and merged.

## What the Test Does

The MQTT5 SOCKS5 integration tests verify end-to-end connectivity against a local Mosquitto broker using the sample client in `bin/mqtt5_socks5_app/mqtt5_socks5_app`. The integration test is run using the Python harness (`mqtt5_client_test.py`).

The Python harness exercises:

- Direct and proxied connections
- MQTT and MQTT over WebSocket
- Encrypted (TLS) and unencrypted connections
- Authentication via SOCKS5 proxy

Each test case runs the client with different combinations of protocol (MQTT/WS), encryption (TLS/no TLS), and proxy settings, ensuring all major local connection scenarios are validated.

## Test Setup

- The test script runs a series of client invocations against a Mosquitto broker and a SOCKS5 proxy.
- Certificate files for TLS are included in the integration test directory and mounted into the Mosquitto container.
- The script expects the following environment:
  - Mosquitto broker accessible at `localhost` (or your chosen host)
  - SOCKS5 proxy accessible at `localhost` (or your chosen host)
  - Proxy credentials: username `testuser`, password `testpass`

## How to Setup Mosquitto

Create required directories for Mosquitto:

```bash
mkdir -p ~/mosquitto/config
mkdir -p ~/mosquitto/data
mkdir -p ~/mosquitto/log
mkdir -p ~/mosquitto/certs
```

Generate certificates for Mosquitto using the provided script:

```bash
integration-testing/mosquitto/scripts/create_certificates.bash
```

Copy generated broker certificates

```bash
sudo cp broker_* /etc/mosquitto/certs/
sudo cp ca_certificate.pem /etc/mosquitto/ca_certificates/
sudo cp mosquitto.conf /etc/mosquitto/conf.d/
```

Start the Mosquitto broker:

```bash
mosquitto -c /etc/mosquitto/conf.d/mosquitto.conf
```

- Make sure your config, data, log, and certs directories exist and are populated as needed.
- The config file should enable listeners for all required ports and protocols (MQTT, MQTT+WS, TLS).
- The cert files for TLS will be generated and installed by the scripts above.

## How to Setup SOCKS5 Proxy

Start the SOCKS5 proxy:

```bash
docker run -p 1080:1080 \
  -e PROXY_USER=testuser -e PROXY_PASSWORD=testpass \
  serjs/go-socks5-proxy
```

- This will start a SOCKS5 proxy on localhost port 1080 with authentication enabled.
- The proxy will be accessible at `localhost:1080`.

## How to Build and Run the Test

### Build the Project

Make sure you have all dependencies installed (CMake, a C/C++ compiler, etc.). From the project root:

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

This will build all binaries required for the integration tests.

### Run the Integration Test

After building, you can run the Python harness from the build directory:

#### Python

```bash
python3 ../integration-testing/mqtt5_client_test.py ./bin/mqtt5_socks5_app/mqtt5_socks5_app
```

- Optional environment overrides:
  - `MQTT5_LOCAL_BROKER_HOST`
  - `MQTT5_LOCAL_CA_FILE`
  - `MQTT5_PROXY_HOST`, `MQTT5_PROXY_PORT`, `MQTT5_PROXY_USER`, `MQTT5_PROXY_PASSWORD`, `MQTT5_PROXY_USE_AUTH`
- Tests requiring a CA certificate automatically skip if the file is not found (useful when the local Mosquitto TLS setup is absent).

## Notes

- All integration test scripts and client invocations should use `localhost` for both Mosquitto and SOCKS5 proxy hosts.
- Certificate files for TLS are generated and installed locally.
- You can modify the Python harness to test other brokers, proxies, or authentication setups as needed.

## Example Test Cases Covered

- Direct MQTT (1883), no TLS
- Direct MQTT (8883), TLS
- Direct MQTT over WebSocket (8080/8081), with/without TLS
- Proxy MQTT (1883/8883), with/without TLS, with authentication
- Proxy MQTT over WebSocket (8080/8081), with/without TLS, with authentication
