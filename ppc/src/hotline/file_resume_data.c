/*
 * file_resume_data.c - File transfer resume data (RFLT format)
 *
 * Maps to: hotline/file_resume_data.go
 *
 * The Go code uses binary.LittleEndian for BinaryMarshal but
 * binary.BigEndian for UnmarshalBinary's ForkInfoList parsing.
 * We reproduce this exactly for wire compatibility.
 *
 * On PPC, the LittleEndian marshal is the exception — we write
 * bytes directly rather than using native word order.
 */

#include "hotline/file_resume_data.h"
#include <string.h>

void hl_file_resume_data_new(hl_file_resume_data_t *frd,
                             const hl_fork_info_t *forks, int count)
{
    /* Maps to: Go NewFileResumeData() */
    memset(frd, 0, sizeof(*frd));
    memcpy(frd->format, FORMAT_RFLT, 4);
    frd->version[0] = 0;
    frd->version[1] = 1;

    /* Clamp BEFORE writing header byte so fork_count and fork_info_count agree */
    if (count > HL_MAX_FORK_COUNT) count = HL_MAX_FORK_COUNT;
    frd->fork_count[0] = 0;
    frd->fork_count[1] = (uint8_t)count;
    frd->fork_info_count = count;

    int i;
    for (i = 0; i < count; i++) {
        memcpy(&frd->fork_info[i], &forks[i], sizeof(hl_fork_info_t));
    }
}

int hl_file_resume_data_marshal(const hl_file_resume_data_t *frd,
                                uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go FileResumeData.BinaryMarshal()
     *
     * The Go code uses binary.Write with binary.LittleEndian for all fields.
     * For byte arrays this is a no-op (bytes are bytes), but for the ForkInfoList
     * struct fields it means multi-byte values are written little-endian.
     *
     * Since all fields in the Go structs are [N]byte arrays (not uint16/uint32),
     * binary.Write with LittleEndian actually just copies bytes in order.
     * So the marshal is effectively a straight memcpy of the raw bytes. */

    size_t needed = HL_FILE_RESUME_HEADER_SIZE +
                    (size_t)(frd->fork_info_count * HL_FORK_INFO_SIZE);
    if (buf_len < needed) return -1;

    /* Copy header fields as raw bytes (endianness is moot for byte arrays) */
    memcpy(buf,      frd->format, 4);
    memcpy(buf + 4,  frd->version, 2);
    memcpy(buf + 6,  frd->rsvd, 34);
    memcpy(buf + 40, frd->fork_count, 2);

    /* Copy fork info entries */
    int i;
    size_t offset = HL_FILE_RESUME_HEADER_SIZE;
    for (i = 0; i < frd->fork_info_count && i < HL_MAX_FORK_COUNT; i++) {
        memcpy(buf + offset, &frd->fork_info[i], HL_FORK_INFO_SIZE);
        offset += HL_FORK_INFO_SIZE;
    }

    return (int)needed;
}

int hl_file_resume_data_unmarshal(hl_file_resume_data_t *frd,
                                  const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go FileResumeData.UnmarshalBinary() */
    if (buf_len < HL_FILE_RESUME_HEADER_SIZE) return -1;

    memset(frd, 0, sizeof(*frd));
    memcpy(frd->format,     buf, 4);
    memcpy(frd->version,    buf + 4, 2);
    /* rsvd at bytes 6-39, skipped in Go */
    memcpy(frd->fork_count, buf + 40, 2);

    int count = (int)frd->fork_count[1];
    if (count > HL_MAX_FORK_COUNT) count = HL_MAX_FORK_COUNT;

    size_t needed = HL_FILE_RESUME_HEADER_SIZE + (size_t)(count * HL_FORK_INFO_SIZE);
    if (buf_len < needed) return -1;

    frd->fork_info_count = count;
    int i;
    for (i = 0; i < count; i++) {
        size_t start = HL_FILE_RESUME_HEADER_SIZE + (size_t)(i * HL_FORK_INFO_SIZE);
        /* Go uses binary.Read with BigEndian here (yes, different from marshal) */
        memcpy(&frd->fork_info[i], buf + start, HL_FORK_INFO_SIZE);
    }

    return 0;
}
