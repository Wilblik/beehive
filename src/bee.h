#ifndef BEE_H_
#define BEE_H_

#include <stdlib.h>
#include <stdbool.h>

bool bee_create_archive(const char *target, char *const filenames[], size_t filenames_len);
bool bee_extract_archive(const char *archive_path, const char *dest);
bool bee_list_archive(const char *archive_path);

#endif // BEE_H_
