#!/bin/bash

set -e

pushd $(dirname $0) 2>&1 >/dev/null

# build bindhack
if [ ! -e bindhack.so ]; then
	curl -sSL -o bindhack.c http://wari.mckay.com/~rm/bindhack.c.txt
	gcc -fPIC -shared -o bindhack.so bindhack.c -lc -ldl
fi

# get local ips from command line
declare -a local_ips=()
while (( "$#" )); do
	case "$1" in
		--numactl)
			numactl=numactl
			numactl_mode=preferred
			shift
			;;
		--preferred)
			numactl_mode=preferred
			shift
			;;
		--cpunodebind)
			numactl_mode=cpunodebind
			shift
			;;
		--mtu=*)
			mtu=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--backlog=*)
			backlog=$(echo $1 | cut -f2 -d=)
			sysctl net.core.netdev_max_backlog=$backlog
			shift
			;;
		*)
			local_ips=(${local_ips[@]} $1)
			shift
			;;
	esac
done

rm -f /tmp/benchmark_*.log

echo Local Interfaces: ${local_ips[@]}

declare -a pids=()
idx=0
for local_ip in "${local_ips[@]}"; do
	echo Launching on ${local_ip}
	if [[ $idx -ge 2 ]]; then
		numa_node=0
	else
		numa_node=1
	fi
	if [ -n "$numactl" ]; then
		numactl="${numactl} --${numactl_mode}=${numa_node}"
	fi

	LD_PRELOAD=`pwd`/bindhack.so BIND_SRC=${local_ip} ${numactl} ./build/canary/aws-crt-cpp-canary -g canary_config_no_upload_100.json 2>&1 > /tmp/benchmark_${local_ip}.log &
	pids=(${pids[@]} $!)
	echo Launched ${pids[-1]}
	idx=$(( $idx + 1 ))
done

kill_benchmarks() {
	echo Killing benchmarks...
	kill $(jobs -p)
	echo Done
}

trap 'kill_benchmarks' SIGINT

for pid in ${pids[*]}; do
	echo Waiting for ${pid}
	wait $pid
done

echo Avg Line rate per interface:
line_rates=$(cat /tmp/benchmark*.log | grep BytesDown | grep -ve '^Possible' | sed -E 's/.+BytesDown,([0-9\.]+).+/\1/')
total_line_rate=0
for line_rate in ${line_rates[*]}; do
	total_line_rate=$(echo "$line_rate + $total_line_rate" | bc)
	echo $line_rate Gbps
done

echo Avg Total: $total_line_rate Gbps

popd 2>1 >/dev/null
