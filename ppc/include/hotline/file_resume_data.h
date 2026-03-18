/*
 * file_resume_data.h - File transfer resume data (RFLT format)
 *
 * Maps to: hotline/file_resume_data.go
 *
 * IMPORTANT: This is the ONE protocol structure that uses little-endian
 * encoding for BinaryMarshal. On PPC, byte-swapping is needed here.
 * UnmarshalBinary uses big-endian for ForkInfoList (yes, it's inconsistent
 * in the Go source — see file_resume_data.go lines 66-90).
 */

#ifndef HOTLINE_FILE_RESUME_DATA_H
#define HOTLINE_FILE_RESUME_DATA_H

#include "hotline/types.h"

#define HL_FILE_RESUME_HEADER_SIZE 42  /* 4 + 2 + 34 + 2 */
#define HL_FORK_INFO_SIZE          16  /* 4 + 4 + 4 + 4 */
#define HL_MAX_FORK_COUNT           3  /* DATA, INFO, MACR */

typedef struct {
    uint8_t fork[4];      /* Go: Fork     ForkType - "DATA", "INFO", or "MACR" */
    uint8_t data_size[4]; /* Go: DataSize [4]byte  - resume offset */
    uint8_t rsvd_a[4];    /* Go: RSVDA    [4]byte  */
    uint8_t rsvd_b[4];    /* Go: RSVDB    [4]byte  */
} hl_fork_info_t;

typedef struct {
    uint8_t        format[4];    /* Go: Format    [4]byte  - "RFLT" */
    uint8_t        version[2];   /* Go: Version   [2]byte  - always 1 */
    uint8_t        rsvd[34];     /* Go: RSVD      [34]byte */
    uint8_t        fork_count[2];/* Go: ForkCount [2]byte  */
    hl_fork_info_t fork_info[HL_MAX_FORK_COUNT]; /* Go: ForkInfoList */
    int            fork_info_count; /* Actual number of fork info entries */
} hl_file_resume_data_t;

/*
 * hl_file_resume_data_new - Create a new FileResumeData with given fork info list.
 * Maps to: Go NewFileResumeData()
 */
void hl_file_resume_data_new(hl_file_resume_data_t *frd,
                             const hl_fork_info_t *forks, int count);

/*
 * hl_file_resume_data_marshal - Serialize to wire format.
 * Maps to: Go FileResumeData.BinaryMarshal()
 * NOTE: Uses little-endian for fixed fields (matching Go behavior).
 * Returns bytes written, or -1 on error.
 */
int hl_file_resume_data_marshal(const hl_file_resume_data_t *frd,
                                uint8_t *buf, size_t buf_len);

/*
 * hl_file_resume_data_unmarshal - Parse from wire bytes.
 * Maps to: Go FileResumeData.UnmarshalBinary()
 * NOTE: Uses big-endian for ForkInfoList (matching Go behavior).
 * Returns 0 on success, -1 on error.
 */
int hl_file_resume_data_unmarshal(hl_file_resume_data_t *frd,
                                  const uint8_t *buf, size_t buf_len);

#endif /* HOTLINE_FILE_RESUME_DATA_H */
