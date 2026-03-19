/*
 * password.h - Salted SHA-1 password hashing
 *
 * Stores passwords as "sha1:<hex_salt>:<hex_hash>" using OpenSSL's SHA-1.
 * Not bcrypt, but far better than plaintext for a retro Hotline server.
 *
 * The client sends passwords obfuscated with 255-rotation, which we decode
 * before hashing. This is purely server-side — clients are unaffected.
 */

#ifndef HOTLINE_PASSWORD_H
#define HOTLINE_PASSWORD_H

#include <stddef.h>

/* Max length of the "sha1:<salt>:<hash>" string (8+16+1+40+1 = ~66) */
#define HL_PASSWORD_HASH_MAX 128

/*
 * hl_password_hash - Hash a plaintext password with a random salt.
 * Writes "sha1:<hex_salt>:<hex_hash>" to out.
 * Returns 0 on success, -1 on error.
 */
int hl_password_hash(const char *password, char *out, size_t out_len);

/*
 * hl_password_verify - Check a plaintext password against a stored hash.
 * If stored starts with "sha1:", verifies against the salted hash.
 * Otherwise, falls back to plaintext strcmp (for migration of old accounts).
 * Returns 1 if match, 0 if no match.
 */
int hl_password_verify(const char *password, const char *stored);

#endif /* HOTLINE_PASSWORD_H */
