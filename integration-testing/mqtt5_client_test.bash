#!/bin/bash

# Set to 1 to use proxy authentication, 0 to disable
USE_PROXY_AUTH=1

# Local broker
LOCAL_BROKER_HOST=localhost
LOCAL_CA_FILE=/etc/mosquitto/ca_certificates/ca_certificate.pem

# Remote broker
REMOTE_BROKER_HOST=test.mosquitto.org
# relative path from build directory
REMOTE_CA_FILE=../integration-testing/mosquitto/certs/mosquitto-org-ca.crt
REMOTE_WS_CA_FILE=../integration-testing/mosquitto/certs/mosquitto-org-wss-ca.crt

PROXY_HOST=localhost
PROXY_PORT=1080
PROXY_URI_NOAUTH="socks5h://${PROXY_HOST}:${PROXY_PORT}"
PROXY_URI_AUTH="socks5h://testuser:testpass@${PROXY_HOST}:${PROXY_PORT}"
EXECUTABLE=./bin/mqtt5_socks5_app/mqtt5_socks5_app

declare -a TEST_NAMES
declare -a TEST_RESULTS
declare -a TEST_CODES

run_case() {
	echo ""
	echo ""
	local test_title="$1"
	echo "===== $test_title ====="
	shift
	echo "cmd:"
	echo "$@"
	"$@"
	local status=$?
	TEST_NAMES+=("$test_title")
	TEST_RESULTS+=("$status")
	TEST_CODES+=("$status")
}

print_summary() {
	GREEN='\033[0;32m'
	RED='\033[0;31m'
	NC='\033[0m' # No Color
	echo "===================="
	echo "Test Summary:"
	pass_count=0
	fail_count=0
	for i in "${!TEST_NAMES[@]}"; do
		name="${TEST_NAMES[$i]}"
		result="${TEST_RESULTS[$i]}"
		if [ "$result" -eq 0 ]; then
			echo -e "${GREEN}[PASS]${NC} $name"
			((pass_count++))
		else
			echo -e "${RED}[FAIL]${NC} $name (exit code ${TEST_CODES[$i]})"
			((fail_count++))
		fi
	done
	echo "--------------------"
	echo "Total: $((pass_count+fail_count)), Passed: $pass_count, Failed: $fail_count"
	echo "===================="
}

# Test case functions (parameterized)
test_direct_mqtt() {
	run_case "Direct MQTT (no proxy, no TLS) [$1]" \
		$EXECUTABLE --broker-host "$2" --broker-port 1883
}

test_direct_mqtts() {
	run_case "Direct MQTTS (no proxy, TLS) [$1]" \
		$EXECUTABLE --broker-host "$2" --broker-port 8883 --ca-file "$3"
}


test_proxy_mqtt() {
	if [ "$USE_PROXY_AUTH" -eq 1 ]; then
		run_case "Proxy MQTT (SOCKS5, no TLS, auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 1883 \
			--proxy "$PROXY_URI_AUTH"
	else
		run_case "Proxy MQTT (SOCKS5, no TLS, no-auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 1883 \
			--proxy "$PROXY_URI_NOAUTH"
	fi
}

test_proxy_mqtts() {
	if [ "$USE_PROXY_AUTH" -eq 1 ]; then
		run_case "Proxy MQTTS (SOCKS5, TLS, auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8883 \
			--proxy "$PROXY_URI_AUTH" \
			--ca-file "$3"
	else
		run_case "Proxy MQTTS (SOCKS5, TLS, no-auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8883 \
			--proxy "$PROXY_URI_NOAUTH" \
			--ca-file "$3"
	fi
}

# WebSocket variants
test_direct_mqtt_ws() {
	run_case "Direct MQTT over WebSocket (no proxy, no TLS) [$1]" \
		$EXECUTABLE --broker-host "$2" --broker-port 8080 --websocket
}

test_direct_mqtts_ws() {
	run_case "Direct MQTTS over WebSocket (no proxy, TLS) [$1]" \
		$EXECUTABLE --broker-host "$2" --broker-port 8081 --websocket --ca-file "$3"
}


test_proxy_mqtt_ws() {
	if [ "$USE_PROXY_AUTH" -eq 1 ]; then
		run_case "Proxy MQTT over WebSocket (SOCKS5, no TLS, auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8080 --websocket \
			--proxy "$PROXY_URI_AUTH"
	else
		run_case "Proxy MQTT over WebSocket (SOCKS5, no TLS, no-auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8080 --websocket \
			--proxy "$PROXY_URI_NOAUTH"
	fi
}

test_proxy_mqtts_ws() {
	if [ "$USE_PROXY_AUTH" -eq 1 ]; then
		run_case "Proxy MQTTS over WebSocket (SOCKS5, TLS, auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8081 --websocket \
			--proxy "$PROXY_URI_AUTH" \
			--ca-file "$3"
	else
		run_case "Proxy MQTTS over WebSocket (SOCKS5, TLS, no-auth) [$1]" \
			$EXECUTABLE --broker-host "$2" --broker-port 8081 --websocket \
			--proxy "$PROXY_URI_NOAUTH" \
			--ca-file "$3"
	fi
}


# Call all test cases for both local and remote brokers (each test on its own line)

# Direct broker tests (no proxy)
test_direct_mqtt "LOCAL" "$LOCAL_BROKER_HOST"
test_direct_mqtts "LOCAL" "$LOCAL_BROKER_HOST" "$LOCAL_CA_FILE"
test_direct_mqtt_ws "LOCAL" "$LOCAL_BROKER_HOST"
test_direct_mqtts_ws "LOCAL" "$LOCAL_BROKER_HOST" "$LOCAL_CA_FILE"

test_direct_mqtt "REMOTE" "$REMOTE_BROKER_HOST"
#test_direct_mqtts "REMOTE" "$REMOTE_BROKER_HOST" "$REMOTE_CA_FILE"
test_direct_mqtt_ws "REMOTE" "$REMOTE_BROKER_HOST"
test_direct_mqtts_ws "REMOTE" "$REMOTE_BROKER_HOST" "$REMOTE_WS_CA_FILE"

# Proxy broker tests
test_proxy_mqtt "LOCAL" "$LOCAL_BROKER_HOST"
test_proxy_mqtts "LOCAL" "$LOCAL_BROKER_HOST" "$LOCAL_CA_FILE"
test_proxy_mqtt_ws "LOCAL" "$LOCAL_BROKER_HOST"
test_proxy_mqtts_ws "LOCAL" "$LOCAL_BROKER_HOST" "$LOCAL_CA_FILE"

test_proxy_mqtt "REMOTE" "$REMOTE_BROKER_HOST"
#test_proxy_mqtts "REMOTE" "$REMOTE_BROKER_HOST" "$REMOTE_CA_FILE"
test_proxy_mqtt_ws "REMOTE" "$REMOTE_BROKER_HOST"
test_proxy_mqtts_ws "REMOTE" "$REMOTE_BROKER_HOST" "$REMOTE_WS_CA_FILE"

print_summary
