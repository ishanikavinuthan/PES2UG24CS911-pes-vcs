// commit.c — Commit creation and history traversal
//
// Commit object format (stored as text, one field per line):
//
//   tree <64-char-hex-hash>
//   parent <64-char-hex-hash>        ← omitted for the first commit
//   author <name> <unix-timestamp>
//   committer <name> <unix-timestamp>
//
//   <commit message>
//
// Note: there is a blank line between the headers and the message.
//
// PROVIDED functions: commit_parse, commit_serialize, commit_walk, head_read, head_update
// TODO functions:     commit_create

#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/stat.h>

// Forward declarations (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── PROVIDED ────────────────────────────────────────────────────────────────

// Parse raw commit data into a Commit struct.
int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len;
    const char *p = (const char *)data;
    char hex[HASH_HEX_SIZE + 1];

    // "tree <hex>\n"
    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    if (hex_to_hash(hex, &commit_out->tree) != 0) return -1;
    p = strchr(p, '\n') + 1;

    // optional "parent <hex>\n"
    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        if (hex_to_hash(hex, &commit_out->parent) != 0) return -1;
        commit_out->has_parent = 1;
        p = strchr(p, '\n') + 1;
    } else {
        commit_out->has_parent = 0;
    }

    // "author <name> <timestamp>\n"
    char author_buf[256];
    uint64_t ts;
    if (sscanf(p, "author %255[^\n]\n", author_buf) != 1) return -1;
    // split off trailing timestamp
    char *last_space = strrchr(author_buf, ' ');
    if (!last_space) return -1;
    ts = (uint64_t)strtoull(last_space + 1, NULL, 10);
    *last_space = '\0';
    snprintf(commit_out->author, sizeof(commit_out->author), "%s", author_buf);
    commit_out->timestamp = ts;
    p = strchr(p, '\n') + 1;  // skip author line
    p = strchr(p, '\n') + 1;  // skip committer line
    p = strchr(p, '\n') + 1;  // skip blank line

    snprintf(commit_out->message, sizeof(commit_out->message), "%s", p);
    return 0;
}

// Serialize a Commit struct to the text format.
// Caller must free(*data_out).
int commit_serialize(const Commit *c, void **data_out, size_t *len_out) {
    char buffer[8192];

    int offset = 0;

    // tree
    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&c->tree, tree_hex);
    offset += sprintf(buffer + offset, "tree %s\n", tree_hex);

    // parent (if exists)
    if (c->has_parent) {
        char parent_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&c->parent, parent_hex);
        offset += sprintf(buffer + offset, "parent %s\n", parent_hex);
    }

    // author
    offset += sprintf(buffer + offset, "author %s\n", c->author);

    // timestamp
    offset += sprintf(buffer + offset, "time %llu\n",
                      (unsigned long long)c->timestamp);

    // blank line
    offset += sprintf(buffer + offset, "\n");

    // message
    offset += sprintf(buffer + offset, "%s\n", c->message);

    // allocate output
    *data_out = malloc(offset);
    if (!*data_out) return -1;

    memcpy(*data_out, buffer, offset);
    *len_out = offset;

    return 0;
}
// Walk commit history from HEAD to the root.
int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;

    while (1) {
        ObjectType type;
        void *raw;
        size_t raw_len;
        if (object_read(&id, &type, &raw, &raw_len) != 0) return -1;

        Commit c;
        int rc = commit_parse(raw, raw_len, &c);
        free(raw);
        if (rc != 0) return -1;

        callback(&id, &c, ctx);

        if (!c.has_parent) break;
        id = c.parent;
    }
    return 0;
}

// Read the current HEAD commit hash.
int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;

    char ref[256];
    if (!fgets(ref, sizeof(ref), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    // 🔥 FIXED
    if (strncmp(ref, "ref:", 4) != 0) return -1;

    char *ref_name = ref + 4;
    while (*ref_name == ' ') ref_name++;  // skip spaces

    ref_name[strcspn(ref_name, "\n")] = 0;

    char ref_path[512];
    snprintf(ref_path, sizeof(ref_path), ".pes/%s", ref_name);

    FILE *rf = fopen(ref_path, "r");
    if (!rf) return -1;

    char hex[HASH_HEX_SIZE + 1];
    if (!fgets(hex, sizeof(hex), rf)) {
        fclose(rf);
        return -1;
    }
    fclose(rf);

    hex[strcspn(hex, "\n")] = 0;
    return hex_to_hash(hex, id_out);
}
// Update the current branch ref to point to a new commit atomically.
int head_update(const ObjectID *new_commit) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);

    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;

    char ref[256];
    if (!fgets(ref, sizeof(ref), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (strncmp(ref, "ref: ", 5) != 0) return -1;

    char *ref_name = ref + 5;
    ref_name[strcspn(ref_name, "\n")] = 0;

    char ref_path[512];
    snprintf(ref_path, sizeof(ref_path), ".pes/%s", ref_name);

    mkdir(".pes", 0755);
    mkdir(".pes/refs", 0755);
    mkdir(".pes/refs/heads", 0755);

    FILE *rf = fopen(ref_path, "w");
    if (!rf) return -1;

    fprintf(rf, "%s\n", hex);
    fclose(rf);

    return 0;
}
// ─── TODO: Implement these ───────────────────────────────────────────────────

// Create a new commit from the current staging area.
//
// HINTS - Useful functions to call:
//   - tree_from_index   : writes the directory tree and gets the root hash
//   - head_read         : gets the parent commit hash (if any)
//   - pes_author        : retrieves the author name string (from pes.h)
//   - time(NULL)        : gets the current unix timestamp
//   - commit_serialize  : converts the filled Commit struct to a text buffer
//   - object_write      : saves the serialized text as OBJ_COMMIT
//   - head_update       : moves the branch pointer to your new commit
//
// Returns 0 on success, -1 on error.
int commit_create(const char *message, ObjectID *commit_id_out) {
    printf("STEP 1: tree\n");
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) {
        printf("FAIL: tree_from_index\n");
        return -1;
    }

    printf("STEP 2: struct\n");
    Commit c;
    memset(&c, 0, sizeof(Commit));
    c.tree = tree_id;

    printf("STEP 3: head_read\n");
    if (head_read(&c.parent) == 0) {
        c.has_parent = 1;
    } else {
        c.has_parent = 0;
    }

    printf("STEP 4: author\n");
    const char *author = getenv("PES_AUTHOR");
    if (!author) author = "unknown";
    snprintf(c.author, sizeof(c.author), "%s", author);

    printf("STEP 5: time\n");
    c.timestamp = time(NULL);

    printf("STEP 6: message\n");
    snprintf(c.message, sizeof(c.message), "%s", message);

    printf("STEP 7: serialize\n");
    void *data = NULL;
    size_t len = 0;
    if (commit_serialize(&c, &data, &len) != 0) {
        printf("FAIL: serialize\n");
        return -1;
    }

    printf("STEP 8: object_write\n");
    if (object_write(OBJ_COMMIT, data, len, commit_id_out) != 0) {
        printf("FAIL: object_write\n");
        free(data);
        return -1;
    }

    free(data);

    printf("STEP 9: head_update\n");
    if (head_update(commit_id_out) != 0) {
        printf("FAIL: head_update\n");
        return -1;
    }

    printf("SUCCESS\n");
    return 0;
}