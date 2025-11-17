# MQTT5 SOCKS5 Example over WebSockets with IAM

This document explains how to run the `mqtt5_socks5_app` sample against AWS IoT Core using MQTT5 over WebSockets with SigV4 (IAM) authentication. You can continue to layer SOCKS5 proxy connectivity on top of the WebSocket connection if needed.

## Prerequisites

- Build the sample as part of the standard `aws-crt-cpp` build:

  ```bash
  cmake --build <build-dir> --target mqtt5_socks5_app
  ```

- Ensure the executable `<build-dir>/bin/mqtt5_socks5_app` is available in your PATH or reference it by absolute path.
- Collect your AWS IoT endpoint (for example `abcdefghijklmnop-ats.iot.us-east-1.amazonaws.com`).
- Download the Amazon Root CA if your environment does not already trust it. You can pass it to the sample with `--ca-file /path/to/AmazonRootCA1.pem`.

## New command line options

When using WebSockets, the following options become relevant:

- `--websocket` – enables the SigV4 WebSocket flow.
- `--region <region>` – AWS Region used to sign the WebSocket upgrade request. **Required** when `--websocket` is set.
- `--credential-source <source>` – selects the IAM credentials provider. Supported values:
  - `default-chain` (default): cached chain of environment → profile → IMDS.
  - `environment`: resolves credentials exclusively from environment variables.
  - `profile`: resolves credentials from an AWS profile (use together with the overrides below).
  - `static`: uses credentials supplied on the command line.
- `--profile <name>` – profile to use when `--credential-source profile`.
- `--config-file <path>` / `--credentials-file <path>` – optional overrides for profile resolution.
- `--access-key <value>`, `--secret-key <value>`, `--session-token <value>` – static credentials for `--credential-source static`. Session token is optional.

When `--websocket` is enabled, the sample ignores any `--cert` or `--key` parameters because mTLS is not used for WebSocket/IAM authentication.

## Supplying credentials

### Default credential chain

No extra options are required. The application constructs the default cached chain (environment → AWS profile → IMDS/ECS) using the same event loop group as the MQTT client.

### Environment variables

Export the standard environment variables and use `--credential-source environment`:

```bash
export AWS_ACCESS_KEY_ID=AKIA...
export AWS_SECRET_ACCESS_KEY=...
export AWS_SESSION_TOKEN=... # optional

mqtt5_socks5_app --broker-host <endpoint> --websocket \
  --region us-east-1 --credential-source environment
```

### AWS profile

Use `--credential-source profile` plus optional overrides:

```bash
mqtt5_socks5_app --broker-host <endpoint> --websocket \
  --region us-west-2 --credential-source profile \
  --profile iot-lab --config-file ~/.aws/config \
  --credentials-file ~/.aws/credentials
```

### Static credentials

Provide the key material directly on the command line:

```bash
mqtt5_socks5_app --broker-host <endpoint> --websocket \
  --region eu-central-1 --credential-source static \
  --access-key AKIA... --secret-key abcd1234... \
  --session-token FwoG... # optional
```

## SOCKS5 proxy usage

SOCKS5 options remain available with WebSockets. For example:

```bash
mqtt5_socks5_app --broker-host <endpoint> --websocket \
  --region us-east-1 --proxy socks5h://proxy.example.com:1080
```

Proxy authentication and DNS resolution mode are automatically forwarded to the MQTT5 client.

## Tips

- The sample defaults to port `443` when `--websocket` is set and no explicit port is supplied.
- You can still provide `--ca-file` to trust a custom root CA. Client certificates are ignored during WebSocket/SigV4 flows.
- Use `--verbose` to enable trace logging if troubleshooting SigV4 signing or proxy connectivity.
