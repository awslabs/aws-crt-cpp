#!/bin/bash

set -e

pushd $(dirname $0) 2>&1 >/dev/null

# build bindhack
if [ ! -e bindhack.so ]; then
	curl -sSL -o bindhack.c http://wari.mckay.com/~rm/bindhack.c.txt
	gcc -fPIC -shared -o bindhack.so bindhack.c -lc -ldl
fi

mtu=9001

echo Enumerating local devices...
devices=$(ip link show | grep -e '^[0-9]' | sed -E 's/[0-9]+: eth([0-9]+).+/eth\1/')

declare -a local_ips=()
declare -a numa_nodes=()
for dev in ${devices[*]}; do
    local_ip=$(ip address show ${dev} | grep -E '^\s+inet ' | sed -E 's/.+inet ([0-9\.]+).+/\1/')
    local_ips=(${local_ips} ${local_ip})
    numa_node=$(cat /sys/class/net/${dev}/device/numa_node)
    numa_nodes=(${numa_nodes} ${numa_node})
done

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
			shift
			;;
        --dev=*
            devices=$(echo $1 | cut -f2 -d= | sed 's/,/ /g')
            shift
            ;;
		*)
			local_ips=(${local_ips[@]} $1)
			shift
			;;
	esac
done

rm -f /tmp/benchmark_*.log

echo Using devices:
idx=0
for dev in ${devices[*]}; do
    sudo ip link set dev ${dev} mtu ${mtu};
    echo Device: ${dev} Address: ${local_ips[${idx}]} MTU: ${mtu} NUMA: ${numa_nodes[$idx]}
    idx=$(($idx + 1))
done
sudo sysctl -w net.core.netdev_max_backlog=$backlog

declare -a pids=()
idx=0
for local_ip in ${local_ips[*]}; do
	echo Launching on ${local_ip}
	numa_node=${numa_nodes[$idx]}
	if [ -n "$numactl" && -n "$numa_node" ]; then
		numactl="${numactl} --${numactl_mode}=${numa_node}"
	fi

    set -x
	LD_PRELOAD=`pwd`/bindhack.so BIND_SRC=${local_ip} ${numactl} ./build/canary/aws-crt-cpp-canary -g canary_config_no_upload_100.json 2>&1 > /tmp/benchmark_${devices[$idx]}.log &
    set +x
	pids=(${pids[@]} $!)
	echo Launched ${pids[-1]}
	idx=$(( $idx + 1 ))
done

kill_benchmarks() {
	echo Killing benchmarks...
	kill $(jobs -p)
	echo Done
}

# make sure ctrl+C kills the benchmarks too
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
