/*
 * access.h - Hotline access permission bitmap
 *
 * The access bitmap is 8 bytes (64 bits) where each bit represents
 * a specific permission. Bit 0 of byte 0 is the highest bit.
 */

#ifndef HOTLINE_ACCESS_H
#define HOTLINE_ACCESS_H

#include "hotline/types.h"

/* Access permission bit indices (from hotline/access.go) */
#define ACCESS_DELETE_FILE         0   /* Can Delete Files */
#define ACCESS_UPLOAD_FILE         1   /* Can Upload Files */
#define ACCESS_DOWNLOAD_FILE       2   /* Can Download Files */
#define ACCESS_RENAME_FILE         3   /* Can Rename Files */
#define ACCESS_MOVE_FILE           4   /* Can Move Files */
#define ACCESS_CREATE_FOLDER       5   /* Can Create Folders */
#define ACCESS_DELETE_FOLDER       6   /* Can Delete Folders */
#define ACCESS_RENAME_FOLDER       7   /* Can Rename Folders */
#define ACCESS_MOVE_FOLDER         8   /* Can Move Folders */
#define ACCESS_READ_CHAT           9   /* Can Read Chat */
#define ACCESS_SEND_CHAT          10   /* Can Send Chat */
#define ACCESS_OPEN_CHAT          11   /* Can Initiate Private Chat */
#define ACCESS_CLOSE_CHAT         12   /* (Unused in practice) */
#define ACCESS_SHOW_IN_LIST       13   /* (Unused in practice) */
#define ACCESS_CREATE_USER        14   /* Can Create Accounts */
#define ACCESS_DELETE_USER        15   /* Can Delete Accounts */
#define ACCESS_OPEN_USER          16   /* Can Read Accounts */
#define ACCESS_MODIFY_USER        17   /* Can Modify Accounts */
#define ACCESS_CHANGE_OWN_PASS    18   /* (Unused in practice) */
/* Bit 19 is unused */
#define ACCESS_NEWS_READ_ART      20   /* Can Read Articles */
#define ACCESS_NEWS_POST_ART      21   /* Can Post Articles */
#define ACCESS_DISCON_USER        22   /* Can Disconnect Users */
#define ACCESS_CANNOT_BE_DISCON   23   /* Cannot be Disconnected */
#define ACCESS_GET_CLIENT_INFO    24   /* Can Get User Info */
#define ACCESS_UPLOAD_ANYWHERE    25   /* Can Upload Anywhere */
#define ACCESS_ANY_NAME           26   /* Can Use Any Name */
#define ACCESS_NO_AGREEMENT       27   /* Don't Show Agreement */
#define ACCESS_SET_FILE_COMMENT   28   /* Can Comment Files */
#define ACCESS_SET_FOLDER_COMMENT 29   /* Can Comment Folders */
#define ACCESS_VIEW_DROP_BOXES    30   /* Can View Drop Boxes */
#define ACCESS_MAKE_ALIAS         31   /* Can Make Aliases */
#define ACCESS_BROADCAST          32   /* Can Broadcast */
#define ACCESS_NEWS_DELETE_ART    33   /* Can Delete Articles */
#define ACCESS_NEWS_CREATE_CAT    34   /* Can Create Categories */
#define ACCESS_NEWS_DELETE_CAT    35   /* Can Delete Categories */
#define ACCESS_NEWS_CREATE_FLDR   36   /* Can Create News Bundles */
#define ACCESS_NEWS_DELETE_FLDR   37   /* Can Delete News Bundles */
#define ACCESS_UPLOAD_FOLDER      38   /* Can Upload Folders */
#define ACCESS_DOWNLOAD_FOLDER    39   /* Can Download Folders */
#define ACCESS_SEND_PRIV_MSG      40   /* Can Send Messages */

/* Chat history extension — bit 56, after accessVoiceChat (55).
 * Servers SHOULD check this bit; if not configured, fall back to ACCESS_READ_CHAT. */
#define ACCESS_READ_CHAT_HISTORY  56   /* Can Request Chat History */

/*
 * hl_access_set - Set a permission bit in the access bitmap.
 */
static inline void hl_access_set(hl_access_bitmap_t bits, int i)
{
    bits[i / 8] |= (uint8_t)(1 << (7 - (i % 8)));
}

/*
 * hl_access_is_set - Check if a permission bit is set.
 */
static inline int hl_access_is_set(const hl_access_bitmap_t bits, int i)
{
    return (bits[i / 8] & (1 << (7 - (i % 8)))) != 0;
}

/*
 * hl_access_clear - Clear a permission bit.
 */
static inline void hl_access_clear(hl_access_bitmap_t bits, int i)
{
    bits[i / 8] &= (uint8_t)~(1 << (7 - (i % 8)));
}

/* --- Account class templates (for colored-nicknames and future class-based features) ---
 *
 * These match the GUI's kAccountPermissionDefs list exactly. The admin
 * template has all GUI-known permission bits set (bits 0-18, 20-40).
 * The guest template has the specific subset the GUI's guestAccessTemplate
 * returns (10 permissions). Class detection is exact bitwise match.
 */

/* Admin template: bits 0-18, 20-40. Bit 19 unused. Bit 56 (ACCESS_READ_CHAT_HISTORY)
 * is intentionally excluded — the GUI's kAccountPermissionDefs does not include it. */
static const hl_access_bitmap_t ADMIN_ACCESS_TEMPLATE = {
    0xFF, /* bits  0-7  */
    0xFF, /* bits  8-15 */
    0xEF, /* bits 16-23 (bit 19 unset) */
    0xFF, /* bits 24-31 */
    0xFF, /* bits 32-39 */
    0x80, /* bits 40-47 (only bit 40 set) */
    0x00, /* bits 48-55 */
    0x00  /* bits 56-63 */
};

/* Guest template: bits 2, 9, 10, 11, 13, 20, 21, 24, 39, 40 set.
 * Matches guestAccessTemplate in AppController+AccountsData.inc. */
static const hl_access_bitmap_t GUEST_ACCESS_TEMPLATE = {
    0x20, /* bit 2                          */
    0x74, /* bits 9, 10, 11, 13             */
    0x0C, /* bits 20, 21                    */
    0x80, /* bit 24                         */
    0x01, /* bit 39                         */
    0x80, /* bit 40                         */
    0x00,
    0x00
};

typedef enum {
    HL_CLASS_CUSTOM = 0,
    HL_CLASS_GUEST,
    HL_CLASS_ADMIN
} hl_account_class_t;

/*
 * hl_access_classify - Classify an access bitmap against the admin/guest
 * templates using exact bitwise equality. Any divergence → HL_CLASS_CUSTOM.
 */
hl_account_class_t hl_access_classify(const hl_access_bitmap_t access);

/*
 * hl_access_bit_name - Return the canonical YAML permission name for a
 * given bit index, e.g. hl_access_bit_name(2) → "DownloadFile". Returns
 * NULL if the bit is not in the canonical map (e.g. unassigned bits).
 *
 * Shared source of truth between the server's YAML account manager and
 * the GUI's permission editor (task 12 of colored-nicknames).
 */
const char *hl_access_bit_name(int bit);

/*
 * hl_access_name_to_bit - Inverse of hl_access_bit_name. Returns the
 * bit index for a given YAML permission name, or -1 if unknown.
 */
int hl_access_name_to_bit(const char *name);

#endif /* HOTLINE_ACCESS_H */
