#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    struct Node *child[2];
    int terminal;
} Node;

static Node *new_node(void) {
    Node *node = calloc(1, sizeof(*node));
    if (!node) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    return node;
}

static void free_tree(Node *node) {
    if (!node) return;
    free_tree(node->child[0]);
    free_tree(node->child[1]);
    free(node);
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

    for (int bit = *bits; bit < *max_bits; bit++)
        address[bit / 8] &= (unsigned char)~(1u << (7 - bit % 8));
    return family;
}

static void insert(Node *root, const unsigned char address[16], int bits) {
    Node *node = root;
    for (int bit = 0; bit < bits; bit++) {
        int value = (address[bit / 8] >> (7 - bit % 8)) & 1;
        if (!node->child[value]) node->child[value] = new_node();
        node = node->child[value];
        if (node->terminal) return;
    }
    node->terminal = 1;
    free_tree(node->child[0]);
    free_tree(node->child[1]);
    node->child[0] = node->child[1] = NULL;
}

static int is_full(const Node *node) {
    return node && (node->terminal ||
                    (is_full(node->child[0]) && is_full(node->child[1])));
}

static void compact(Node *node) {
    if (!node || node->terminal) return;
    compact(node->child[0]);
    compact(node->child[1]);
    if (is_full(node->child[0]) && is_full(node->child[1])) {
        free_tree(node->child[0]);
        free_tree(node->child[1]);
        node->child[0] = node->child[1] = NULL;
        node->terminal = 1;
    }
}

static size_t print_tree(const Node *node, unsigned char address[16], int depth,
                         int max_bits) {
    char text[INET6_ADDRSTRLEN];
    if (!node) return 0;
    if (node->terminal) {
        if (inet_ntop(max_bits == 32 ? AF_INET : AF_INET6, address, text,
                      sizeof(text))) {
            if (depth == max_bits) printf("%s\n", text);
            else printf("%s/%d\n", text, depth);
        }
        return 1;
    }

    size_t count = 0;
    for (int branch = 0; branch < 2; branch++) {
        if (node->child[branch]) {
            if (branch) address[depth / 8] |= (unsigned char)(1u << (7 - depth % 8));
            count += print_tree(node->child[branch], address, depth + 1, max_bits);
            if (branch) address[depth / 8] &= (unsigned char)~(1u << (7 - depth % 8));
        }
    }
    return count;
}

int main(int argc, char **argv) {
    FILE *input = stdin;
    Node *ipv4 = new_node();
    Node *ipv6 = new_node();
    char line[512];
    int line_number = 0;
    int stats = 0;
    size_t input_entries = 0;
    size_t output_entries;

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
    compact(ipv4);
    compact(ipv6);
    {
        unsigned char address[16] = {0};
        output_entries = print_tree(ipv4, address, 0, 32);
        memset(address, 0, sizeof(address));
        output_entries += print_tree(ipv6, address, 0, 128);
    }
    if (stats) {
        double reduction = input_entries
                               ? 100.0 * (1.0 - (double)output_entries / input_entries)
                               : 0.0;
        fprintf(stderr, "input entries: %zu\noutput entries: %zu\ncompression: %.2f%%\n",
                input_entries, output_entries, reduction);
    }
    free_tree(ipv4);
    free_tree(ipv6);
    return EXIT_SUCCESS;
}
