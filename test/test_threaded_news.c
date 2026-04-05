/*
 * test_threaded_news.c - Unit tests for threaded news YAML parser
 *
 * Tests tn_load() against the Mobius Go server YAML format and
 * verifies save/load round-tripping.
 */

#include "mobius/threaded_news_yaml.h"

#include <yaml.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-60s ", #name); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

/* Helper: write a string to a temp file, load it, return the news object */
static mobius_threaded_news_t *load_yaml_string(const char *yaml)
{
    char tmppath[] = "/tmp/test_tn_XXXXXX";
    int fd = mkstemp(tmppath);
    assert(fd >= 0);
    write(fd, yaml, strlen(yaml));
    close(fd);

    mobius_threaded_news_t *tn = mobius_threaded_news_new(NULL);
    assert(tn != NULL);
    strncpy(tn->file_path, tmppath, sizeof(tn->file_path) - 1);

    int rc = tn_load(tn);
    assert(rc == 0);

    unlink(tmppath);
    return tn;
}

/* ===== Basic parsing tests ===== */

static void test_empty_categories(void)
{
    mobius_threaded_news_t *tn = load_yaml_string("Categories: {}\n");
    assert(tn->category_count == 0);
    mobius_threaded_news_free(tn);
}

static void test_single_category_no_articles(void)
{
    const char *yaml =
        "Categories:\n"
        "  General:\n"
        "    Name: General\n"
        "    Type:\n"
        "    - 0\n"
        "    - 3\n"
        "    Articles: {}\n"
        "    SubCats: {}\n";

    mobius_threaded_news_t *tn = load_yaml_string(yaml);
    assert(tn->category_count == 1);
    assert(strcmp(tn->categories[0].name, "General") == 0);
    assert(tn->categories[0].type[0] == 0);
    assert(tn->categories[0].type[1] == 3);
    assert(tn->categories[0].article_count == 0);
    mobius_threaded_news_free(tn);
}

/* ===== Mobius Go format parsing ===== */

/*
 * Exact YAML from the Apple Media Archive VPS (Mobius Go output).
 * Key format traits:
 *   - 4-space indentation (Go yaml.v2 default)
 *   - Flow-style sequences for Type, Date, PrevArt, etc: [0, 3]
 *   - Block scalar (|-) for multi-line Data with blank lines
 *   - Quoted string with \n escapes for Data
 *   - UTF-8 smart quotes in article 3
 *   - No DataFlav field (newer Mobius, yaml:"-")
 */
static const char *MOBIUS_GO_YAML =
    "Categories:\n"
    "    General Discussion:\n"
    "        Type: [0, 3]\n"
    "        Name: General Discussion\n"
    "        Articles:\n"
    "            1:\n"
    "                Title: Success!\n"
    "                Poster: dmg\n"
    "                Date: [7, 234, 0, 0, 0, 89, 232, 198]\n"
    "                PrevArt: [0, 0, 0, 0]\n"
    "                NextArt: [0, 0, 0, 2]\n"
    "                ParentArt: [0, 0, 0, 0]\n"
    "                FirstChildArtArt: [0, 0, 0, 2]\n"
    "                Data: Hey everyone, my server now has TLS support and a News section!\n"
    "            2:\n"
    "                Title: This is Cool\n"
    "                Poster: guest\n"
    "                Date: [7, 234, 0, 0, 0, 93, 91, 158]\n"
    "                PrevArt: [0, 0, 0, 1]\n"
    "                NextArt: [0, 0, 0, 3]\n"
    "                ParentArt: [0, 0, 0, 1]\n"
    "                FirstChildArtArt: [0, 0, 0, 3]\n"
    "                Data: |-\n"
    "                    Loved Hotline from back in the day.\n"
    "\n"
    "                    It would be good if you could integrate the server and client in one, even trackers maybe.\n"
    "            3:\n"
    "                Title: Server + client?\n"
    "                Poster: dmg - dev\n"
    "                Date: [7, 234, 0, 0, 0, 97, 85, 57]\n"
    "                PrevArt: [0, 0, 0, 2]\n"
    "                NextArt: [0, 0, 0, 4]\n"
    "                ParentArt: [0, 0, 0, 2]\n"
    "                FirstChildArtArt: [0, 0, 0, 0]\n"
    "                Data: \"That is an interesting idea but right now I\xe2\x80\x99m focused on providing a client for every single OS and new features. \\n\\nI\xe2\x80\x99" "d be more likely to write a GUI wrapper for Mobius or one of the other servers as a stand alone app.\"\n"
    "            4:\n"
    "                Title: Test out markdown\n"
    "                Poster: dmg - dev\n"
    "                Date: [7, 234, 0, 0, 0, 100, 47, 51]\n"
    "                PrevArt: [0, 0, 0, 3]\n"
    "                NextArt: [0, 0, 0, 0]\n"
    "                ParentArt: [0, 0, 0, 0]\n"
    "                FirstChildArtArt: [0, 0, 0, 0]\n"
    "                Data: |-\n"
    "                    # This is a title\n"
    "\n"
    "                    Now for a link\n"
    "\n"
    "                    [MobiusAdmin ](https://github.com/fuzzywalrus/mobius-macOS-GUI/releases)\n"
    "\n"
    "                    ![MobiusAdmin screenshot](https://hotlinenavigator.com/mobius-screenshot.png)\n"
    "        SubCats: {}\n";

static void test_mobius_go_format_categories(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    assert(tn->category_count == 1);
    assert(strcmp(tn->categories[0].name, "General Discussion") == 0);
    assert(tn->categories[0].type[0] == 0);
    assert(tn->categories[0].type[1] == 3);
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_article_count(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    assert(tn->categories[0].article_count == 4);
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_article_titles(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    /* Find articles by ID */
    int found[5] = {0};
    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        switch (art->id) {
        case 1:
            assert(strcmp(art->title, "Success!") == 0);
            found[1] = 1;
            break;
        case 2:
            assert(strcmp(art->title, "This is Cool") == 0);
            found[2] = 1;
            break;
        case 3:
            assert(strcmp(art->title, "Server + client?") == 0);
            found[3] = 1;
            break;
        case 4:
            assert(strcmp(art->title, "Test out markdown") == 0);
            found[4] = 1;
            break;
        }
    }
    assert(found[1] && found[2] && found[3] && found[4]);
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_article_posters(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 1)
            assert(strcmp(art->poster, "dmg") == 0);
        else if (art->id == 2)
            assert(strcmp(art->poster, "guest") == 0);
        else if (art->id == 3)
            assert(strcmp(art->poster, "dmg - dev") == 0);
    }
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_plain_data(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 1) {
            printf("\n    [DEBUG] art1 data_len=%u data=\"%.*s\" ",
                   art->data_len, (int)art->data_len, art->data);
            assert(art->data_len > 0);
            assert(strstr(art->data, "TLS support") != NULL);
            assert(strstr(art->data, "News section") != NULL);
            return;
        }
    }
    assert(0 && "article 1 not found");
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_block_scalar_data(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 2) {
            printf("\n    [DEBUG] art2 data_len=%u data=\"%.*s\" ",
                   art->data_len, (int)art->data_len, art->data);
            assert(art->data_len > 0);
            /* Block scalar with |- and blank line between paragraphs.
             * libyaml resolves to string with \n.
             * tn_load() converts \n to \r for Hotline wire format. */
            assert(strstr(art->data, "Loved Hotline") != NULL);
            assert(strstr(art->data, "integrate the server") != NULL);
            return;
        }
    }
    assert(0 && "article 2 not found");
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_quoted_string_data(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 3) {
            printf("\n    [DEBUG] art3 data_len=%u data=\"%.*s\" ",
                   art->data_len, (int)art->data_len, art->data);
            assert(art->data_len > 0);
            /* Quoted string with \n escapes and UTF-8 smart quotes */
            assert(strstr(art->data, "interesting idea") != NULL);
            assert(strstr(art->data, "GUI wrapper") != NULL);
            return;
        }
    }
    assert(0 && "article 3 not found");
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_second_block_scalar(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 4) {
            printf("\n    [DEBUG] art4 data_len=%u data=\"%.*s\" ",
                   art->data_len, (int)art->data_len, art->data);
            assert(art->data_len > 0);
            /* Block scalar with blank lines, markdown, URLs */
            assert(strstr(art->data, "This is a title") != NULL);
            assert(strstr(art->data, "MobiusAdmin") != NULL);
            return;
        }
    }
    assert(0 && "article 4 not found");
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_date_parsed(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 1) {
            /* Date: [7, 234, 0, 0, 0, 89, 232, 198] */
            assert(art->date[0] == 7);
            assert(art->date[1] == 234);
            assert(art->date[4] == 0);
            assert(art->date[5] == 89);
            return;
        }
    }
    assert(0 && "article 1 not found");
    mobius_threaded_news_free(tn);
}

static void test_mobius_go_format_parent_art(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_YAML);
    tn_category_t *cat = &tn->categories[0];

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->id == 1)
            assert(art->parent_id == 0);
        else if (art->id == 2)
            assert(art->parent_id == 1);  /* reply to article 1 */
        else if (art->id == 3)
            assert(art->parent_id == 2);  /* reply to article 2 */
        else if (art->id == 4)
            assert(art->parent_id == 0);  /* root article */
    }
    mobius_threaded_news_free(tn);
}

/* ===== Mobius Go format WITH DataFlav (older Mobius versions) ===== */

static const char *MOBIUS_GO_WITH_DATAFLAV =
    "Categories:\n"
    "  TestCat:\n"
    "    Type:\n"
    "    - 0\n"
    "    - 3\n"
    "    Name: TestCat\n"
    "    Articles:\n"
    "      1:\n"
    "        Title: TestArt\n"
    "        Poster: Halcyon 1.9.2\n"
    "        Date:\n"
    "        - 7\n"
    "        - 228\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        - 254\n"
    "        - 252\n"
    "        - 204\n"
    "        PrevArt:\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        NextArt:\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        - 2\n"
    "        ParentArt:\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        FirstChildArtArt:\n"
    "        - 0\n"
    "        - 0\n"
    "        - 0\n"
    "        - 2\n"
    "        DataFlav:\n"
    "        - 116\n"
    "        - 101\n"
    "        - 120\n"
    "        - 116\n"
    "        - 47\n"
    "        - 112\n"
    "        - 108\n"
    "        - 97\n"
    "        - 105\n"
    "        - 110\n"
    "        Data: TestArt Body\n"
    "    SubCats: {}\n";

static void test_mobius_go_with_dataflav(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(MOBIUS_GO_WITH_DATAFLAV);
    assert(tn->category_count == 1);
    tn_category_t *cat = &tn->categories[0];
    assert(cat->article_count == 1);

    tn_article_t *art = &cat->articles[0];
    printf("\n    [DEBUG] art data_len=%u data=\"%.*s\" ",
           art->data_len, (int)art->data_len, art->data);
    assert(art->id == 1);
    assert(strcmp(art->title, "TestArt") == 0);
    assert(strcmp(art->poster, "Halcyon 1.9.2") == 0);
    assert(art->data_len > 0);
    assert(strcmp(art->data, "TestArt Body") == 0);
    mobius_threaded_news_free(tn);
}

/* ===== Invalid UTF-8 resilience ===== */

/*
 * If the YAML file has mojibake (e.g. raw \x80\x99 bytes), tn_load()
 * sanitizes invalid UTF-8 before parsing so articles still load.
 * Bad bytes become '?' — better than losing everything.
 */
static void test_invalid_utf8_sanitized(void)
{
    const char *yaml =
        "Categories:\n"
        "    TestCat:\n"
        "        Type: [0, 3]\n"
        "        Name: TestCat\n"
        "        Articles:\n"
        "            1:\n"
        "                Title: Test\n"
        "                Poster: user\n"
        "                Data: \"bad bytes \x80\x99 here\"\n"
        "        SubCats: {}\n";

    char tmppath[] = "/tmp/test_tn_utf8_XXXXXX";
    int fd = mkstemp(tmppath);
    assert(fd >= 0);
    write(fd, yaml, strlen(yaml));
    close(fd);

    mobius_threaded_news_t *tn = mobius_threaded_news_new(NULL);
    strncpy(tn->file_path, tmppath, sizeof(tn->file_path) - 1);

    int rc = tn_load(tn);
    printf("\n    [DEBUG] invalid UTF-8: rc=%d cats=%d ", rc, tn->category_count);
    assert(rc == 0);
    assert(tn->category_count == 1);
    assert(tn->categories[0].article_count == 1);
    /* Bad bytes replaced with ?, article body still present */
    tn_article_t *art = &tn->categories[0].articles[0];
    printf("data=\"%.*s\" ", (int)art->data_len, art->data);
    assert(art->data_len > 0);
    assert(strstr(art->data, "bad bytes") != NULL);
    assert(strstr(art->data, "here") != NULL);

    unlink(tmppath);
    mobius_threaded_news_free(tn);
}

/* ===== Lemoniscate save format round-trip ===== */

static const char *LEMONISCATE_SAVED_YAML =
    "Categories:\n"
    "  \"General\":\n"
    "    Name: \"General\"\n"
    "    Type: category\n"
    "    Articles:\n"
    "      \"1\":\n"
    "        Title: \"Hello World\"\n"
    "        Poster: \"admin\"\n"
    "        Body: \"First post!\\nWith a newline.\"\n"
    "        ParentID: 0\n";

static void test_lemoniscate_save_format(void)
{
    mobius_threaded_news_t *tn = load_yaml_string(LEMONISCATE_SAVED_YAML);
    assert(tn->category_count == 1);
    assert(strcmp(tn->categories[0].name, "General") == 0);

    tn_category_t *cat = &tn->categories[0];
    assert(cat->article_count == 1);

    tn_article_t *art = &cat->articles[0];
    printf("\n    [DEBUG] art data_len=%u data=\"%.*s\" ",
           art->data_len, (int)art->data_len, art->data);
    assert(art->id == 1);
    assert(strcmp(art->title, "Hello World") == 0);
    assert(strcmp(art->poster, "admin") == 0);
    /* Body should be parsed (aliased to Data) */
    assert(art->data_len > 0);
    /* ParentID should be parsed as scalar */
    assert(art->parent_id == 0);
    mobius_threaded_news_free(tn);
}

/* ===== Save/load round-trip ===== */

static void test_save_load_roundtrip(void)
{
    /* Create a news object, add an article, save, reload, verify */
    char tmppath[] = "/tmp/test_tn_rt_XXXXXX";
    int fd = mkstemp(tmppath);
    assert(fd >= 0);
    close(fd);

    mobius_threaded_news_t *tn = mobius_threaded_news_new(NULL);
    strncpy(tn->file_path, tmppath, sizeof(tn->file_path) - 1);

    /* Post an article with a newline in the body */
    tn_post_article(tn, "General", 0,
                    "Round Trip Test",
                    "tester",
                    "Line one\rLine two\rLine three", 28);

    /* Reload from saved file */
    mobius_threaded_news_t *tn2 = mobius_threaded_news_new(NULL);
    strncpy(tn2->file_path, tmppath, sizeof(tn2->file_path) - 1);
    int rc = tn_load(tn2);
    assert(rc == 0);

    tn_category_t *cat = NULL;
    int i;
    for (i = 0; i < tn2->category_count; i++) {
        if (strcmp(tn2->categories[i].name, "General") == 0) {
            cat = &tn2->categories[i];
            break;
        }
    }
    assert(cat != NULL);
    assert(cat->article_count >= 1);

    tn_article_t *art = &cat->articles[0];
    printf("\n    [DEBUG] roundtrip data_len=%u ", art->data_len);
    assert(art->data_len == 28);
    assert(strcmp(art->title, "Round Trip Test") == 0);
    assert(strcmp(art->poster, "tester") == 0);
    /* \r should survive round-trip (stored as \r in memory,
     * saved as \n in YAML, loaded back as \r) */
    assert(memcmp(art->data, "Line one\rLine two\rLine three", 28) == 0);

    unlink(tmppath);
    mobius_threaded_news_free(tn);
    mobius_threaded_news_free(tn2);
}

/* ===== Main ===== */

int main(void)
{
    printf("\n=== Threaded News YAML Tests ===\n\n");

    /* Basic parsing */
    TEST(test_empty_categories);
    TEST(test_single_category_no_articles);

    /* Mobius Go format (what's on the VPS) */
    TEST(test_mobius_go_format_categories);
    TEST(test_mobius_go_format_article_count);
    TEST(test_mobius_go_format_article_titles);
    TEST(test_mobius_go_format_article_posters);
    TEST(test_mobius_go_format_plain_data);
    TEST(test_mobius_go_format_block_scalar_data);
    TEST(test_mobius_go_format_quoted_string_data);
    TEST(test_mobius_go_format_second_block_scalar);
    TEST(test_mobius_go_format_date_parsed);
    TEST(test_mobius_go_format_parent_art);

    /* Mobius Go format with DataFlav (older versions) */
    TEST(test_mobius_go_with_dataflav);

    /* Invalid UTF-8 handling */
    TEST(test_invalid_utf8_sanitized);

    /* Lemoniscate save format */
    TEST(test_lemoniscate_save_format);

    /* Round-trip */
    TEST(test_save_load_roundtrip);

    printf("\n  %d/%d tests passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
