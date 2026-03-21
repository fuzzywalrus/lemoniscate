/*
 * config_plist.c - macOS plist configuration loader
 *
 * Uses CoreFoundation's CFPropertyList API to read server configuration
 * from a standard macOS property list file. Tiger 10.4 compatible.
 */

#include "mobius/config_plist.h"
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include <stdio.h>

/* Helper: extract a C string from a CFDictionary by key */
static int plist_get_string(CFDictionaryRef dict, const char *key,
                             char *out, size_t out_len)
{
    CFStringRef cf_key = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (!cf_key) return 0;

    CFStringRef val = (CFStringRef)CFDictionaryGetValue(dict, cf_key);
    CFRelease(cf_key);

    if (!val || CFGetTypeID(val) != CFStringGetTypeID()) return 0;

    return CFStringGetCString(val, out, (CFIndex)out_len, kCFStringEncodingUTF8);
}

/* Helper: extract a boolean from a CFDictionary by key */
static int plist_get_bool(CFDictionaryRef dict, const char *key, int *out)
{
    CFStringRef cf_key = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (!cf_key) return 0;

    CFBooleanRef val = (CFBooleanRef)CFDictionaryGetValue(dict, cf_key);
    CFRelease(cf_key);

    if (!val || CFGetTypeID(val) != CFBooleanGetTypeID()) return 0;

    *out = CFBooleanGetValue(val) ? 1 : 0;
    return 1;
}

/* Helper: extract an integer from a CFDictionary by key */
static int plist_get_int(CFDictionaryRef dict, const char *key, int *out)
{
    CFStringRef cf_key = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (!cf_key) return 0;

    CFNumberRef val = (CFNumberRef)CFDictionaryGetValue(dict, cf_key);
    CFRelease(cf_key);

    if (!val || CFGetTypeID(val) != CFNumberGetTypeID()) return 0;

    return CFNumberGetValue(val, kCFNumberIntType, out);
}

/* Helper: extract a string array from a CFDictionary by key */
static int plist_get_string_array(CFDictionaryRef dict, const char *key,
                                   char out[][256], int max_count, int *count)
{
    CFStringRef cf_key = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (!cf_key) return 0;

    CFArrayRef arr = (CFArrayRef)CFDictionaryGetValue(dict, cf_key);
    CFRelease(cf_key);

    if (!arr || CFGetTypeID(arr) != CFArrayGetTypeID()) return 0;

    CFIndex n = CFArrayGetCount(arr);
    if (n > max_count) n = max_count;

    int i;
    *count = 0;
    for (i = 0; i < (int)n; i++) {
        CFStringRef item = (CFStringRef)CFArrayGetValueAtIndex(arr, i);
        if (item && CFGetTypeID(item) == CFStringGetTypeID()) {
            if (CFStringGetCString(item, out[*count], 256, kCFStringEncodingUTF8)) {
                (*count)++;
            }
        }
    }
    return 1;
}

int mobius_load_config_plist(hl_config_t *cfg, const char *plist_path)
{
    /* Read plist file into CFData */
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8 *)plist_path, (CFIndex)strlen(plist_path), false);
    if (!url) return -1;

    CFReadStreamRef stream = CFReadStreamCreateWithFile(NULL, url);
    CFRelease(url);
    if (!stream) return -1;

    if (!CFReadStreamOpen(stream)) {
        CFRelease(stream);
        return -1;
    }

    /* Read file contents */
    UInt8 buf[65536];
    CFIndex bytes_read = CFReadStreamRead(stream, buf, sizeof(buf));
    CFReadStreamClose(stream);
    CFRelease(stream);

    if (bytes_read <= 0) return -1;

    CFDataRef data = CFDataCreate(NULL, buf, bytes_read);
    if (!data) return -1;

    /* Parse plist */
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        NULL, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);

    if (!plist) {
        return -1;
    }

    if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        CFRelease(plist);
        return -1;
    }

    CFDictionaryRef dict = (CFDictionaryRef)plist;

    /* Initialize defaults first */
    hl_config_init(cfg);

    /* Read all config fields */
    plist_get_string(dict, "ServerName", cfg->name, sizeof(cfg->name));
    plist_get_string(dict, "ServerDescription", cfg->description, sizeof(cfg->description));
    plist_get_string(dict, "BannerFile", cfg->banner_file, sizeof(cfg->banner_file));
    plist_get_string(dict, "FileRoot", cfg->file_root, sizeof(cfg->file_root));
    plist_get_string(dict, "Encoding", cfg->encoding, sizeof(cfg->encoding));
    plist_get_string(dict, "NewsDelimiter", cfg->news_delimiter, sizeof(cfg->news_delimiter));
    plist_get_string(dict, "NewsDateFormat", cfg->news_date_format, sizeof(cfg->news_date_format));

    plist_get_bool(dict, "EnableTrackerRegistration", &cfg->enable_tracker_registration);
    plist_get_bool(dict, "EnableBonjour", &cfg->enable_bonjour);
    plist_get_bool(dict, "PreserveResourceForks", &cfg->preserve_resource_forks);

    plist_get_int(dict, "MaxDownloads", &cfg->max_downloads);
    plist_get_int(dict, "MaxDownloadsPerClient", &cfg->max_downloads_per_client);
    plist_get_int(dict, "MaxConnectionsPerIP", &cfg->max_connections_per_ip);

    plist_get_string_array(dict, "Trackers",
                            cfg->trackers, HL_CONFIG_MAX_TRACKERS,
                            &cfg->tracker_count);

    CFRelease(plist);
    return 0;
}
