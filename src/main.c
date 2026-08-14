#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    uint32_t child[2];
    uint8_t terminal;
} Node;

typedef struct NodePool {
    Node *nodes;
    size_t count;
    size_t capacity;
} NodePool;

enum { INITIAL_NODE_CAPACITY = 65536 };

/* Child index zero means "no child", so leave it unused. */
static NodePool pool = {.count = 1, .capacity = INITIAL_NODE_CAPACITY};

static Node *node_at(uint32_t index) {
    return &pool.nodes[index];
}

static uint32_t new_node(void) {
    if (!pool.nodes || pool.count == pool.capacity) {
        size_t capacity = pool.capacity ? pool.capacity * 2 : 1024;
        Node *nodes = realloc(pool.nodes, capacity * sizeof(*nodes));
        if (!nodes) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        pool.nodes = nodes;
        pool.capacity = capacity;
    }
    memset(&pool.nodes[pool.count], 0, sizeof(pool.nodes[pool.count]));
    return (uint32_t)pool.count++;
}

static void free_nodes(void) {
    free(pool.nodes);
}

static int parse_prefix(char *text, unsigned char address[16], int *bits,
                        int *max_bits) {
    char *slash = strchr(text, '/');
    int family;

    if (slash) {
        char *end;
        long value;
        *slash = '\0';
        errno = 0;
        value = strtol(slash + 1, &end, 10);
        if (errno || *end || value < 0) return 0;
        *bits = (int)value;
    } else {
        *bits = -1;
    }

    memset(address, 0, 16);
    if (inet_pton(AF_INET, text, address) == 1) {
        family = AF_INET;
        *max_bits = 32;
    } else if (inet_pton(AF_INET6, text, address) == 1) {
        family = AF_INET6;
        *max_bits = 128;
    } else {
        return 0;
    }

    if (*bits < 0) *bits = *max_bits;
    if (*bits > *max_bits) return 0;
    if (family == AF_INET) memset(address + 4, 0, 12);

    {
        int byte = *bits / 8;
        int remainder = *bits % 8;
        int address_bytes = *max_bits / 8;

        if (remainder) {
            address[byte] &= (unsigned char)(0xffu << (8 - remainder));
            byte++;
        }
        if (byte < address_bytes)
            memset(address + byte, 0, (size_t)(address_bytes - byte));
    }
    return family;
}

static void insert(uint32_t root, const unsigned char address[16], int bits) {
    uint32_t index = root;
    Node *node = node_at(root);
    for (int bit = 0; bit < bits; bit++) {
        int value = (address[bit / 8] >> (7 - bit % 8)) & 1;
        uint32_t child = node->child[value];
        if (!child) {
            child = new_node();
            node = node_at(index);
            node->child[value] = child;
        }
        index = child;
        node = node_at(index);
        if (node->terminal) return;
    }
    node->terminal = 1;
    node->child[0] = node->child[1] = 0;
}

/* Compact bottom-up and return whether this subtree is completely covered. */
typedef struct CompactResult {
    int full;
} CompactResult;

typedef struct OutputEntry {
    unsigned char address[16];
    int depth;
} OutputEntry;

typedef struct OutputList {
    OutputEntry *entries;
    size_t count;
    size_t capacity;
} OutputList;

static void append_output(OutputList *output, const unsigned char address[16],
                          int depth) {
    if (output->count == output->capacity) {
        size_t capacity = output->capacity ? output->capacity * 2 : 1024;
        OutputEntry *entries = realloc(output->entries,
                                       capacity * sizeof(*entries));
        if (!entries) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        output->entries = entries;
        output->capacity = capacity;
    }
    memcpy(output->entries[output->count].address, address, 16);
    output->entries[output->count++].depth = depth;
}

static CompactResult compact(uint32_t index, unsigned char address[16],
                             int depth, OutputList *output) {
    CompactResult left, right;
    Node *node;
    size_t output_start = output->count;

    if (!index) return (CompactResult){0};
    node = node_at(index);
    if (node->terminal) {
        append_output(output, address, depth);
        return (CompactResult){1};
    }

    left = compact(node->child[0], address, depth + 1, output);
    address[depth / 8] |= (unsigned char)(1u << (7 - depth % 8));
    right = compact(node->child[1], address, depth + 1, output);
    address[depth / 8] &= (unsigned char)~(1u << (7 - depth % 8));
    if (left.full && right.full) {
        node->terminal = 1;
        node->child[0] = node->child[1] = 0;
        output->count = output_start;
        append_output(output, address, depth);
        return (CompactResult){1};
    }
    return (CompactResult){0};
}

static void print_outputs(const OutputList *output, int max_bits) {
    char text[INET6_ADDRSTRLEN];
    for (size_t index = 0; index < output->count; index++) {
        const OutputEntry *entry = &output->entries[index];
        if (inet_ntop(max_bits == 32 ? AF_INET : AF_INET6, entry->address, text,
                      sizeof(text))) {
            if (entry->depth == max_bits) printf("%s\n", text);
            else printf("%s/%d\n", text, entry->depth);
        }
    }
}

int main(int argc, char **argv) {
    FILE *input = stdin;
    uint32_t ipv4 = new_node();
    uint32_t ipv6 = new_node();
    char line[512];
    int line_number = 0;
    int stats = 0;
    size_t input_entries = 0;
    size_t output_entries;
    OutputList ipv4_output = {0};
    OutputList ipv6_output = {0};

    if (argc > 1 && strcmp(argv[1], "--stats") == 0) {
        stats = 1;
        argv++;
        argc--;
    }
    if (argc > 2) {
        fprintf(stderr, "usage: %s [--stats] [input-file]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }

    while (fgets(line, sizeof(line), input)) {
        unsigned char address[16];
        int bits, max_bits, family;
        char *start = line;
        char *comment;
        line_number++;
        while (*start == ' ' || *start == '\t') start++;
        comment = strchr(start, '#');
        if (comment) *comment = '\0';
        start[strcspn(start, " \t\r\n")] = '\0';
        if (!*start) continue;
        family = parse_prefix(start, address, &bits, &max_bits);
        if (!family) {
            fprintf(stderr, "invalid address on line %d: %s\n", line_number, start);
            continue;
        }
        input_entries++;
        insert(family == AF_INET ? ipv4 : ipv6, address, bits);
    }

    if (ferror(input)) perror("read");
    if (input != stdin) fclose(input);
    {
        unsigned char address[16] = {0};
        compact(ipv4, address, 0, &ipv4_output);
        memset(address, 0, sizeof(address));
        compact(ipv6, address, 0, &ipv6_output);
    }
    output_entries = ipv4_output.count + ipv6_output.count;
    print_outputs(&ipv4_output, 32);
    print_outputs(&ipv6_output, 128);
    if (stats) {
        double reduction = input_entries
                               ? 100.0 * (1.0 - (double)output_entries / input_entries)
                               : 0.0;
        fprintf(stderr, "input entries: %zu\noutput entries: %zu\ncompression: %.2f%%\n",
                input_entries, output_entries, reduction);
    }
    free(ipv4_output.entries);
    free(ipv6_output.entries);
    free_nodes();
    return EXIT_SUCCESS;
}
