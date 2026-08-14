#!/bin/sh
set -eu

program=${1:-./bin/cidr-compressor}
count=${STRESS_COUNT:-1048576}
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cidr-compressor-stress.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

generate() {
	python3 scripts/generate_test_ips.py \
		--family "$1" --start "$2" --count "$count" --output "$3"
}

check() {
	name=$1
	input=$2
	expected=$3

	"$program" --stats "$input" > "$tmpdir/$name.output" 2> "$tmpdir/$name.stats"
	diff -u "$expected" "$tmpdir/$name.output"
	grep -q "input entries: $count" "$tmpdir/$name.stats"
	echo "PASS: $name ($count entries)"
}

generate ipv4 10.0.0.0 "$tmpdir/ipv4.input"
printf '10.0.0.0/12\n' > "$tmpdir/ipv4.expected"
check ipv4 "$tmpdir/ipv4.input" "$tmpdir/ipv4.expected"

generate ipv6 2001:db8:: "$tmpdir/ipv6.input"
printf '2001:db8::/108\n' > "$tmpdir/ipv6.expected"
check ipv6 "$tmpdir/ipv6.input" "$tmpdir/ipv6.expected"

cat "$tmpdir/ipv4.input" "$tmpdir/ipv6.input" > "$tmpdir/mixed.input"
cat "$tmpdir/ipv4.expected" "$tmpdir/ipv6.expected" > "$tmpdir/mixed.expected"
"$program" --stats "$tmpdir/mixed.input" > "$tmpdir/mixed.output" 2> "$tmpdir/mixed.stats"
diff -u "$tmpdir/mixed.expected" "$tmpdir/mixed.output"
grep -q "input entries: $((count * 2))" "$tmpdir/mixed.stats"
echo "PASS: mixed ($((count * 2)) entries)"

echo 'All stress tests passed.'
