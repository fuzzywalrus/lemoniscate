/*
 * access.c - Hotline account class detection
 *
 * Compares an account's access bitmap against the admin and guest templates
 * defined in access.h. Any divergence → HL_CLASS_CUSTOM.
 */

#include "hotline/access.h"
#include <string.h>

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
