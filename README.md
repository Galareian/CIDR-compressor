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
