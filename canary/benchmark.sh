#!/bin/bash

set -e

# build bindhack if needed
bindhack=/tmp/bindhack.so
if [ ! -e ${bindhack} ]; then
	curl -sSL -o /tmp/bindhack.c http://wari.mckay.com/~rm/bindhack.c.txt
	gcc -fPIC -shared -o ${bindhack} /tmp/bindhack.c -lc -ldl
fi

mtu=9001
backlog=10000

instance_type=$(curl -sSL http://169.254.169.254/latest/meta-data/instance-type)
instance_id=$(curl -sSL http://169.254.169.254/latest/meta-data/instance-id)
region=$(curl -sSL http://169.254.169.254/latest/meta-data/placement/availability-zone | sed -E 's/[a-z]*$//')

uploads=0
downloads=160
threads=0

bucket_name=multicard-s3-test
object_name=crt-canary-obj-single-part-9223372036854775807

single_part=--measureSinglePartTransfer
multi_part=
use_tls=

echo "Enumerating local devices on ${instance_id}(${region}/${instance_type})..."
devices=($(ip link show | grep -E '^[0-9]+:[ ]+eth' | sed -E 's/[0-9]+: eth([0-9]+).+/eth\1/'))

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
        --devices=*)
            ;&
        --dev=*)
            devices=($(echo $1 | cut -f2 -d= | sed 's/,/ /g'))
            shift
            ;;
		--uploads=*)
			uploads=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--downloads=*)
			downloads=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--threads=*)
			threads=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--bucket=*)
			bucket_name=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--object=*)
			object_name=$(echo $1 | cut -f2 -d=)
			shift
			;;
		--multipart)
			multi_part=--measureMultiPartTransfer
			single_part=
			shift
			;;
		--tls)
			use_tls=--sendEncrypted
			shift
			;;
		*)
			local_ips=(${local_ips[@]} $1)
			shift
			;;
	esac
done

rm -f /tmp/benchmark_*.log

# Find local IP and NUMA node per device
declare -a local_ips=()
declare -a numa_nodes=()
for dev in ${devices[*]}; do
    local_ip=$(ip address show ${dev} | grep -E '^\s+inet ' | sed -E 's/.+inet ([0-9\.]+).+/\1/')
    local_ips+=(${local_ip})
    numa_node=$(cat /sys/class/net/${dev}/device/numa_node)
    numa_nodes+=(${numa_node})
done

# Dump some device info
echo Using devices:
idx=0
for dev in ${devices[*]}; do
    sudo ip link set dev ${dev} mtu ${mtu};
    echo Device: ${dev} Address: ${local_ips[${idx}]} MTU: ${mtu} NUMA: ${numa_nodes[$idx]}
    idx=$(($idx + 1))
done
sudo sysctl -w net.core.netdev_max_backlog=$backlog

# Run the canary in benchmark mode
pushd $(dirname $0) 2>&1 >/dev/null
declare -a pids=()
idx=0
for local_ip in ${local_ips[*]}; do
	echo -n Launching on ${devices[${idx}]}/${local_ip}...
	numa_node=${numa_nodes[$idx]}
	if [ -n "$numactl" ] && [ -n "$numa_node" ]; then
		numactl="${numactl} --${numactl_mode}=${numa_node}"
	fi
    log_file=/tmp/benchmark_${devices[$idx]}.log

	set -x
    LD_PRELOAD=${bindhack} BIND_SRC=${local_ip} ${numactl} ../build/canary/aws-crt-cpp-canary \
		-l \
		--toolName 'S3CRTBenchmark' --instanceType ${instance_type} \
		--region ${region} --metricsPublishingEnabled 0 \
		--downloadObjectName ${object_name} \
		--bucketName ${bucket_name} \
		--maxNumThreads ${threads} \
		${single_part} ${multi_part} ${use_tls} \
		--numTransfers ${uploads}:${downloads} --numConcurrentTransfers ${uploads}:${downloads} 2>&1 > ${log_file} &
	set +x
	pids+=($!)
	echo Launched $!
	idx=$(( $idx + 1 ))
done

popd 2>1 >/dev/null

# make sure ctrl+C kills the benchmarks too
kill_benchmarks() {
	echo Killing benchmarks...
	kill $(jobs -p)
	echo Done
}
trap 'kill_benchmarks' SIGINT

# Wait for all of the canaries to finish
for pid in ${pids[*]}; do
	echo Waiting for ${pid}
	wait $pid
done

# Results
echo Avg Line rate per interface:
line_rates=$(cat /tmp/benchmark*.log | grep BytesDown | grep -ve '^Possible' | sed -E 's/.+BytesDown,([0-9\.]+).+/\1/')
total_line_rate=0
for line_rate in ${line_rates[*]}; do
	total_line_rate=$(echo "$line_rate + $total_line_rate" | bc)
	echo $line_rate Gbps
done

echo Avg Overall Line Rate: $total_line_rate Gbps
