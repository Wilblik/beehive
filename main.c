#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

typedef struct {
    char magic[8];
    uint32_t file_count;
} ArchiveHeader;

typedef struct {
    uint32_t filename_len;
    uint64_t file_size;
} FileHeader;

static void print_usage(const char* program_name) {
    printf(
        "beehive <command> [args]\n\n"
        "Commands:\n"
        "  create <archive_name> <file1> [file2] ...  Create a new archive from provided files.\n"
        "  extract <archive_name> [destination]       Extract files from an archive.\n"
        "  list <archive_name>                        List files stored in the archive.\n"
        "  -h | --help                                Show this help message.\n\n");
}

static int create_archive(const char* target, char* const filenames[], size_t filenames_len) {

    FILE* fp = fopen(target, "wb");
    if (fp == NULL) {
        perror("ERROR: Could not open archive file for writing");
        return 1;
    }

    ArchiveHeader archive_header = { .file_count = (uint32_t)filenames_len };
    strncpy(archive_header.magic, "BEEHIVE", sizeof(archive_header.magic));

    if (fwrite(&archive_header, sizeof(ArchiveHeader), 1, fp) != 1) {
        fprintf(stderr, "ERROR: Could not write archive header\n");
        fclose(fp);
        return 1;
    }

    for (size_t i = 0; i < filenames_len; ++i) {
        const char* filename = filenames[i];
        struct stat st;
        if (stat(filename, &st) != 0) {
            fprintf(stderr, "ERROR: Could not stat file: '%s'. Reason: %s", filename, strerror(errno));
            fclose(fp);
            return 1;
        }

        FileHeader file_header = {
            .filename_len = (uint32_t)strlen(filename),
            .file_size = (uint64_t)st.st_size
        };

        if (fwrite(&file_header, sizeof(FileHeader), 1, fp) != 1) {
            fprintf(stderr, "ERROR: Could not write file header for %s\n", filename);
            fclose(fp);
            return 1;
        }

        if (fwrite(filename, 1, file_header.filename_len, fp) != file_header.filename_len) {
            fprintf(stderr, "ERROR: Could not write filename for %s\n", filename);
            fclose(fp);
            return 1;
        }

        FILE* src_fp = fopen(filename, "rb");
        if (src_fp == NULL) {
            perror("ERROR: Could not open source file");
            fclose(fp);
            return 1;
        }

        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
            if (fwrite(buffer, 1, bytes_read, fp) != bytes_read) {
                fprintf(stderr, "ERROR: Could not write file data for %s\n", filename);
                fclose(src_fp);
                fclose(fp);
                return 1;
            }
        }

        fclose(src_fp);
    }

    fclose(fp);
    return 0;
}

static int extract_archive(const char* archive_path, const char* dest) {
    FILE* archive = fopen(archive_path, "rb");
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
        char* filename = NULL;
        char* file_content = NULL;
        FILE* fp = NULL;
        int result = 0;

        FileHeader file_header;
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
            goto cleanup;
        }
        filename[file_header.filename_len] = '\0';

        file_content = malloc(file_header.file_size);
        if (file_content == NULL) {
            fprintf(stderr, "ERROR: Buy more RAM!\n");
            result = 1;
            goto cleanup;
        }

        if (fread(file_content, sizeof(char), file_header.file_size, archive) != file_header.file_size) {
            fprintf(stderr, "ERROR: Could not read file header from the archive\n");
            result = 1;
            goto cleanup;
        }

        // TODO Without proper directories support this will fail on nested paths
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dest, filename);

        fp = fopen(full_path, "wb");
        if (fp == NULL) {
           perror("ERROR: Could not open file for writing");
           result = 1;
           goto cleanup;
        }

        if (fwrite(file_content, sizeof(char), file_header.file_size, fp) != file_header.file_size) {
            fprintf(stderr, "ERROR: Could not open file for writing\n");
            result = 1;
            goto cleanup;
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

static int list_archive() {
    // TODO List archive
    fprintf(stderr, "ERROR: Listing archive is not supported yet!\n");
    return 1;
}

int main(int argc, char* argv[]) {
    char* program_name = argv[0];
    if (argc < 2) {
        print_usage(program_name);
        return 1;
    }

    char* command = argv[1];

    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage(program_name);
        return 0;
    }


    // TODO Add support for passing directory as a source file
    // TODO Save permissions
    if (strcmp(command, "create") == 0) {
        if (argc < 4) {
            print_usage(program_name);
            return 1;
        }

        char** filenames = &argv[3];
        size_t filenames_len = argc - 3;

        char* target = argv[2];
        size_t target_len = strlen(target);
        char target_buff[PATH_MAX];

        if (target_len < 4 || strcmp(target + target_len - 4, ".bee") != 0) {
            snprintf(target_buff, sizeof(target_buff), "%s.bee", target);
            target = target_buff;
        } 

        int result = create_archive(target, filenames, filenames_len);

        return result;
    }

    if (strcmp(command, "extract") == 0) {
        if (argc < 3) {
            print_usage(program_name);
            return 1;
        }

        char* archive_path = argv[2];
        char* dest = NULL;
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
        return list_archive();
    } 

    fprintf(stderr, "ERROR: Unknown command: %s\n", command);
    print_usage(program_name);
    return 1;
}
