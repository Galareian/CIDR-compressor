#!/usr/bin/env python3
"""Generate a large, deterministic IPv4 or IPv6 test file."""

import argparse
import ipaddress
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Generate a file containing sequential IPv4 addresses."
    )
    parser.add_argument(
        "-n", "--count", type=int, default=1_000_000,
        help="number of addresses to generate (default: 1,000,000)",
    )
    parser.add_argument(
        "-o", "--output", default="test-ips.txt",
        help="output file (default: test-ips.txt)",
    )
    parser.add_argument(
        "--start", default="10.0.0.0",
        help="first address (default: 10.0.0.0)",
    )
    parser.add_argument(
        "--family", choices=("ipv4", "ipv6"), default="ipv4",
        help="address family (default: ipv4)",
    )
    args = parser.parse_args()

    if args.count < 1:
        parser.error("count must be positive")

    try:
        address_type = ipaddress.IPv4Address if args.family == "ipv4" else ipaddress.IPv6Address
        start_address = address_type(args.start)
        start = int(start_address)
    except ipaddress.AddressValueError as error:
        parser.error(f"invalid {args.family} start address: {error}")

    max_address = 0xFFFFFFFF if args.family == "ipv4" else (1 << 128) - 1
    if start + args.count - 1 > max_address:
        parser.error(f"count exceeds the {args.family} address space from --start")

    with open(args.output, "w", buffering=1024 * 1024) as output:
        for value in range(start, start + args.count):
            if args.family == "ipv4":
                text = (f"{value >> 24}.{(value >> 16) & 255}."
                        f"{(value >> 8) & 255}.{value & 255}")
            else:
                text = str(ipaddress.IPv6Address(value))
            output.write(text + "\n")

    print(f"wrote {args.count:,} {args.family} addresses to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
