/*
 * main.c - Lemoniscate server entry point
 *
 * Maps to: cmd/mobius-hotline-server/main.go
 *
 * Parses command-line flags, initializes the server, and starts
 * the kqueue event loop.
 */

#include "hotline/server.h"
#include "hotline/tls.h"
#include "hotline/bonjour.h"
#include "hotline/tracker.h"
#include "mobius/transaction_handlers.h"
#include "mobius/config_loader.h"
#include "mobius/config_plist.h"
#include "mobius/agreement.h"
#include "mobius/flat_news.h"
#include "mobius/yaml_account_manager.h"
#include "mobius/ban_file.h"
#include "mobius/logger_impl.h"
#include "mobius/mnemosyne_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

static hl_server_t *g_server = NULL;
static volatile sig_atomic_t g_reload = 0;

/* Signal handler for graceful shutdown */
static void shutdown_handler(int sig)
{
    (void)sig;
    if (g_server) {
        hl_server_shutdown(g_server);
    }
}

/* Signal handler for SIGHUP reload */
static void reload_handler(int sig)
{
    (void)sig;
    g_reload = 1;
    if (g_server) {
        g_server->reload_pending = 1;
    }
}

/* Perform config reload — maps to Go reloadFunc */
static void do_reload(hl_server_t *srv, const char *config_dir)
{
    hl_log_info(srv->logger, "Reloading configuration (SIGHUP)");

    if (config_dir) {
        /* Reload config */
        mobius_load_config(&srv->config, config_dir);

        /* Reload agreement */
        if (srv->agreement) {
            char path[2048];
            snprintf(path, sizeof(path), "%s/Agreement.txt", config_dir);
            FILE *f = fopen(path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long len = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (len > 0) {
                    uint8_t *new_data = (uint8_t *)malloc((size_t)len);
                    if (new_data) {
                        fread(new_data, 1, (size_t)len, f);
                        free(srv->agreement);
                        srv->agreement = new_data;
                        srv->agreement_len = (size_t)len;
                    }
                }
                fclose(f);
            }
        }

        /* Reload ban list — re-create from file */
        if (srv->ban_list) {
            char path[2048];
            snprintf(path, sizeof(path), "%s/Banlist.yaml", config_dir);
            hl_ban_mgr_t *new_bans = mobius_ban_file_new(path);
            if (new_bans) {
                mobius_ban_file_free(srv->ban_list);
                srv->ban_list = new_bans;
            }
        }

        hl_log_info(srv->logger, "Reload complete");
    }
}

/* Create default config directory — maps to Go --init flag */
static int init_config_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) {
        fprintf(stderr, "Config directory already exists: %s\n", dir);
        return 0;
    }

    /* Create directory structure */
    char path[2048];

    mkdir(dir, 0750);

    snprintf(path, sizeof(path), "%s/Users", dir);
    mkdir(path, 0750);

    snprintf(path, sizeof(path), "%s/Files", dir);
    mkdir(path, 0755);

    /* Write default config.yaml */
    snprintf(path, sizeof(path), "%s/config.yaml", dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f,
            "Name: Lemoniscate Server\n"
            "Description: A Hotline server\n"
            "FileRoot: Files\n"
            "EnableTrackerRegistration: false\n"
            "Trackers: []\n"
            "EnableBonjour: true\n"
            "Encoding: macintosh\n"
            "MaxDownloads: 0\n"
            "MaxDownloadsPerClient: 0\n"
            "MaxConnectionsPerIP: 0\n"
            "PreserveResourceForks: false\n"
            "\n"
            "# Mnemosyne search integration (optional)\n"
            "# Syncs files and news to a Mnemosyne indexing service.\n"
            "# Register at https://agora.vespernet.net/login for an API key.\n"
            "# Mnemosyne:\n"
            "#   url: http://tracker.vespernet.net:8980\n"
            "#   api_key: msv_your_server_key_here\n"
            "#   index_files: true\n"
            "#   index_news: true\n"
        );
        fclose(f);
    }

    /* Write default Agreement.txt */
    snprintf(path, sizeof(path), "%s/Agreement.txt", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Welcome to the server.\n");
        fclose(f);
    }

    /* Write default MessageBoard.txt */
    snprintf(path, sizeof(path), "%s/MessageBoard.txt", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Welcome to the Message Board!\r"
                   "Post news and updates here.\r");
        fclose(f);
    }

    /* Write empty Banlist.yaml */
    snprintf(path, sizeof(path), "%s/Banlist.yaml", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "banList: {}\nbannedUsers: {}\nbannedNicks: {}\n");
        fclose(f);
    }

    /* Write empty ThreadedNews.yaml */
    snprintf(path, sizeof(path), "%s/ThreadedNews.yaml", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Categories: {}\n");
        fclose(f);
    }

    /* Write default admin account */
    snprintf(path, sizeof(path), "%s/Users/admin.yaml", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f,
            "Login: admin\n"
            "Name: Administrator\n"
            "Password: \"\"\n"
            "Access:\n"
            "  DownloadFile: true\n"
            "  UploadFile: true\n"
            "  ReadChat: true\n"
            "  SendChat: true\n"
            "  CreateUser: true\n"
            "  DeleteUser: true\n"
            "  OpenUser: true\n"
            "  ModifyUser: true\n"
            "  GetClientInfo: true\n"
            "  DisconnectUser: true\n"
            "  Broadcast: true\n"
            "  CreateFolder: true\n"
            "  DeleteFile: true\n"
            "  OpenChat: true\n"
            "  NewsReadArt: true\n"
            "  NewsPostArt: true\n"
        );
        fclose(f);
    }

    /* Write default guest account */
    snprintf(path, sizeof(path), "%s/Users/guest.yaml", dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f,
            "Login: guest\n"
            "Name: Guest\n"
            "Password: \"\"\n"
            "Access:\n"
            "  DownloadFile: true\n"
            "  UploadFile: false\n"
            "  ReadChat: true\n"
            "  SendChat: true\n"
            "  CreateUser: false\n"
            "  DeleteUser: false\n"
            "  OpenUser: false\n"
            "  ModifyUser: false\n"
            "  GetClientInfo: true\n"
            "  DisconnectUser: false\n"
            "  Broadcast: false\n"
            "  CreateFolder: false\n"
            "  DeleteFile: false\n"
            "  OpenChat: true\n"
            "  NewsReadArt: true\n"
            "  NewsPostArt: true\n"
        );
        fclose(f);
    }

    printf("Initialized config directory: %s\n", dir);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -i, --interface ADDR   IP address to listen on (default: all)\n"
        "  -p, --port PORT        Base port (default: 5500)\n"
        "  -c, --config DIR       Configuration directory\n"
        "  -f, --log-file PATH    Log file path (enables file logging)\n"
        "  -l, --log-level LEVEL  Log level: debug, info, error (default: info)\n"
        "      --tls-cert PATH    TLS certificate file (PEM)\n"
        "      --tls-key PATH     TLS private key file (PEM)\n"
        "      --tls-port PORT    TLS base port (default: port + 100)\n"
        "      --api-addr ADDR    API listener address (accepted for compatibility)\n"
        "      --api-key KEY      API key (accepted for compatibility)\n"
        "      --init             Initialize a default config directory\n"
        "  -v, --version          Print version and exit\n"
        "  -h, --help             Show this help\n"
        "\n"
        "Lemoniscate — a Hotline server for Mac OS X Tiger/Leopard PPC\n"
        "Based on Mobius (https://github.com/jhalter/mobius)\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *interface_addr = "";
    int port = 5500;
    const char *config_dir = NULL;
    const char *log_file = NULL;
    const char *log_level = "info";
    const char *api_addr = NULL; /* Compatibility flag used by MobiusAdmin */
    const char *api_key = NULL;  /* Compatibility flag used by MobiusAdmin */
    const char *tls_cert = NULL;
    const char *tls_key = NULL;
    int tls_port_arg = 0;
    int show_version = 0;
    int do_init = 0;

    static struct option long_options[] = {
        {"interface", required_argument, 0, 'i'},
        {"port",      required_argument, 0, 'p'},
        {"bind",      required_argument, 0, 'p'}, /* alias for --port */
        {"config",    required_argument, 0, 'c'},
        {"log-file",  required_argument, 0, 'f'},
        {"log-level", required_argument, 0, 'l'},
        {"api-addr",  required_argument, 0, 'A'}, /* accepted for compatibility */
        {"api-key",   required_argument, 0, 'K'}, /* accepted for compatibility */
        {"tls-cert",  required_argument, 0, 'T'},
        {"tls-key",   required_argument, 0, 'Y'},
        {"tls-port",  required_argument, 0, 'S'},
        {"init",      no_argument,       0, 'I'},
        {"version",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:c:f:l:A:K:T:Y:S:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i': interface_addr = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'c': config_dir = optarg; break;
            case 'f': log_file = optarg; break;
            case 'l': log_level = optarg; break;
            case 'A': api_addr = optarg; break;
            case 'K': api_key = optarg; break;
            case 'T': tls_cert = optarg; break;
            case 'Y': tls_key = optarg; break;
            case 'S': tls_port_arg = atoi(optarg); break;
            case 'I': do_init = 1; break;
            case 'v': show_version = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (show_version) {
        printf("lemoniscate 0.1.4\n");
        return 0;
    }

    /* --init: create default config directory and exit */
    if (do_init) {
        const char *dir = config_dir ? config_dir : "config";
        return init_config_dir(dir);
    }

    /* Find config directory if not specified — default to ~/Public/Lemoniscate */
    char default_config[2048];
    if (!config_dir) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(default_config, sizeof(default_config),
                     "%s/Public/Lemoniscate", home);
            config_dir = default_config;
        } else {
            config_dir = mobius_find_config_dir();
        }
    }

    /* Create server */
    hl_server_t *srv = hl_server_new();
    if (!srv) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    /* Replace default logger with file logger if configured */
    if (log_file) {
        hl_logger_t *fl = mobius_file_logger_new(log_file, log_level);
        if (fl) {
            srv->logger->vt->free(srv->logger);
            srv->logger = fl;
        }
    }

    /* Apply options */
    strncpy(srv->net_interface, interface_addr, sizeof(srv->net_interface) - 1);
    srv->port = port;

    if (api_addr || api_key) {
        hl_log_info(srv->logger,
                    "API compatibility flags detected (--api-addr/--api-key); "
                    "embedded REST API is not implemented in this C build");
    }

    /* Initialize file transfer manager */
    srv->file_transfer_mgr = hl_mem_xfer_mgr_new();

    /* Initialize threaded news */
    srv->threaded_news = mobius_threaded_news_new(NULL);

    /* Set text encoding from config (default MacRoman for PPC) */
    srv->use_mac_roman = (strcmp(srv->config.encoding, "utf-8") != 0);

    /* Load config — try plist first on macOS, then YAML */
    int config_loaded = 0;
    if (config_dir) {
#ifdef __APPLE__
        /* Try plist in ~/Library/Preferences/ */
        char plist_path[2048];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(plist_path, sizeof(plist_path),
                     "%s/Library/Preferences/com.lemoniscate.server.plist", home);
            if (mobius_load_config_plist(&srv->config, plist_path) == 0) {
                hl_log_info(srv->logger, "Loaded config from %s", plist_path);
                config_loaded = 1;
            }
        }
        /* Try plist in config directory */
        if (!config_loaded) {
            snprintf(plist_path, sizeof(plist_path), "%s/config.plist", config_dir);
            if (mobius_load_config_plist(&srv->config, plist_path) == 0) {
                hl_log_info(srv->logger, "Loaded config from %s", plist_path);
                config_loaded = 1;
            }
        }
#endif
        /* Load YAML config */
        if (!config_loaded) {
            if (mobius_load_config(&srv->config, config_dir) == 0) {
                hl_log_info(srv->logger, "Loaded config from %s", config_dir);
                config_loaded = 1;
            } else {
                hl_log_info(srv->logger, "No config found, using defaults");
            }
        }

        /* Resolve relative FileRoot against config directory */
        if (srv->config.file_root[0] != '\0' &&
            srv->config.file_root[0] != '/') {
            char combined[2048];
            char resolved[1024];
            snprintf(combined, sizeof(combined), "%s/%s",
                     config_dir, srv->config.file_root);
            if (realpath(combined, resolved) != NULL) {
                strncpy(srv->config.file_root, resolved,
                        sizeof(srv->config.file_root) - 1);
                srv->config.file_root[sizeof(srv->config.file_root) - 1] = '\0';
            }
            hl_log_info(srv->logger, "File root: %s", srv->config.file_root);
        }

        /* Load accounts */
        char path[2048];
        snprintf(path, sizeof(path), "%s/Users", config_dir);
        srv->account_mgr = mobius_yaml_account_mgr_new(path);

        /* Load ban list */
        snprintf(path, sizeof(path), "%s/Banlist.yaml", config_dir);
        srv->ban_list = mobius_ban_file_new(path);

        /* Load agreement */
        snprintf(path, sizeof(path), "%s/Agreement.txt", config_dir);
        {
            FILE *af = fopen(path, "rb");
            if (af) {
                fseek(af, 0, SEEK_END);
                long alen = ftell(af);
                fseek(af, 0, SEEK_SET);
                if (alen > 0) {
                    srv->agreement = (uint8_t *)malloc((size_t)alen);
                    if (srv->agreement) {
                        fread(srv->agreement, 1, (size_t)alen, af);
                        srv->agreement_len = (size_t)alen;
                    }
                }
                fclose(af);
            }
        }

        /* Load message board (flat news) */
        snprintf(path, sizeof(path), "%s/MessageBoard.txt", config_dir);
        srv->flat_news = mobius_flat_news_new(path);

        /* Set threaded news file path and load from disk */
        snprintf(path, sizeof(path), "%s/ThreadedNews.yaml", config_dir);
        strncpy(srv->threaded_news->file_path, path, sizeof(srv->threaded_news->file_path) - 1);
        tn_load(srv->threaded_news);

        /* Load banner */
        if (srv->config.banner_file[0]) {
            char bpath[2048];
            /* If relative, resolve against config dir */
            if (srv->config.banner_file[0] == '/') {
                snprintf(bpath, sizeof(bpath), "%s", srv->config.banner_file);
            } else {
                snprintf(bpath, sizeof(bpath), "%s/%s",
                         config_dir, srv->config.banner_file);
            }
            FILE *bf = fopen(bpath, "rb");
            if (bf) {
                fseek(bf, 0, SEEK_END);
                long blen = ftell(bf);
                fseek(bf, 0, SEEK_SET);
                if (blen > 0) {
                    srv->banner = (uint8_t *)malloc((size_t)blen);
                    if (srv->banner) {
                        fread(srv->banner, 1, (size_t)blen, bf);
                        srv->banner_len = (size_t)blen;
                        hl_log_info(srv->logger, "Loaded banner: %s (%ld bytes)",
                                    srv->config.banner_file, blen);
                    }
                }
                fclose(bf);
            }
        }
    }

    /* Register all 43 transaction handlers */
    mobius_register_handlers(srv);

    /* TLS initialization — maps to Go WithTLS() in main.go */
    {
        /* CLI flags override config file */
        const char *cert = tls_cert ? tls_cert : srv->config.tls_cert_path;
        const char *key  = tls_key  ? tls_key  : srv->config.tls_key_path;
        int tp = tls_port_arg > 0 ? tls_port_arg
               : srv->config.tls_port > 0 ? srv->config.tls_port
               : port + 100; /* default: port + 100 (5600 for 5500) */

        if (cert && cert[0] && key && key[0]) {
            if (hl_tls_server_ctx_init(&srv->tls_ctx, cert, key) == 0) {
                srv->tls_port = tp;
                hl_log_info(srv->logger,
                    "TLS enabled (port %d, transfers on %d)", tp, tp + 1);
            } else {
                hl_log_error(srv->logger,
                    "TLS initialization failed — running without TLS");
            }
        }
    }

    /* Install signal handlers */
    g_server = srv;
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);
    signal(SIGHUP, reload_handler);
    signal(SIGPIPE, SIG_IGN);

    hl_log_info(srv->logger, "Lemoniscate starting on port %d", port);

    /* Bonjour registration (macOS only) */
    hl_bonjour_reg_t *bonjour = NULL;
    if (srv->config.enable_bonjour) {
#ifdef __APPLE__
        bonjour = hl_bonjour_register(srv->config.name, port);
        if (bonjour) {
            hl_log_info(srv->logger, "Registered with Bonjour as \"%s\"", srv->config.name);
        } else {
            hl_log_error(srv->logger, "Failed to register with Bonjour");
        }
#else
        hl_log_info(srv->logger, "Bonjour is not supported on this platform");
#endif
    }

    /* Initial tracker registration */
    if (srv->config.enable_tracker_registration && srv->config.tracker_count > 0) {
        int reg_ok = hl_tracker_register_all(
            (const char (*)[256])srv->config.trackers,
            srv->config.tracker_count,
            (uint16_t)port, 0,
            (uint16_t)srv->tls_port,
            srv->tracker_pass_id,
            srv->config.name,
            srv->config.description);
        hl_log_info(srv->logger, "Tracker registration: %d/%d succeeded",
                    reg_ok, srv->config.tracker_count);
    }

    /* Store config_dir in server for SIGHUP reload access */
    if (config_dir) {
        strncpy(srv->config_dir, config_dir, sizeof(srv->config_dir) - 1);
    }

    /* Initialize Mnemosyne sync if configured */
    mn_sync_t mnemosyne_sync;
    if (srv->config.mnemosyne_url[0] != '\0') {
        mn_sync_init(&mnemosyne_sync, srv);
        if (mn_sync_enabled(&mnemosyne_sync)) {
            srv->mnemosyne_sync = &mnemosyne_sync;
        }
    }

    /* Run the server (blocks until shutdown) */
    int rc = hl_server_listen_and_serve(srv);

    /* Note: SIGHUP reload would ideally be checked inside the kqueue loop.
     * For now, do_reload() is available but requires integration with the
     * server's event loop to be called when g_reload is set. */
    (void)do_reload; /* Suppress unused warning */

    /* Mnemosyne: deregister on shutdown */
    if (srv->mnemosyne_sync) {
        mn_deregister((mn_sync_t *)srv->mnemosyne_sync);
        mn_sync_cleanup((mn_sync_t *)srv->mnemosyne_sync);
        srv->mnemosyne_sync = NULL;
    }

    /* Cleanup */
    if (bonjour) {
        hl_bonjour_unregister(bonjour);
    }

    /* Free mobius-layer subsystems before server struct */
    if (srv->account_mgr) mobius_yaml_account_mgr_free(srv->account_mgr);
    srv->account_mgr = NULL;
    if (srv->ban_list) mobius_ban_file_free(srv->ban_list);
    srv->ban_list = NULL;
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    if (srv->flat_news) mobius_flat_news_free(srv->flat_news);
    srv->flat_news = NULL;

    hl_server_free(srv);
    g_server = NULL;

    return (rc == 0) ? 0 : 1;
}
