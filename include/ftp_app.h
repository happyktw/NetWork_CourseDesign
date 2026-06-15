#ifndef FTP_APP_H
#define FTP_APP_H

#include <stddef.h>

#include "network_io.h"
#include "protocol_structs.h"

typedef enum ftp_command_type {
    FTP_CMD_INVALID = 0,
    FTP_CMD_HELP,
    FTP_CMD_PWD,
    FTP_CMD_DIR,
    FTP_CMD_CD,
    FTP_CMD_GET,
    FTP_CMD_PUT,
    FTP_CMD_QUIT
} ftp_command_type_t;

typedef struct ftp_command {
    ftp_command_type_t type;
    char argument[FTP_MAX_PATH_LEN];
} ftp_command_t;

int ftp_server_run(unsigned short port);
int ftp_client_run(const char *host, unsigned short port);

ftp_command_type_t ftp_parse_command(const char *line, ftp_command_t *command);

#endif
