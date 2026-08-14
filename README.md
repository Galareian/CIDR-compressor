# CIDR-compressor

![CIDR-compressor](assets/hero-text.png)

CIDR-compressor is a simple command-line tool (written in C) that takes a
list of individual IPs and CIDR ranges and compresses them into the smallest
possible set of IPs and CIDR blocks. It's handy when importing large address
lists into services that limit list size (for example Cloudflare's 10,000
item limit).

Quick start
----------

Build (requires `gcc`):

```bash
mkdir -p bin
gcc -O2 -std=c11 -o bin/cidr-compressor src/*.c
```

Basic usage:

```bash
# read from a file
./bin/cidr-compressor input.txt

# read from stdin
cat input.txt | ./bin/cidr-compressor > compressed.txt
```

Input supports single IPv4/IPv6 addresses and CIDR ranges (one per line).
Lines starting with `#` and blank lines are ignored.

Example input:

```
203.0.113.5
198.51.100.0/24
2001:db8::1
# comment
```

Example output (compressed):

```
198.51.100.0/24
203.0.113.0/30
203.0.113.4
2001:db8::1
```

Inspiration
-----------

This project started with a practical need: I needed a small, dependable CIDR
compressor for working with lists of IP addresses and ranges. Rather than
reaching for another large dependency or treating the problem as a black box,
I decided to revisit an idea I first encountered at university many years
ago—the humble tree structure.

There is something satisfying about seeing that old concept become useful
again. CIDR ranges naturally form a hierarchy, and a tree provides an elegant
way to walk that hierarchy, combine neighbouring networks, and reduce a long
list of addresses to its simplest representation. What began as a utility for
solving an immediate problem became a small exercise in reconnecting theory
with practice.

The result is intentionally straightforward: a focused command-line tool,
written in C, that keeps the algorithm visible and the output useful. It is a
little reminder that ideas learned years ago can remain quietly valuable—and
that sometimes the best way to solve a modern problem is to dust off an old
one.
