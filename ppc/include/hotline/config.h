/*
 * config.h - Hotline server configuration
 *
 * Maps to: hotline/config.go
 */

#ifndef HOTLINE_CONFIG_H
#define HOTLINE_CONFIG_H

#include "hotline/types.h"

#define HL_CONFIG_NAME_MAX         50
#define HL_CONFIG_DESC_MAX        200
#define HL_CONFIG_PATH_MAX       1024
#define HL_CONFIG_MAX_TRACKERS     16
#define HL_CONFIG_MAX_IGNORE       16
#define HL_CONFIG_TRACKER_LEN     256

typedef struct {
    char name[HL_CONFIG_NAME_MAX + 1];           /* Go: Name string                     */
    char description[HL_CONFIG_DESC_MAX + 1];    /* Go: Description string               */
    char banner_file[HL_CONFIG_PATH_MAX];        /* Go: BannerFile string                */
    char file_root[HL_CONFIG_PATH_MAX];          /* Go: FileRoot string                  */
    int  enable_tracker_registration;            /* Go: EnableTrackerRegistration bool    */
    char trackers[HL_CONFIG_MAX_TRACKERS][HL_CONFIG_TRACKER_LEN]; /* Go: Trackers []string */
    int  tracker_count;
    char news_delimiter[64];                     /* Go: NewsDelimiter string              */
    char news_date_format[64];                   /* Go: NewsDateFormat string             */
    int  max_downloads;                          /* Go: MaxDownloads int                  */
    int  max_downloads_per_client;               /* Go: MaxDownloadsPerClient int         */
    int  max_connections_per_ip;                 /* Go: MaxConnectionsPerIP int           */
    int  preserve_resource_forks;                /* Go: PreserveResourceForks bool        */
    char ignore_files[HL_CONFIG_MAX_IGNORE][256];/* Go: IgnoreFiles []string              */
    int  ignore_file_count;
    int  enable_bonjour;                         /* Go: EnableBonjour bool                */
    char encoding[16];                           /* Go: Encoding string ("macintosh"|"utf8") */
} hl_config_t;

/* Initialize config with defaults */
void hl_config_init(hl_config_t *cfg);

#endif /* HOTLINE_CONFIG_H */
