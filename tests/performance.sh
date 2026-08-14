#!/bin/sh
set -eu

program=${1:-./bin/cidr-compressor}
count=${PERF_COUNT:-1048576}
runs=${PERF_RUNS:-3}
log=${PERF_LOG:-performance/performance.log}
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cidr-compressor-perf.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

mkdir -p "$(dirname "$log")"
python3 scripts/generate_test_ips.py --family ipv4 --start 10.0.0.0 \
	--count "$count" --output "$tmpdir/ipv4.input"
python3 scripts/generate_test_ips.py --family ipv6 --start 2001:db8:: \
	--count "$count" --output "$tmpdir/ipv6.input"
cat "$tmpdir/ipv4.input" "$tmpdir/ipv6.input" > "$tmpdir/mixed.input"

timestamp=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
commit=$(git rev-parse --short HEAD 2>/dev/null || printf 'unknown')
{
	printf '\n[%s] commit=%s count=%s runs=%s\n' "$timestamp" "$commit" "$count" "$runs"
	printf 'workload\trun\tseconds\toutput_entries\n'
} >> "$log"

for workload in ipv4 ipv6 mixed; do
	input="$tmpdir/$workload.input"
	results="$tmpdir/$workload.times"
	: > "$results"
	for run in $(seq 1 "$runs"); do
		/usr/bin/time -f '%e' -o "$tmpdir/time" \
			"$program" --stats "$input" > /dev/null 2> "$tmpdir/stats"
		seconds=$(cat "$tmpdir/time")
	output_entries=$(sed -n 's/^output entries: //p' "$tmpdir/stats")
		printf '%s\t%s\t%s\t%s\n' "$workload" "$run" "$seconds" "$output_entries" >> "$log"
		printf '%s\n' "$seconds" >> "$results"
	done
	average=$(awk '{ total += $1 } END { printf "%.3f", total / NR }' "$results")
	printf '%s\taverage\t%s\t-\n' "$workload" "$average" >> "$log"
	printf 'PERF %-5s average: %ss (%s runs)\n' "$workload" "$average" "$runs"
done

printf 'Performance results appended to %s\n' "$log"
