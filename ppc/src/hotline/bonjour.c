/*
 * bonjour.c - Bonjour/mDNS service registration via dns_sd.h
 *
 * Maps to: oleksandr/bonjour usage in cmd/mobius-hotline-server/main.go
 *
 * Tiger's dns_sd.h provides DNSServiceRegister() which is simpler
 * than the Go wrapper — just one function call.
 */

#include "hotline/bonjour.h"
#include <dns_sd.h>
#include <stdlib.h>
#include <string.h>

struct hl_bonjour_reg {
    DNSServiceRef service_ref;
};

hl_bonjour_reg_t *hl_bonjour_register(const char *service_name, int port)
{
    hl_bonjour_reg_t *reg = (hl_bonjour_reg_t *)calloc(1, sizeof(hl_bonjour_reg_t));
    if (!reg) return NULL;

    /* Build TXT record: "txtv=1" + "app=hotline"
     * Maps to: Go []string{"txtv=1", "app=hotline"} */
    TXTRecordRef txt;
    TXTRecordCreate(&txt, 0, NULL);
    TXTRecordSetValue(&txt, "txtv", 1, "1");
    TXTRecordSetValue(&txt, "app", 7, "hotline");

    DNSServiceErrorType err = DNSServiceRegister(
        &reg->service_ref,
        0,                          /* flags */
        0,                          /* interface index (all) */
        service_name,               /* service name */
        "_hotline._tcp",            /* service type */
        NULL,                       /* domain (NULL = default) */
        NULL,                       /* host (NULL = this machine) */
        htons((uint16_t)port),      /* port (network byte order) */
        TXTRecordGetLength(&txt),   /* TXT record length */
        TXTRecordGetBytesPtr(&txt), /* TXT record data */
        NULL,                       /* callback (don't need) */
        NULL                        /* context */
    );

    TXTRecordDeallocate(&txt);

    if (err != kDNSServiceErr_NoError) {
        free(reg);
        return NULL;
    }

    return reg;
}

void hl_bonjour_unregister(hl_bonjour_reg_t *reg)
{
    if (!reg) return;
    if (reg->service_ref) {
        DNSServiceRefDeallocate(reg->service_ref);
    }
    free(reg);
}
