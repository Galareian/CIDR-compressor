#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    uint32_t child[2];
    uint8_t terminal;
    uint8_t skip_len;
    unsigned char label[16];
} Node;

typedef struct NodePool {
    Node *nodes;
    size_t count;
    size_t capacity;
} NodePool;

enum { INITIAL_NODE_CAPACITY = 65536 };

/* Child index zero means "no child", so leave it unused. */
static NodePool pool = {.count = 1, .capacity = INITIAL_NODE_CAPACITY};

static inline Node *node_at(uint32_t index) {
    return pool.nodes + index;
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
                        int *max_bits, int max_prefix) {
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
    if (*bits > *max_bits || (slash && *bits > max_prefix)) return 0;
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

static int get_bit(const unsigned char address[16], int bit) {
    return (address[bit / 8] >> (7 - bit % 8)) & 1;
}

static void clear_bits(unsigned char address[16], int start, int end) {
    for (int bit = start; bit < end; bit++)
        address[bit / 8] &= (unsigned char)~(1u << (7 - bit % 8));
}

static void set_label(Node *node, const unsigned char address[16], int bits) {
    node->skip_len = (uint8_t)bits;
    memcpy(node->label, address, sizeof(node->label));
}

static uint32_t insert_at(uint32_t index, const unsigned char address[16],
                          int bits, int depth) {
    Node *node = node_at(index);
    int matched = 0;
    int prefix_depth = depth + node->skip_len;

    while (matched < node->skip_len && depth + matched < bits &&
           get_bit(node->label, depth + matched) ==
               get_bit(address, depth + matched))
        matched++;

    if (matched < node->skip_len) {
        int split_depth = depth + matched;
        int old_branch = get_bit(node->label, split_depth);
        uint32_t split_index = new_node();
        Node *split = node_at(split_index);

        set_label(split, address, matched);
        split->child[old_branch] = index;
        node = node_at(index);
        node->skip_len = (uint8_t)(node->skip_len - matched - 1);

        if (bits == split_depth) {
            split->terminal = 1;
        } else {
            int new_branch = get_bit(address, split_depth);
            uint32_t new_index = new_node();
            Node *new_child = node_at(new_index);
            split = node_at(split_index);
            set_label(new_child, address, bits - split_depth - 1);
            new_child->terminal = 1;
            split->child[new_branch] = new_index;
        }
        return split_index;
    }

    if (bits == prefix_depth) {
        node->terminal = 1;
        node->child[0] = node->child[1] = 0;
        return index;
    }
    if (node->terminal) return index;

    {
        int branch = get_bit(address, prefix_depth);
        uint32_t child = node->child[branch];
        if (child) {
            child = insert_at(child, address, bits, prefix_depth + 1);
            node = node_at(index);
            node->child[branch] = child;
        } else {
            child = new_node();
            node = node_at(index);
            node->child[branch] = child;
            set_label(node_at(child), address, bits - prefix_depth - 1);
            node_at(child)->terminal = 1;
        }
    }
    return index;
}

static void insert(uint32_t root, const unsigned char address[16], int bits) {
    insert_at(root, address, bits, 0);
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
                             int depth, OutputList *output, int max_prefix) {
    CompactResult left, right;
    Node *node;
    size_t output_start = output->count;
    int prefix_depth;

    if (!index) return (CompactResult){0};
    node = node_at(index);
    prefix_depth = depth + node->skip_len;
    for (int bit = depth; bit < prefix_depth; bit++) {
        if (get_bit(node->label, bit))
            address[bit / 8] |= (unsigned char)(1u << (7 - bit % 8));
        else
            address[bit / 8] &= (unsigned char)~(1u << (7 - bit % 8));
    }
    if (node->terminal) {
        append_output(output, address, prefix_depth);
        clear_bits(address, depth, prefix_depth);
        return (CompactResult){node->skip_len == 0};
    }

    address[prefix_depth / 8] &=
        (unsigned char)~(1u << (7 - prefix_depth % 8));
    left = compact(node->child[0], address, prefix_depth + 1, output,
                   max_prefix);
    address[prefix_depth / 8] |=
        (unsigned char)(1u << (7 - prefix_depth % 8));
    right = compact(node->child[1], address, prefix_depth + 1, output,
                    max_prefix);
    address[prefix_depth / 8] &=
        (unsigned char)~(1u << (7 - prefix_depth % 8));
    if (left.full && right.full && prefix_depth <= max_prefix) {
        node->terminal = 1;
        node->child[0] = node->child[1] = 0;
        output->count = output_start;
        append_output(output, address, prefix_depth);
        clear_bits(address, depth, prefix_depth);
        return (CompactResult){node->skip_len == 0};
    }
    clear_bits(address, depth, prefix_depth);
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
    int max_prefix = 128;
    size_t input_entries = 0;
    size_t output_entries;
    OutputList ipv4_output = {0};
    OutputList ipv6_output = {0};

    {
        int argument = 1;
        while (argument < argc) {
            if (strcmp(argv[argument], "--stats") == 0) {
                stats = 1;
                argument++;
            } else if (strcmp(argv[argument], "--max-prefix") == 0 &&
                       argument + 1 < argc) {
                char *end;
                long value = strtol(argv[argument + 1], &end, 10);
                if (*argv[argument + 1] == '\0' || *end || value < 0 || value > 128) {
                    fprintf(stderr, "invalid maximum prefix length: %s\n",
                            argv[argument + 1]);
                    return EXIT_FAILURE;
                }
                max_prefix = (int)value;
                argument += 2;
            } else if (strncmp(argv[argument], "--max-prefix=", 13) == 0) {
                char *end;
                long value = strtol(argv[argument] + 13, &end, 10);
                if (argv[argument][13] == '\0' || *end || value < 0 || value > 128) {
                    fprintf(stderr, "invalid maximum prefix length: %s\n",
                            argv[argument] + 13);
                    return EXIT_FAILURE;
                }
                max_prefix = (int)value;
                argument++;
            } else break;
        }
        if (argc - argument > 1) {
            fprintf(stderr, "usage: %s [--stats] [--max-prefix x] [input-file]\n", argv[0]);
            return EXIT_FAILURE;
        }
        if (argc - argument == 1) {
            input = fopen(argv[argument], "r");
        if (!input) {
            perror(argv[argument]);
            return EXIT_FAILURE;
        }
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
        family = parse_prefix(start, address, &bits, &max_bits, max_prefix);
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
        compact(ipv4, address, 0, &ipv4_output, max_prefix);
        memset(address, 0, sizeof(address));
        compact(ipv6, address, 0, &ipv6_output, max_prefix);
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
