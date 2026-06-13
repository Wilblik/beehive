#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

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

static int extract_archive() {
    // TODO Extract archive
    fprintf(stderr, "ERROR: Extracting archive is not supported yet!\n");
    return 1;
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

    if (strcmp(command, "create") == 0) {
        if (argc < 4) {
            print_usage(program_name);
            return 1;
        }

        char** filenames = &argv[3];
        size_t filenames_len = argc - 3;

        char* final_target = NULL;
        char* og_target = argv[2];
        size_t og_target_len = strlen(og_target);

        if (og_target_len < 4 || strcmp(og_target + og_target_len - 4, ".bee") != 0) {
            final_target = malloc(og_target_len + 5);
            if (final_target != NULL) {
                snprintf(final_target, og_target_len + 5, "%s.bee", og_target);
            }
        } else {
            final_target = strdup(og_target);
        }

        if (final_target == NULL) {
            fprintf(stderr, "ERROR: Buy more RAM!");
            return 1;
        }

        int result = create_archive(final_target, filenames, filenames_len);

        free(final_target);
        return result;
    }

    if (strcmp(command, "extract") == 0) {
        return extract_archive();
    }

    if (strcmp(command, "list") == 0) {
        return list_archive();
    } 

    fprintf(stderr, "ERROR: Unknown command: %s\n", command);
    print_usage(program_name);
    return 1;
}
