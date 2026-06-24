#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/limits.h>

typedef struct {
    char magic[8];
    uint32_t file_count;
} ArchiveHeader;

typedef struct {
    uint32_t filename_len;
    uint64_t file_size;
    uint8_t type;
} FileHeader;

typedef enum {
    ENTRY_TYPE_FILE = 0,
    ENTRY_TYPE_DIR = 1,
} EntryType;

static void print_usage(const char *program_name) {
    printf(
        "%s <command> [args]\n\n"
        "Commands:\n"
        "  create <archive_name> <file1> [file2] ...  Create a new archive from provided files.\n"
        "  extract <archive_name> [destination]       Extract files from an archive.\n"
        "  list <archive_name>                        List files stored in the archive.\n"
        "  -h | --help                                Show this help message.\n\n"
        ,program_name);
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);

    char *p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "ERROR: Could not create directory %s. Reason: %s\n", tmp, strerror(errno));
                return 1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "ERROR: Could not create directory %s. Reason: %s\n", tmp, strerror(errno));
        return 1;
    }

    return 0;
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

static int add_entry_to_archive(FILE *archive_fp, const char *archive_abs_path, const char *entry_abs_path, const char *entry_rel_path, uint32_t *file_count) {
    /* Skip the archive itself */
    if (strcmp(archive_abs_path, entry_abs_path) == 0) {
        return 0;
    }

    struct stat st;
    if (stat(entry_abs_path, &st) != 0) {
        fprintf(stderr, "ERROR: Could not stat file: '%s'. Reason: %s", entry_abs_path, strerror(errno));
        return 1;
    }

    FileHeader file_header = {
        .filename_len = (uint32_t)strlen(entry_rel_path),
        .file_size = (uint64_t)st.st_size,
        .type = S_ISDIR(st.st_mode) ? 1 : 0,
    };

    if (fwrite(&file_header, sizeof(FileHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not write file header for %s\n", entry_abs_path);
        return 1;
    }

    if (fwrite(entry_rel_path, 1, file_header.filename_len, archive_fp) != file_header.filename_len) {
        fprintf(stderr, "ERROR: Could not write filename for %s\n", entry_abs_path);
        return 1;
    }

    (*file_count)++;

    if (file_header.type == ENTRY_TYPE_FILE) {
        FILE *fp = fopen(entry_abs_path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "ERROR: Could not open %s. Reason: %s\n", entry_abs_path, strerror(errno));
            return 1;
        }

        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            if (fwrite(buffer, 1, bytes_read, archive_fp) != bytes_read) {
                fprintf(stderr, "ERROR: Could not write file data for %s\n", entry_abs_path);
                fclose(fp);
                return 1;
            }
        }

        fclose(fp);
        return 0;
    } else if (file_header.type == ENTRY_TYPE_DIR) {
        int result = 0;

        DIR *dir = opendir(entry_abs_path);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Could not open %s. Reason: %s\n", entry_abs_path, strerror(errno));
            goto cleanup;
        }

        char dir_entry_abs_path[PATH_MAX];
        char dir_entry_rel_path[PATH_MAX];
        struct dirent *entry = NULL;

        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            snprintf(dir_entry_abs_path, sizeof(dir_entry_abs_path), "%s/%s", entry_abs_path, entry->d_name);
            snprintf(dir_entry_rel_path, sizeof(dir_entry_rel_path), "%s/%s", entry_rel_path, entry->d_name);

            result = add_entry_to_archive(archive_fp, archive_abs_path, dir_entry_abs_path, dir_entry_rel_path, file_count);
            if (result != 0) goto cleanup;
        }
    cleanup:
        closedir(dir);
        return result;
    } 

    fprintf(stderr, "ERROR: Entry type unsupported\n");
    return 1;
}

static int create_archive(const char *target, char *const filenames[], size_t filenames_len) {
    FILE *archive_fp = fopen(target, "wb");
    if (archive_fp == NULL) {
        fprintf(stderr, "ERROR: Could not open '%s' for writing. Reason: %s\n", target, strerror(errno));
        return 1;
    }

    int result = 0;
    uint32_t file_count = 0;
    char target_abs_path_buff[PATH_MAX];
    char file_abs_path_buff[PATH_MAX];

    if (realpath(target, target_abs_path_buff) == NULL) {
        fprintf(stderr, "ERROR: Could not get absolute path for '%s'. Reason: %s\n", target, strerror(errno));
        result = 1;
        goto cleanup;
    }

    ArchiveHeader archive_header;
    strncpy(archive_header.magic, "BEEHIVE", sizeof(archive_header.magic));

    if (fwrite(&archive_header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not write archive header\n");
        result = 1;
        goto cleanup;
    }

    for (size_t i = 0; i < filenames_len; ++i) {
        if (realpath(filenames[i], file_abs_path_buff) == NULL) {
            fprintf(stderr, "ERROR: Could not get absolute path for %s. Reason: %s\n", filenames[i], strerror(errno));
            result = 1;
            goto cleanup;
        }

        const char *last_slash = strrchr(file_abs_path_buff, '/');
        const char *rel_path = last_slash != NULL ? last_slash + 1 : file_abs_path_buff;

        result = add_entry_to_archive(archive_fp, target_abs_path_buff, file_abs_path_buff, rel_path, &file_count);
        if (result != 0) {
            goto cleanup;
        }
    }

    archive_header.file_count = file_count;
    if (fseek(archive_fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Could not seek to start of file: %s\n", strerror(errno));
        result = 1;
        goto cleanup;
    }

    if (fwrite(&archive_header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not update archive header\n");
        result = 1;
        goto cleanup;
    }

cleanup:
    fclose(archive_fp);
    return result;
}

static int extract_archive(const char *archive_path, const char *dest) {
    FILE *archive = fopen(archive_path, "rb");
    if (archive == NULL) {
        perror("ERROR: Could not open archive file");
        return 1;
    }

    ArchiveHeader header;
    if (fread(&header, sizeof(ArchiveHeader), 1, archive) != 1) {
        fprintf(stderr, "ERROR: Archive is corrupted\n");
        fclose(archive);
        return 1;
    }

    if (memcmp(header.magic, "BEEHIVE", 8) != 0) {
        fprintf(stderr, "ERROR: Not a valid beehive archive\n");
        fclose(archive);
        return 1;
    }

    uint32_t file_count = header.file_count;
    for (uint32_t i = 0; i < file_count; ++i) {
        int result = 0;
        FileHeader file_header;
        char *filename = NULL;
        char *file_content = NULL;
        FILE *fp = NULL;

        if (fread(&file_header, sizeof(FileHeader), 1, archive) != 1) {
            fprintf(stderr, "ERROR: Could not read file header from the archive\n");
            result = 1;
            goto cleanup;
        }

        filename = malloc(file_header.filename_len + 1);
        if (filename == NULL) {
            fprintf(stderr, "ERROR: Buy more RAM!\n");
            goto cleanup;
        }

        if (fread(filename, sizeof(char), file_header.filename_len, archive) != file_header.filename_len) {
            fprintf(stderr, "ERROR: Could not read filename from the archive\n");
            result = 1;
            goto cleanup;
        }

        filename[file_header.filename_len] = '\0';

        if (!is_safe_path(filename)) {
            fprintf(stderr, "ERROR: filepath '%s' is not safe!. Archive was tempered with!\n", filename);
            result = 1;
            goto cleanup;
        }

        if (file_header.type == ENTRY_TYPE_DIR) {
            char dir_full_path[PATH_MAX];
            snprintf(dir_full_path, sizeof(dir_full_path), "%s/%s", dest, filename);
            if (mkdir_p(dir_full_path) != 0) {
                result = 1;
                goto cleanup;
            }
        } else if (file_header.type == ENTRY_TYPE_FILE) {
           file_content = malloc(file_header.file_size);
           if (file_content == NULL) {
               fprintf(stderr, "ERROR: Buy more RAM!\n");
               result = 1;
               goto cleanup;
           }

           char file_full_path[PATH_MAX];
           snprintf(file_full_path, sizeof(file_full_path), "%s/%s", dest, filename);

           fp = fopen(file_full_path, "wb");
           if (fp == NULL) {
               fprintf(stderr, "ERROR: Could not open file '%s' for writing. Reason: %s\n", file_full_path, strerror(errno));
               result = 1;
               goto cleanup;
           }

           if (fread(file_content, sizeof(char), file_header.file_size, archive) !=
               file_header.file_size) {
               fprintf(stderr, "ERROR: Could not read file content from the archive\n");
               result = 1;
               goto cleanup;
           }

           if (fwrite(file_content, sizeof(char), file_header.file_size, fp) != file_header.file_size) {
               fprintf(stderr, "ERROR: Could not open file for writing\n");
               result = 1;
               goto cleanup;
           }
        }
    cleanup:
        free(filename);
        free(file_content);
        if (fp) fclose(fp);
        if (result != 0) {
            fclose(archive);
            return result;
        }
    }

    fclose(archive);
    return 0;
}

static bool read_archive_header(FILE *archive_fp, ArchiveHeader *header) {
    if (fread(header, sizeof(ArchiveHeader), 1, archive_fp) != 1) {
        return false;
    }

    if (memcmp(header->magic, "BEEHIVE", 8) != 0) {
        fprintf(stderr, "ERROR: Not a valid beehive archive\n");
        return false;
    }

    return true;
}

static char *read_file_header(FILE *archive_fp, FileHeader *header) {
    if (fread(header, sizeof(FileHeader), 1, archive_fp) != 1) {
        fprintf(stderr, "ERROR: Could not read file header from the archive\n");
        return NULL;
    }

    char *filename = malloc(header->filename_len + 1);
    if (filename == NULL) {
        fprintf(stderr, "ERROR: Buy more RAM!\n");
        return NULL;
    }

    if (fread(filename, sizeof(char), header->filename_len, archive_fp) != header->filename_len) {
        fprintf(stderr, "ERROR: Could not read filename from the archive\n");
        return NULL;
    }

    filename[header->filename_len] = '\0';
    return filename;
}

static int list_archive(const char *archive_path) {
    FILE *archive_fp = fopen(archive_path, "rb");
    if (archive_fp == NULL) {
        fprintf(stderr, "ERROR: Could not open archive '%s'. Reason: %s\n", archive_path, strerror(errno));
        return 1;
    }

    ArchiveHeader archive_header;
    if (!read_archive_header(archive_fp, &archive_header)) {
        fprintf(stderr, "ERROR: Archive is corrupted\n");
        fclose(archive_fp);
        return 1;
    }

    for (uint32_t i = 0; i < archive_header.file_count; ++i) {
        FileHeader file_header;
        char *filename = read_file_header(archive_fp, &file_header);
        if (filename == NULL) {
            fclose(archive_fp);
            return 1;
        }

        if (file_header.type == ENTRY_TYPE_FILE) {
            if (fseek(archive_fp, file_header.file_size, SEEK_CUR) != 0) {
                fprintf(stderr, "ERROR: Could not seek to the end of file data. Reason: %s\n", strerror(errno));
                fclose(archive_fp);
                free(filename);
                return 1;
            }
        }

        int level = 0;
        const char *p = filename;
        while ((p = strchr(p, '/')) != NULL) {
            level++;
            p++;
        }

        const char *base_name = strrchr(filename, '/');
        base_name = (base_name != NULL) ? base_name + 1 : filename;
        printf("%*s%s\n", level * 4, "", base_name);

        free(filename);
    }

    fclose(archive_fp);
    return 0;
}

int main(int argc, char *argv[]) {
    char *program_name = argv[0];
    if (argc < 2) {
        print_usage(program_name);
        return 1;
    }

    char *command = argv[1];

    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage(program_name);
        return 0;
    }

    // TODO Save permissions
    if (strcmp(command, "create") == 0) {
        if (argc < 4) {
            print_usage(program_name);
            return 1;
        }

        char **filenames = &argv[3];
        size_t filenames_len = argc - 3;

        char *target = argv[2];
        size_t target_len = strlen(target);
        char target_buff[PATH_MAX];

        if (target_len < 4 || strcmp(target + target_len - 4, ".bee") != 0) {
            snprintf(target_buff, sizeof(target_buff), "%s.bee", target);
            target = target_buff;
        } 

        return create_archive(target, filenames, filenames_len);
    }

    if (strcmp(command, "extract") == 0) {
        if (argc < 3) {
            print_usage(program_name);
            return 1;
        }

        char *archive_path = argv[2];
        char *dest = NULL;
        char cwd_buff[PATH_MAX];

        if (argc >= 4 && *argv[3] != '\0') {
            dest = argv[3];
        } else {
            if (getcwd(cwd_buff, sizeof(cwd_buff)) != NULL) {
                dest = cwd_buff;
            } else {
                perror("ERROR: Could not get current working directory");
                return 1;
            }
        }

        return extract_archive(archive_path, dest);
    }

    if (strcmp(command, "list") == 0) {
        if (argc < 3) {
            print_usage(program_name);
            return 1;
        }

        return list_archive(argv[2]);
    } 

    fprintf(stderr, "ERROR: Unknown command: %s\n", command);
    print_usage(program_name);
    return 1;
}
