// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {

    char header[64];
    int header_len = sprintf(header, "%s %zu",
        type == OBJ_BLOB ? "blob" :
        type == OBJ_TREE ? "tree" : "commit",
        len);

    size_t total_size = header_len + 1 + len;
    unsigned char *buffer = malloc(total_size);

    memcpy(buffer, header, header_len);
    buffer[header_len] = '\0';
    memcpy(buffer + header_len + 1, data, len);

    compute_hash(buffer, total_size, id_out);

    // Deduplication
    if (object_exists(id_out)) {
        free(buffer);
        return 0;
    }

    char path[512];
    object_path(id_out, path, sizeof(path));

    // Create directory
    char dir[512];
    strncpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(OBJECTS_DIR, 0755);
        mkdir(dir, 0755);
    }

    // Write file
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buffer);
        return -1;
    }

    fwrite(buffer, 1, total_size, f);
    fclose(f);

    free(buffer);
    return 0;
}