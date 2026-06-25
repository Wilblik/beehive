#include "bee.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/limits.h>

typedef struct {
    char magic[8];
    uint32_t entry_count;
} ArchiveHeader;

typedef struct {
    uint32_t name_len;
    uint64_t size;
    uint8_t type;
} EntryHeader;

typedef enum {
    ENTRY_TYPE_FILE = 0,
    ENTRY_TYPE_DIR = 1,
} EntryType;

static int add_entry_to_archive(FILE *archive_fp, const char *archive_abs_path,
                                const char *entry_abs_path,
                                const char *entry_rel_path,
                                uint32_t *entry_count);

bool bee_create_archive(const char *target, char *const filenames[], size_t filenames_len) {
    FILE *archive_fp = fopen(target, "wb");
    if (archive_fp == NULL) {
        fprintf(stderr, "ERROR: Could not open archive '%s' for writing. Reason: %s\n", target, strerror(errno));
        return false;
    }

    char target_abs_path_buff[PATH_MAX];
    if (realpath(target, target_abs_path_buff) == NULL) {
        fprintf(stderr, "ERROR: Could not get absolute path for '%s'. Reason: %s\n", target, strerror(errno));
        fclose(archive_fp);
        return false;
    }

    ArchiveHeader archive_header;
    strncpy(archive_header.magic, "BEEHIVE", sizeof(archive_header.magic));

    if (fwrite(&archive_header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not write archive header\n");
        fclose(archive_fp);
        return false;
    }

    uint32_t entry_count = 0;

    for (size_t i = 0; i < filenames_len; ++i) {
        char file_abs_path_buff[PATH_MAX];
        if (realpath(filenames[i], file_abs_path_buff) == NULL) {
            fprintf(stderr, "ERROR: Could not get absolute path for %s. Reason: %s\n", filenames[i], strerror(errno));
            fclose(archive_fp);
            return false;
        }

        const char *last_slash = strrchr(file_abs_path_buff, '/');
        const char *rel_path = last_slash != NULL ? last_slash + 1 : file_abs_path_buff;

        bool success = add_entry_to_archive(archive_fp, target_abs_path_buff,
                                            file_abs_path_buff, rel_path, &entry_count);

        if (!success) {
            fclose(archive_fp);
            return false;
        }
    }

    archive_header.entry_count = entry_count;

    if (fseek(archive_fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Could not seek to the start of file. Reason: %s\n", strerror(errno));
        fclose(archive_fp);
        return false;
    }

    if (fwrite(&archive_header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not update archive header\n");
        fclose(archive_fp);
        return false;
    }

    fclose(archive_fp);
    return true;
}

static bool read_archive_header(FILE *archive_fp, ArchiveHeader *header);
static char *read_entry_header(FILE *archive_fp, EntryHeader *header);
static bool is_safe_path(const char *path);
static bool mkdir_p(const char *path);

bool bee_extract_archive(const char *archive_path, const char *dest) {
    FILE *archive_fp = fopen(archive_path, "rb");
    if (archive_fp == NULL) {
        fprintf(stderr, "ERROR: Could not open archive '%s'. Reason: %s\n", archive_path, strerror(errno));
        return false;
    }

    ArchiveHeader archive_header;
    if (!read_archive_header(archive_fp, &archive_header)) {
        fclose(archive_fp);
        return false;
    }

    char dest_abs_path[PATH_MAX];
    if (realpath(dest, dest_abs_path) == NULL) {
        fprintf(stderr, "ERROR: Could not get absolute path for %s. Reason: %s\n", dest, strerror(errno));
        fclose(archive_fp);
        return false;
    }

    for (uint32_t i = 0; i < archive_header.entry_count; ++i) {
        EntryHeader entry_header;
        char *entry_name = read_entry_header(archive_fp, &entry_header);
        if (entry_name == NULL) {
            fclose(archive_fp);
            return false;
        }

        if (!is_safe_path(entry_name)) {
            fprintf(stderr, "ERROR: filepath '%s' is not safe!. Archive was tempered with!\n", entry_name);
            free(entry_name);
            fclose(archive_fp);
            return false;
        }

        if (entry_header.type == ENTRY_TYPE_DIR) {
            char dir_full_path[PATH_MAX];
            snprintf(dir_full_path, sizeof(dir_full_path), "%s/%s", dest_abs_path, entry_name);
            if (!mkdir_p(dir_full_path)) {
                free(entry_name);
                fclose(archive_fp);
                return false;
            }
        } else if (entry_header.type == ENTRY_TYPE_FILE) {
            char file_full_path[PATH_MAX];
            snprintf(file_full_path, sizeof(file_full_path), "%s/%s", dest_abs_path, entry_name);

            FILE *fp = fopen(file_full_path, "wb");
            if (fp == NULL) {
                fprintf(stderr, "ERROR: Could not open file '%s' for writing. Reason: %s\n", file_full_path, strerror(errno));
                free(entry_name);
                fclose(archive_fp);
                return false;
            }

            char buffer[4096];
            size_t total_bytes_read = 0;

            while (entry_header.size > total_bytes_read) {
                size_t bytes_to_read = entry_header.size - total_bytes_read;
                if (bytes_to_read > sizeof(buffer)) {
                    bytes_to_read = sizeof(buffer);
                }

                size_t bytes_read = fread(buffer, 1, bytes_to_read, archive_fp);
                if (bytes_read == 0) {
                    if (ferror(archive_fp)) {
                        fprintf(stderr, "ERROR: Could not read file from the archive\n");
                        free(entry_name);
                        fclose(fp);
                        fclose(archive_fp);
                        return false;
                    }
                    break;
                }

                if (fwrite(buffer, 1, bytes_read, fp) != bytes_read) {
                    fprintf(stderr, "ERROR: Could not write file data for %s\n", file_full_path);
                    free(entry_name);
                    fclose(fp);
                    fclose(archive_fp);
                    return false;
                }

                total_bytes_read += bytes_read;
            }

            fclose(fp);
        } else {
            fprintf(stderr, "ERROR: Entry type unsupported\n");
            free(entry_name);
            fclose(archive_fp);
            return false;
        }

        free(entry_name);
    }

    fclose(archive_fp);
    return true;
}

bool bee_list_archive(const char *archive_path) {
    FILE *archive_fp = fopen(archive_path, "rb");
    if (archive_fp == NULL) {
        fprintf(stderr, "ERROR: Could not open archive '%s'. Reason: %s\n", archive_path, strerror(errno));
        return false;
    }

    ArchiveHeader archive_header;
    if (!read_archive_header(archive_fp, &archive_header)) {
        fclose(archive_fp);
        return false;
    }

    for (uint32_t i = 0; i < archive_header.entry_count; ++i) {
        EntryHeader entry_header;
        char *entry_name = read_entry_header(archive_fp, &entry_header);
        if (entry_name == NULL) {
            fclose(archive_fp);
            return false;
        }

        if (entry_header.type == ENTRY_TYPE_FILE) {
            if (fseek(archive_fp, entry_header.size, SEEK_CUR) != 0) {
                fprintf(stderr, "ERROR: Could not seek to the end of file data. Reason: %s\n", strerror(errno));
                fclose(archive_fp);
                free(entry_name);
                return false;
            }
        }

        int level = 0;
        const char *p = entry_name;
        while ((p = strchr(p, '/')) != NULL) {
            level++;
            p++;
        }

        const char *base_name = strrchr(entry_name, '/');
        base_name = (base_name != NULL) ? base_name + 1 : entry_name;
        printf("%*s%s\n", level * 4, "", base_name);

        free(entry_name);
    }

    fclose(archive_fp);
    return true;
}

static int add_entry_to_archive(FILE *archive_fp, const char *archive_abs_path,
                                const char *entry_abs_path,
                                const char *entry_rel_path,
                                uint32_t *entry_count) {

    /* Skip the archive itself */
    if (strcmp(archive_abs_path, entry_abs_path) == 0) {
        return true;
    }

    struct stat st;
    if (stat(entry_abs_path, &st) != 0) {
        fprintf(stderr, "ERROR: Could not stat file: '%s'. Reason: %s", entry_abs_path, strerror(errno));
        return false;
    }

    EntryHeader entry_header = {
        .name_len = (uint32_t)strlen(entry_rel_path),
        .size = (uint64_t)st.st_size,
        .type = S_ISDIR(st.st_mode) ? 1 : 0,
    };

    if (fwrite(&entry_header, sizeof(entry_header), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not write file header for %s\n", entry_abs_path);
        return false;
    }

    if (fwrite(entry_rel_path, 1, entry_header.name_len, archive_fp) != entry_header.name_len) {
        fprintf(stderr, "ERROR: Could not write filename for %s\n", entry_abs_path);
        return false;
    }

    (*entry_count)++;

    if (entry_header.type == ENTRY_TYPE_FILE) {
        FILE *fp = fopen(entry_abs_path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "ERROR: Could not open %s. Reason: %s\n", entry_abs_path, strerror(errno));
            return false;
        }

        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            if (fwrite(buffer, 1, bytes_read, archive_fp) != bytes_read) {
                fprintf(stderr, "ERROR: Could not write file data for %s\n", entry_abs_path);
                fclose(fp);
                return false;
            }
        }

        fclose(fp);
        return true;
    }

    if (entry_header.type == ENTRY_TYPE_DIR) {
        DIR *dir = opendir(entry_abs_path);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Could not open %s. Reason: %s\n", entry_abs_path, strerror(errno));
            return false;
        }

        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char dir_entry_abs_path[PATH_MAX];
            char dir_entry_rel_path[PATH_MAX];
            snprintf(dir_entry_abs_path, sizeof(dir_entry_abs_path), "%s/%s", entry_abs_path, entry->d_name);
            snprintf(dir_entry_rel_path, sizeof(dir_entry_rel_path), "%s/%s", entry_rel_path, entry->d_name);

            bool success = add_entry_to_archive(archive_fp, archive_abs_path, dir_entry_abs_path,
                                                dir_entry_rel_path, entry_count);

            if (!success) {
                closedir(dir);
                return false;
            }
        }

        closedir(dir);
        return true;
    }

    fprintf(stderr, "ERROR: Entry type unsupported\n");
    return false;
}

static bool read_archive_header(FILE *archive_fp, ArchiveHeader *header) {
    if (fread(header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not read archive header\n");
        return false;
    }

    if (memcmp(header->magic, "BEEHIVE", 8) != 0) {
        fprintf(stderr, "ERROR: Not a valid beehive archive\n");
        return false;
    }

    return true;
}

static char *read_entry_header(FILE *archive_fp, EntryHeader *header) {
    if (fread(header, sizeof(EntryHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not read entry header from the archive\n");
        return NULL;
    }

    char *filename = malloc(header->name_len + 1);
    if (filename == NULL) {
        fprintf(stderr, "ERROR: Could not allocate memory. Buy more RAM!\n");
        return NULL;
    }

    if (fread(filename, sizeof(char), header->name_len, archive_fp) != header->name_len) {
        fprintf(stderr, "ERROR: Could not read entry name from the archive\n");
        return NULL;
    }

    filename[header->name_len] = '\0';
    return filename;
}

static bool is_safe_path(const char *path) {
    if (path[0] == '\0' || path[0] == '/') {
        return false;
    }

    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '.' && path[i+1] == '.') {
            if (i == 0 || path[i-1] == '/' || path[i+2] == '\0' || path[i+2] == '/') {
                return false;
            }
        }
    }
    return true;
}

static bool mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);

    char *p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "ERROR: Could not create directory '%s'. Reason: %s\n", tmp, strerror(errno));
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "ERROR: Could not create directory '%s'. Reason: %s\n", tmp, strerror(errno));
        return false;
    }

    return true;
}
