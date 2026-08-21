#!/bin/sh
set -eu

program=${1:-./bin/cidr-compressor}
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cidr-compressor.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

check() {
	name=$1
	input=$2
	expected=$3
	actual="$tmpdir/$name.actual"

	printf '%s' "$input" | "$program" > "$actual"
	if ! diff -u "$expected" "$actual"; then
		echo "FAIL: $name" >&2
		exit 1
	fi
	echo "PASS: $name"
}

printf '%s\n' \
	'10.0.0.0/25' \
	'10.0.0.128/25' \
	'10.0.1.1' \
	'# ignored' \
	'  ' > "$tmpdir/ipv4.input"
printf '%s\n' '10.0.0.0/24' '10.0.1.1' > "$tmpdir/ipv4.expected"
check ipv4 "$(cat "$tmpdir/ipv4.input")" "$tmpdir/ipv4.expected"

printf '%s\n' '2001:db8::/127' '2001:db8::2' > "$tmpdir/ipv6.expected"
check ipv6 '2001:db8::/127
2001:db8::2
' "$tmpdir/ipv6.expected"

printf '%s\n' '192.0.2.0/24' > "$tmpdir/file.input"
if [ "$("$program" "$tmpdir/file.input")" != '192.0.2.0/24' ]; then
	echo 'FAIL: file input' >&2
	exit 1
fi
echo 'PASS: file input'

if "$program" <<'EOF' 2>"$tmpdir/invalid.err" >/dev/null
not-an-ip
EOF
then
	if ! grep -q 'invalid address' "$tmpdir/invalid.err"; then
		echo 'FAIL: invalid input warning' >&2
		exit 1
	fi
else
	echo 'FAIL: invalid input handling' >&2
	exit 1
fi
echo 'PASS: invalid input warning'

max_prefix_actual="$tmpdir/max-prefix.actual"
printf '2001:db8::1\n2001:db8::2\n' | "$program" --max-prefix 64 > "$max_prefix_actual"
printf '%s\n' '2001:db8::1' '2001:db8::2' > "$tmpdir/max-prefix.expected"
if ! diff -u "$tmpdir/max-prefix.expected" "$max_prefix_actual"; then
	echo 'FAIL: maximum prefix length' >&2
	exit 1
fi
echo 'PASS: maximum prefix length'

printf '%s\n' '2001:db8::/65' | "$program" --max-prefix 64 > /dev/null 2>"$tmpdir/max-prefix.err"
if ! grep -q 'invalid address' "$tmpdir/max-prefix.err"; then
	echo 'FAIL: maximum prefix validation' >&2
	exit 1
fi
echo 'PASS: maximum prefix validation'

echo 'All tests passed.'
