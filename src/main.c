#include "bee.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/limits.h>

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
    // TODO Symlinks handling
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

        return !bee_create_archive(target, filenames, filenames_len);
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

        return !bee_extract_archive(archive_path, dest);
    }

    if (strcmp(command, "list") == 0) {
        if (argc < 3) {
            print_usage(program_name);
            return 1;
        }

        return !bee_list_archive(argv[2]);
    } 

    fprintf(stderr, "ERROR: Unknown command: %s\n", command);
    print_usage(program_name);
    return 1;
}
