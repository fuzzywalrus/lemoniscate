/*
 * file_types.c - File type/creator code mappings
 *
 * Maps to: hotline/file_types.go
 */

#include "hotline/file_types.h"
#include <string.h>
#include <ctype.h>

const char HL_TYPE_FOLDER[5]    = "fldr";
const char HL_CREATOR_FOLDER[5] = "n/a ";

static const hl_file_type_entry_t file_type_table[] = {
    /* Archives */
    { ".sit",        "SIT!", "SIT!" },
    { ".zip",        "ZIP ", "SITx" },
    { ".gz",         "Gzip", "SITx" },
    { ".tgz",        "Gzip", "SITx" },
    { ".tar",        "TARF", "SITx" },
    { ".hqx",        "TEXT", "SITx" },
    { ".sea",        "APPL", "aust" },
    { ".rar",        "RARf", "SITx" },
    { ".7z",         "7zip", "SITx" },
    /* Disk images */
    { ".dmg",        "dImg", "ddsk" },
    { ".img",        "rohd", "ddsk" },
    { ".iso",        "rohd", "ddsk" },
    { ".toast",      "CDr3", "TOAS" },
    { ".smi",        "dImg", "ddsk" },
    /* Images */
    { ".jpg",        "JPEG", "ogle" },
    { ".jpeg",       "JPEG", "ogle" },
    { ".gif",        "GIFf", "ogle" },
    { ".png",        "PNGf", "ogle" },
    { ".bmp",        "BMPf", "ogle" },
    { ".tif",        "TIFF", "ogle" },
    { ".tiff",       "TIFF", "ogle" },
    { ".psd",        "8BPS", "8BIM" },
    { ".ico",        "ICO ", "ogle" },
    /* Documents */
    { ".pdf",        "PDF ", "CARO" },
    { ".txt",        "TEXT", "ttxt" },
    { ".rtf",        "TEXT", "MSWD" },
    { ".doc",        "W8BN", "MSWD" },
    { ".xls",        "XLS8", "XCEL" },
    { ".ppt",        "SLD8", "PPT3" },
    { ".htm",        "TEXT", "MOSS" },
    { ".html",       "TEXT", "MOSS" },
    { ".css",        "TEXT", "ttxt" },
    { ".xml",        "TEXT", "ttxt" },
    { ".csv",        "TEXT", "XCEL" },
    /* Source code / config */
    { ".c",          "TEXT", "ttxt" },
    { ".h",          "TEXT", "ttxt" },
    { ".m",          "TEXT", "ttxt" },
    { ".py",         "TEXT", "ttxt" },
    { ".rb",         "TEXT", "ttxt" },
    { ".js",         "TEXT", "ttxt" },
    { ".sh",         "TEXT", "ttxt" },
    { ".cfg",        "TEXT", "ttxt" },
    { ".conf",       "TEXT", "ttxt" },
    { ".ini",        "TEXT", "ttxt" },
    { ".log",        "TEXT", "ttxt" },
    { ".yaml",       "TEXT", "ttxt" },
    { ".yml",        "TEXT", "ttxt" },
    { ".json",       "TEXT", "ttxt" },
    { ".md",         "TEXT", "ttxt" },
    { ".plist",      "TEXT", "ttxt" },
    /* Audio */
    { ".mp3",        "MPG3", "TVOD" },
    { ".aif",        "AIFF", "TVOD" },
    { ".aiff",       "AIFF", "TVOD" },
    { ".wav",        "WAVE", "TVOD" },
    /* Video */
    { ".mov",        "MooV", "TVOD" },
    { ".mp4",        "mpg4", "TVOD" },
    { ".avi",        "VfW ", "TVOD" },
    /* Applications */
    { ".app",        "APPL", "MACS" },
    /* Game files */
    { ".wad",        "BINA", "????"},
    { ".bsp",        "BINA", "????"},
    { ".gam",        "TEXT", "ttxt" },
    { ".lst",        "TEXT", "ttxt" },
    /* Hotline */
    { ".incomplete", "HTft", "HTLC" },
    { NULL, "", "" }
};

static const hl_file_type_entry_t default_type = { NULL, "BINA", "HTLC" };

const hl_file_type_entry_t *hl_file_type_from_filename(const char *filename)
{
    if (!filename) return &default_type;

    /* Find extension */
    const char *dot = strrchr(filename, '.');
    if (!dot) return &default_type;

    /* Case-insensitive comparison */
    const hl_file_type_entry_t *e;
    for (e = file_type_table; e->extension; e++) {
        if (strcasecmp(dot, e->extension) == 0) {
            return e;
        }
    }

    return &default_type;
}
