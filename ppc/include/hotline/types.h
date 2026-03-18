/*
 * types.h - Foundation types for the Hotline protocol
 *
 * Maps to: hotline/transaction.go (TranType), hotline/field.go (FieldType),
 *          hotline/client_manager.go (ClientID), hotline/chat.go (ChatID)
 *
 * All multi-byte protocol values are big-endian (network byte order),
 * which is native on PowerPC. No byte-swapping needed except in
 * file_resume_data.c (little-endian RFLT format).
 */

#ifndef HOTLINE_TYPES_H
#define HOTLINE_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* --- Fundamental protocol types --- */

typedef uint8_t hl_tran_type_t[2];
typedef uint8_t hl_field_type_t[2];
typedef uint8_t hl_client_id_t[2];
typedef uint8_t hl_chat_id_t[4];
typedef uint8_t hl_access_bitmap_t[8];
typedef uint8_t hl_user_flags_t[2];

/* Dynamic buffer (replaces Go []byte) */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} hl_buf_t;

/* --- Transaction type constants (from hotline/transaction.go) --- */

/* Error / special */
static const hl_tran_type_t TRAN_ERROR                  = {0x00, 0x00}; /* 0   */

/* Chat & messaging (101-122) */
static const hl_tran_type_t TRAN_GET_MSGS               = {0x00, 0x65}; /* 101 */
static const hl_tran_type_t TRAN_NEW_MSG                 = {0x00, 0x66}; /* 102 */
static const hl_tran_type_t TRAN_OLD_POST_NEWS           = {0x00, 0x67}; /* 103 */
static const hl_tran_type_t TRAN_SERVER_MSG              = {0x00, 0x68}; /* 104 */
static const hl_tran_type_t TRAN_CHAT_SEND               = {0x00, 0x69}; /* 105 */
static const hl_tran_type_t TRAN_CHAT_MSG                = {0x00, 0x6A}; /* 106 */
static const hl_tran_type_t TRAN_LOGIN                   = {0x00, 0x6B}; /* 107 */
static const hl_tran_type_t TRAN_SEND_INSTANT_MSG        = {0x00, 0x6C}; /* 108 */
static const hl_tran_type_t TRAN_SHOW_AGREEMENT          = {0x00, 0x6D}; /* 109 */
static const hl_tran_type_t TRAN_DISCONNECT_USER         = {0x00, 0x6E}; /* 110 */
static const hl_tran_type_t TRAN_DISCONNECT_MSG          = {0x00, 0x6F}; /* 111 */
static const hl_tran_type_t TRAN_INVITE_NEW_CHAT         = {0x00, 0x70}; /* 112 */
static const hl_tran_type_t TRAN_INVITE_TO_CHAT          = {0x00, 0x71}; /* 113 */
static const hl_tran_type_t TRAN_REJECT_CHAT_INVITE      = {0x00, 0x72}; /* 114 */
static const hl_tran_type_t TRAN_JOIN_CHAT               = {0x00, 0x73}; /* 115 */
static const hl_tran_type_t TRAN_LEAVE_CHAT              = {0x00, 0x74}; /* 116 */
static const hl_tran_type_t TRAN_NOTIFY_CHAT_CHANGE_USER = {0x00, 0x75}; /* 117 */
static const hl_tran_type_t TRAN_NOTIFY_CHAT_DELETE_USER = {0x00, 0x76}; /* 118 */
static const hl_tran_type_t TRAN_NOTIFY_CHAT_SUBJECT     = {0x00, 0x77}; /* 119 */
static const hl_tran_type_t TRAN_SET_CHAT_SUBJECT        = {0x00, 0x78}; /* 120 */
static const hl_tran_type_t TRAN_AGREED                  = {0x00, 0x79}; /* 121 */
static const hl_tran_type_t TRAN_SERVER_BANNER           = {0x00, 0x7A}; /* 122 */

/* File operations (200-213) */
static const hl_tran_type_t TRAN_GET_FILE_NAME_LIST      = {0x00, 0xC8}; /* 200 */
static const hl_tran_type_t TRAN_DOWNLOAD_FILE           = {0x00, 0xCA}; /* 202 */
static const hl_tran_type_t TRAN_UPLOAD_FILE             = {0x00, 0xCB}; /* 203 */
static const hl_tran_type_t TRAN_DELETE_FILE             = {0x00, 0xCC}; /* 204 */
static const hl_tran_type_t TRAN_NEW_FOLDER              = {0x00, 0xCD}; /* 205 */
static const hl_tran_type_t TRAN_GET_FILE_INFO           = {0x00, 0xCE}; /* 206 */
static const hl_tran_type_t TRAN_SET_FILE_INFO           = {0x00, 0xCF}; /* 207 */
static const hl_tran_type_t TRAN_MOVE_FILE               = {0x00, 0xD0}; /* 208 */
static const hl_tran_type_t TRAN_MAKE_FILE_ALIAS         = {0x00, 0xD1}; /* 209 */
static const hl_tran_type_t TRAN_DOWNLOAD_FLDR           = {0x00, 0xD2}; /* 210 */
static const hl_tran_type_t TRAN_DOWNLOAD_INFO           = {0x00, 0xD3}; /* 211 */
static const hl_tran_type_t TRAN_DOWNLOAD_BANNER         = {0x00, 0xD4}; /* 212 */
static const hl_tran_type_t TRAN_UPLOAD_FLDR             = {0x00, 0xD5}; /* 213 */

/* User operations (300-355) */
static const hl_tran_type_t TRAN_GET_USER_NAME_LIST      = {0x01, 0x2C}; /* 300 */
static const hl_tran_type_t TRAN_NOTIFY_CHANGE_USER      = {0x01, 0x2D}; /* 301 */
static const hl_tran_type_t TRAN_NOTIFY_DELETE_USER      = {0x01, 0x2E}; /* 302 */
static const hl_tran_type_t TRAN_GET_CLIENT_INFO_TEXT    = {0x01, 0x2F}; /* 303 */
static const hl_tran_type_t TRAN_SET_CLIENT_USER_INFO    = {0x01, 0x30}; /* 304 */
static const hl_tran_type_t TRAN_LIST_USERS              = {0x01, 0x5C}; /* 348 */
static const hl_tran_type_t TRAN_UPDATE_USER             = {0x01, 0x5D}; /* 349 */
static const hl_tran_type_t TRAN_NEW_USER                = {0x01, 0x5E}; /* 350 */
static const hl_tran_type_t TRAN_DELETE_USER             = {0x01, 0x5F}; /* 351 */
static const hl_tran_type_t TRAN_GET_USER                = {0x01, 0x60}; /* 352 */
static const hl_tran_type_t TRAN_SET_USER                = {0x01, 0x61}; /* 353 */
static const hl_tran_type_t TRAN_USER_ACCESS             = {0x01, 0x62}; /* 354 */
static const hl_tran_type_t TRAN_USER_BROADCAST          = {0x01, 0x63}; /* 355 */

/* News operations (370-411) */
static const hl_tran_type_t TRAN_GET_NEWS_CAT_NAME_LIST  = {0x01, 0x72}; /* 370 */
static const hl_tran_type_t TRAN_GET_NEWS_ART_NAME_LIST  = {0x01, 0x73}; /* 371 */
static const hl_tran_type_t TRAN_DEL_NEWS_ITEM           = {0x01, 0x7C}; /* 380 */
static const hl_tran_type_t TRAN_NEW_NEWS_FLDR           = {0x01, 0x7D}; /* 381 */
static const hl_tran_type_t TRAN_NEW_NEWS_CAT            = {0x01, 0x7E}; /* 382 */
static const hl_tran_type_t TRAN_GET_NEWS_ART_DATA       = {0x01, 0x90}; /* 400 */
static const hl_tran_type_t TRAN_POST_NEWS_ART           = {0x01, 0x9A}; /* 410 */
static const hl_tran_type_t TRAN_DEL_NEWS_ART            = {0x01, 0x9B}; /* 411 */

/* Keepalive (500) */
static const hl_tran_type_t TRAN_KEEP_ALIVE              = {0x01, 0xF4}; /* 500 */


/* --- Field type constants (from hotline/field.go) --- */

static const hl_field_type_t FIELD_ERROR                = {0x00, 0x64}; /* 100 */
static const hl_field_type_t FIELD_DATA                 = {0x00, 0x65}; /* 101 */
static const hl_field_type_t FIELD_USER_NAME            = {0x00, 0x66}; /* 102 */
static const hl_field_type_t FIELD_USER_ID              = {0x00, 0x67}; /* 103 */
static const hl_field_type_t FIELD_USER_ICON_ID         = {0x00, 0x68}; /* 104 */
static const hl_field_type_t FIELD_USER_LOGIN           = {0x00, 0x69}; /* 105 */
static const hl_field_type_t FIELD_USER_PASSWORD        = {0x00, 0x6A}; /* 106 */
static const hl_field_type_t FIELD_REF_NUM              = {0x00, 0x6B}; /* 107 */
static const hl_field_type_t FIELD_TRANSFER_SIZE        = {0x00, 0x6C}; /* 108 */
static const hl_field_type_t FIELD_CHAT_OPTIONS         = {0x00, 0x6D}; /* 109 */
static const hl_field_type_t FIELD_USER_ACCESS          = {0x00, 0x6E}; /* 110 */
static const hl_field_type_t FIELD_USER_FLAGS           = {0x00, 0x70}; /* 112 */
static const hl_field_type_t FIELD_OPTIONS              = {0x00, 0x71}; /* 113 */
static const hl_field_type_t FIELD_CHAT_ID              = {0x00, 0x72}; /* 114 */
static const hl_field_type_t FIELD_CHAT_SUBJECT         = {0x00, 0x73}; /* 115 */
static const hl_field_type_t FIELD_WAITING_COUNT        = {0x00, 0x74}; /* 116 */
static const hl_field_type_t FIELD_BANNER_TYPE          = {0x00, 0x98}; /* 152 */
static const hl_field_type_t FIELD_VERSION              = {0x00, 0xA0}; /* 160 */
static const hl_field_type_t FIELD_COMMUNITY_BANNER_ID  = {0x00, 0xA1}; /* 161 */
static const hl_field_type_t FIELD_SERVER_NAME          = {0x00, 0xA2}; /* 162 */
static const hl_field_type_t FIELD_FILE_NAME_WITH_INFO  = {0x00, 0xC8}; /* 200 */
static const hl_field_type_t FIELD_FILE_NAME            = {0x00, 0xC9}; /* 201 */
static const hl_field_type_t FIELD_FILE_PATH            = {0x00, 0xCA}; /* 202 */
static const hl_field_type_t FIELD_FILE_RESUME_DATA     = {0x00, 0xCB}; /* 203 */
static const hl_field_type_t FIELD_FILE_TRANSFER_OPTS   = {0x00, 0xCC}; /* 204 */
static const hl_field_type_t FIELD_FILE_TYPE_STRING     = {0x00, 0xCD}; /* 205 */
static const hl_field_type_t FIELD_FILE_CREATOR_STRING  = {0x00, 0xCE}; /* 206 */
static const hl_field_type_t FIELD_FILE_SIZE            = {0x00, 0xCF}; /* 207 */
static const hl_field_type_t FIELD_FILE_CREATE_DATE     = {0x00, 0xD0}; /* 208 */
static const hl_field_type_t FIELD_FILE_MODIFY_DATE     = {0x00, 0xD1}; /* 209 */
static const hl_field_type_t FIELD_FILE_COMMENT         = {0x00, 0xD2}; /* 210 */
static const hl_field_type_t FIELD_FILE_NEW_NAME        = {0x00, 0xD3}; /* 211 */
static const hl_field_type_t FIELD_FILE_NEW_PATH        = {0x00, 0xD4}; /* 212 */
static const hl_field_type_t FIELD_FILE_TYPE            = {0x00, 0xD5}; /* 213 */
static const hl_field_type_t FIELD_QUOTING_MSG          = {0x00, 0xD6}; /* 214 */
static const hl_field_type_t FIELD_AUTOMATIC_RESPONSE   = {0x00, 0xD7}; /* 215 */
static const hl_field_type_t FIELD_FOLDER_ITEM_COUNT    = {0x00, 0xDC}; /* 220 */
static const hl_field_type_t FIELD_USERNAME_WITH_INFO   = {0x01, 0x2C}; /* 300 */
static const hl_field_type_t FIELD_NEWS_ART_LIST_DATA   = {0x01, 0x41}; /* 321 */
static const hl_field_type_t FIELD_NEWS_CAT_NAME        = {0x01, 0x42}; /* 322 */
static const hl_field_type_t FIELD_NEWS_CAT_LIST_DATA15 = {0x01, 0x43}; /* 323 */
static const hl_field_type_t FIELD_NEWS_PATH            = {0x01, 0x45}; /* 325 */
static const hl_field_type_t FIELD_NEWS_ART_ID          = {0x01, 0x46}; /* 326 */
static const hl_field_type_t FIELD_NEWS_ART_DATA_FLAV   = {0x01, 0x47}; /* 327 */
static const hl_field_type_t FIELD_NEWS_ART_TITLE       = {0x01, 0x48}; /* 328 */
static const hl_field_type_t FIELD_NEWS_ART_POSTER      = {0x01, 0x49}; /* 329 */
static const hl_field_type_t FIELD_NEWS_ART_DATE        = {0x01, 0x4A}; /* 330 */
static const hl_field_type_t FIELD_NEWS_ART_PREV_ART    = {0x01, 0x4B}; /* 331 */
static const hl_field_type_t FIELD_NEWS_ART_NEXT_ART    = {0x01, 0x4C}; /* 332 */
static const hl_field_type_t FIELD_NEWS_ART_DATA        = {0x01, 0x4D}; /* 333 */
static const hl_field_type_t FIELD_NEWS_ART_PARENT_ART  = {0x01, 0x4F}; /* 335 */
static const hl_field_type_t FIELD_NEWS_ART_1ST_CHILD   = {0x01, 0x50}; /* 336 */
static const hl_field_type_t FIELD_NEWS_ART_RECURSE_DEL = {0x01, 0x51}; /* 337 */


/* --- Well-known magic values --- */

static const uint8_t FORMAT_FILP[4] = {0x46, 0x49, 0x4C, 0x50}; /* "FILP" */
static const uint8_t FORMAT_RFLT[4] = {0x52, 0x46, 0x4C, 0x54}; /* "RFLT" */
static const uint8_t PLATFORM_AMAC[4] = {0x41, 0x4D, 0x41, 0x43}; /* "AMAC" */
static const uint8_t PLATFORM_MWIN[4] = {0x4D, 0x57, 0x49, 0x4E}; /* "MWIN" */

/* Fork types for file transfers */
static const uint8_t FORK_TYPE_DATA[4] = {0x44, 0x41, 0x54, 0x41}; /* "DATA" */
static const uint8_t FORK_TYPE_INFO[4] = {0x49, 0x4E, 0x46, 0x4F}; /* "INFO" */
static const uint8_t FORK_TYPE_MACR[4] = {0x4D, 0x41, 0x43, 0x52}; /* "MACR" */


/* --- Utility macros for big-endian protocol values on PPC --- */

/* Read a uint16 from a big-endian byte array. On PPC this is a no-op memcpy. */
static inline uint16_t hl_read_u16(const uint8_t b[2])
{
    return (uint16_t)((b[0] << 8) | b[1]);
}

/* Read a uint32 from a big-endian byte array. */
static inline uint32_t hl_read_u32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

/* Write a uint16 to a big-endian byte array. */
static inline void hl_write_u16(uint8_t b[2], uint16_t val)
{
    b[0] = (uint8_t)(val >> 8);
    b[1] = (uint8_t)(val & 0xFF);
}

/* Write a uint32 to a big-endian byte array. */
static inline void hl_write_u32(uint8_t b[4], uint32_t val)
{
    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)(val >> 16);
    b[2] = (uint8_t)(val >> 8);
    b[3] = (uint8_t)(val & 0xFF);
}

/* Compare two 2-byte type identifiers. */
static inline int hl_type_eq(const uint8_t a[2], const uint8_t b[2])
{
    return a[0] == b[0] && a[1] == b[1];
}

/* Convert a 2-byte type to uint16 for indexing/display. */
static inline uint16_t hl_type_to_u16(const uint8_t t[2])
{
    return hl_read_u16(t);
}

#endif /* HOTLINE_TYPES_H */
