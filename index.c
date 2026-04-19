#include "index.h"
#include "pes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
extern int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) printf("  (nothing to show)\n");

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
    }

    printf("\nUnstaged changes:\n");
    int found = 0;

    for (int i = 0; i < index->count; i++) {
        struct stat st;

        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            found = 1;
        } else {
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                found = 1;
            }
        }
    }

    if (!found) printf("  (nothing to show)\n");

    printf("\nUntracked files:\n");
    found = 0;

    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..") ||
                !strcmp(ent->d_name, ".pes") || !strcmp(ent->d_name, "pes") ||
                strstr(ent->d_name, ".o"))
                continue;

            int tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (!strcmp(index->entries[i].path, ent->d_name)) {
                    tracked = 1;
                    break;
                }
            }

            if (!tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) {
                    printf("  untracked:  %s\n", ent->d_name);
                    found = 1;
                }
            }
        }
        closedir(dir);
    }

    if (!found) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ─── IMPLEMENTED ─────────────────────────────────────────────────────────────

int index_load(Index *idx) {
    idx->count = 0;

    FILE *fp = fopen(".pes/index", "r");
    if (!fp) return 0;

    while (1) {
        IndexEntry e;
        char hex[HASH_HEX_SIZE + 1];

        int res = fscanf(fp, "%o %s %lu %u %s\n",
                         &e.mode, hex, &e.mtime_sec, &e.size, e.path);

        if (res != 5) break;

        hex_to_hash(hex, &e.hash);
        idx->entries[idx->count++] = e;
    }

    fclose(fp);
    return 0;
}

int index_save(const Index *idx) {
    FILE *fp = fopen(".pes/index", "w");
    if (!fp) return -1;

    for (int i = 0; i < idx->count; i++) {
        const IndexEntry *e = &idx->entries[i];

        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hex);

        fprintf(fp, "%o %s %lu %u %s\n",
                e->mode, hex, e->mtime_sec, e->size, e->path);
    }

    fclose(fp);
    return 0;
}

int index_add(Index *idx, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    void *data = malloc(size);
    fread(data, 1, size, fp);
    fclose(fp);

    ObjectID id;
    object_write(OBJ_BLOB, data, size, &id);

    struct stat st;
    stat(path, &st);

    IndexEntry e;
    e.mode = 100644;
    e.hash = id;
    e.size = size;
    e.mtime_sec = st.st_mtime;

    strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path) - 1] = '\0';

    idx->entries[idx->count++] = e;

    free(data);

    return index_save(idx);
}
