#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"

enum {
    PATH_CAPACITY = 1024,
    PACK_LIMIT = 0x3800
};

static const char PACK_PATHS[] = ECHO_LANGUAGE_PACK_PATHS;

static uint32_t read_pack(const char *path, uint8_t *pack)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t size = fread(pack, 1, PACK_LIMIT + 1, file);
    fclose(file);
    return (uint32_t)size;
}

static void the_english_pack_is_the_only_pack_and_attaches(void)
{
    static uint8_t pack[PACK_LIMIT + 1];
    CHECK(strchr(PACK_PATHS, '|') == NULL);
    CHECK(strstr(PACK_PATHS, "en.bin") != NULL);
    uint32_t size = read_pack(PACK_PATHS, pack);
    CHECK(size > 0 && size <= PACK_LIMIT);
    CHECK(i18n_attach(pack, size));
    CHECK(strcmp("Settings", i18n_text(STR_MENU_SETTINGS)) == 0);
    i18n_detach();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_english_pack_is_the_only_pack_and_attaches),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
