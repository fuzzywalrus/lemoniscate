/*
 * handshake.h - Hotline protocol handshake
 *
 * Maps to: hotline/handshake.go
 *
 * Client sends: TRTP(4) + HOTL(4) + Version(2) + SubVersion(2) = 12 bytes
 * Server replies: TRTP(4) + ErrorCode(4) = 8 bytes
 */

#ifndef HOTLINE_HANDSHAKE_H
#define HOTLINE_HANDSHAKE_H

#include "hotline/types.h"

#define HL_HANDSHAKE_SIZE 12

/* Protocol magic bytes */
static const uint8_t HL_PROTO_TRTP[4] = {0x54, 0x52, 0x54, 0x50}; /* "TRTP" */
static const uint8_t HL_PROTO_HOTL[4] = {0x48, 0x4F, 0x54, 0x4C}; /* "HOTL" */

/* Client handshake (sent by client to server) */
/* Maps to: Go ClientHandshake in client.go */
static const uint8_t HL_CLIENT_HANDSHAKE[12] = {
    0x54, 0x52, 0x54, 0x50,  /* TRTP */
    0x48, 0x4F, 0x54, 0x4C,  /* HOTL */
    0x00, 0x01,               /* Version 1 */
    0x00, 0x02                /* SubVersion 2 */
};

/* Server handshake response (success) */
/* Maps to: Go ServerHandshake / handshakeResponse in handshake.go */
static const uint8_t HL_SERVER_HANDSHAKE_RESPONSE[8] = {
    0x54, 0x52, 0x54, 0x50,  /* TRTP */
    0x00, 0x00, 0x00, 0x00   /* Error code 0 (success) */
};

typedef struct {
    uint8_t protocol[4];     /* Go: Protocol    [4]byte - must be TRTP */
    uint8_t sub_protocol[4]; /* Go: SubProtocol [4]byte - must be HOTL */
    uint8_t version[2];      /* Go: Version     [2]byte */
    uint8_t sub_version[2];  /* Go: SubVersion  [2]byte */
} hl_handshake_t;

/*
 * hl_handshake_parse - Parse a client handshake from 12 bytes.
 * Maps to: Go handshake.Write()
 * Returns 0 on success, -1 on error.
 */
int hl_handshake_parse(hl_handshake_t *h, const uint8_t *buf, size_t buf_len);

/*
 * hl_handshake_valid - Check if handshake has valid TRTP + HOTL.
 * Maps to: Go handshake.Valid()
 */
int hl_handshake_valid(const hl_handshake_t *h);

/*
 * hl_perform_handshake_server - Server-side handshake: read client handshake, validate, send response.
 * Maps to: Go performHandshake()
 * fd is the connected socket. Returns 0 on success, -1 on error.
 */
int hl_perform_handshake_server(int fd);

/*
 * hl_perform_handshake_client - Client-side handshake: send client handshake, read server response.
 * Maps to: Go Client.Handshake()
 * fd is the connected socket. Returns 0 on success, -1 on error.
 */
int hl_perform_handshake_client(int fd);

#endif /* HOTLINE_HANDSHAKE_H */
