/*
 * access.c - Hotline account class detection and permission-name mapping
 *
 * Compares an account's access bitmap against the admin and guest templates
 * defined in access.h. Any divergence → HL_CLASS_CUSTOM.
 *
 * Also provides the canonical bit-index → YAML permission name table,
 * shared between the server's YAML account manager and the GUI's permission
 * editor. Prior to this change the GUI held its own hardcoded set of names
 * in AppController+AccountsData.inc; task 12 of the colored-nicknames
 * change consolidates on this single source of truth.
 */

#include "hotline/access.h"
#include <string.h>
#include <stddef.h>

/* Canonical name-to-bit mapping. Must stay in sync with
 * include/hotline/access.h #defines and the original access map
 * that was in src/mobius/yaml_account_manager.c. */
static const struct {
    const char *name;
    int         bit;
} hl_access_name_map[] = {
    {"DeleteFile",           ACCESS_DELETE_FILE},
    {"UploadFile",           ACCESS_UPLOAD_FILE},
    {"DownloadFile",         ACCESS_DOWNLOAD_FILE},
    {"RenameFile",           ACCESS_RENAME_FILE},
    {"MoveFile",             ACCESS_MOVE_FILE},
    {"CreateFolder",         ACCESS_CREATE_FOLDER},
    {"DeleteFolder",         ACCESS_DELETE_FOLDER},
    {"RenameFolder",         ACCESS_RENAME_FOLDER},
    {"MoveFolder",           ACCESS_MOVE_FOLDER},
    {"ReadChat",             ACCESS_READ_CHAT},
    {"SendChat",             ACCESS_SEND_CHAT},
    {"OpenChat",             ACCESS_OPEN_CHAT},
    {"CloseChat",            ACCESS_CLOSE_CHAT},
    {"ShowInList",           ACCESS_SHOW_IN_LIST},
    {"CreateUser",           ACCESS_CREATE_USER},
    {"DeleteUser",           ACCESS_DELETE_USER},
    {"OpenUser",             ACCESS_OPEN_USER},
    {"ModifyUser",           ACCESS_MODIFY_USER},
    {"ChangeOwnPass",        ACCESS_CHANGE_OWN_PASS},
    {"NewsReadArt",          ACCESS_NEWS_READ_ART},
    {"NewsPostArt",          ACCESS_NEWS_POST_ART},
    {"DisconnectUser",       ACCESS_DISCON_USER},
    {"CannotBeDisconnected", ACCESS_CANNOT_BE_DISCON},
    {"GetClientInfo",        ACCESS_GET_CLIENT_INFO},
    {"UploadAnywhere",       ACCESS_UPLOAD_ANYWHERE},
    {"AnyName",              ACCESS_ANY_NAME},
    {"NoAgreement",          ACCESS_NO_AGREEMENT},
    {"SetFileComment",       ACCESS_SET_FILE_COMMENT},
    {"SetFolderComment",     ACCESS_SET_FOLDER_COMMENT},
    {"ViewDropBoxes",        ACCESS_VIEW_DROP_BOXES},
    {"MakeAlias",            ACCESS_MAKE_ALIAS},
    {"Broadcast",            ACCESS_BROADCAST},
    {"NewsDeleteArt",        ACCESS_NEWS_DELETE_ART},
    {"NewsCreateCat",        ACCESS_NEWS_CREATE_CAT},
    {"NewsDeleteCat",        ACCESS_NEWS_DELETE_CAT},
    {"NewsCreateFldr",       ACCESS_NEWS_CREATE_FLDR},
    {"NewsDeleteFldr",       ACCESS_NEWS_DELETE_FLDR},
    {"UploadFolder",         ACCESS_UPLOAD_FOLDER},
    {"DownloadFolder",       ACCESS_DOWNLOAD_FOLDER},
    {"SendPrivMsg",          ACCESS_SEND_PRIV_MSG},
    {NULL, 0}
};

hl_account_class_t hl_access_classify(const hl_access_bitmap_t access)
{
    if (memcmp(access, ADMIN_ACCESS_TEMPLATE, sizeof(hl_access_bitmap_t)) == 0) {
        return HL_CLASS_ADMIN;
    }
    if (memcmp(access, GUEST_ACCESS_TEMPLATE, sizeof(hl_access_bitmap_t)) == 0) {
        return HL_CLASS_GUEST;
    }
    return HL_CLASS_CUSTOM;
}

const char *hl_access_bit_name(int bit)
{
    int i;
    for (i = 0; hl_access_name_map[i].name; i++) {
        if (hl_access_name_map[i].bit == bit) return hl_access_name_map[i].name;
    }
    return NULL;
}

int hl_access_name_to_bit(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; hl_access_name_map[i].name; i++) {
        if (strcmp(hl_access_name_map[i].name, name) == 0)
            return hl_access_name_map[i].bit;
    }
    return -1;
}
