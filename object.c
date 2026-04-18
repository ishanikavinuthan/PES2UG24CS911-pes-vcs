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

#include <openssl/sha.h>

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

    // 🔥 REAL HASH (instead of compute_hash)
    SHA256(buffer, total_size, id_out->hash);

    // 🔥 CREATE BASE DIR
    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);

    // 🔥 BUILD PATH MANUALLY
    char path[512];
    sprintf(path, ".pes/objects/%02x", id_out->hash[0]);
    mkdir(path, 0755);

    sprintf(path + strlen(path), "/");
    for (int i = 1; i < HASH_SIZE; i++) {
        sprintf(path + strlen(path), "%02x", id_out->hash[i]);
    }

    // 🔥 CHECK IF FILE EXISTS (dedup)
    if (access(path, F_OK) == 0) {
        free(buffer);
        return 0;
    }

    // 🔥 WRITE FILE
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
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {

    char path[512];
    sprintf(path, ".pes/objects/%02x/", id->hash[0]);

    for (int i = 1; i < HASH_SIZE; i++) {
        sprintf(path + strlen(path), "%02x", id->hash[i]);
    }

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    unsigned char *buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);

    // VERIFY HASH
    unsigned char check[HASH_SIZE];
    SHA256(buffer, size, check);

    if (memcmp(check, id->hash, HASH_SIZE) != 0) {
        free(buffer);
        return -1;
    }

    // PARSE HEADER
    char *null_pos = memchr(buffer, '\0', size);
    if (!null_pos) {
        free(buffer);
        return -1;
    }

    size_t header_len = null_pos - (char *)buffer;
    size_t data_len = size - header_len - 1;

    *data_out = malloc(data_len);
    memcpy(*data_out, null_pos + 1, data_len);
    *len_out = data_len;

    if (strncmp((char *)buffer, "blob", 4) == 0)
        *type_out = OBJ_BLOB;
    else if (strncmp((char *)buffer, "tree", 4) == 0)
        *type_out = OBJ_TREE;
    else
        *type_out = OBJ_COMMIT;

    free(buffer);
    return 0;
}

void hash_to_hex(const ObjectID *id, char *hex) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex + i * 2, "%02x", id->hash[i]);
    }
    hex[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sscanf(hex + 2 * i, "%2hhx", &id->hash[i]);
    }
    return 0;
}
void object_path(const ObjectID *id, char *path, size_t size) {
    snprintf(path, size, ".pes/objects/%02x", id->hash[0]);

    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);
    mkdir(path, 0755);

    size_t len = strlen(path);
    snprintf(path + len, size - len, "/");

    for (int i = 1; i < HASH_SIZE; i++) {
        snprintf(path + strlen(path), size - strlen(path), "%02x", id->hash[i]);
    }
}