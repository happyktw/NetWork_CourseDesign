#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datalink_sim.h"
#include "ftp_app.h"
#include "network_io.h"
#include "utils.h"

#include <iphlpapi.h>
#include <icmpapi.h>

static void print_usage(const char *program_name)
{
    printf("Usage:\n");
    printf("  %s help\n", program_name);
    printf("  %s ftp-server <port>\n", program_name);
    printf("  %s ftp-client <host> <port>\n", program_name);
    printf("  %s datalink-demo\n", program_name);
    printf("  %s ping <host> [-t]\n", program_name);
    printf("  %s capture <local-ip>\n", program_name);
}

static int run_datalink_demo(void)
{
    static const uint8_t sample1[] = "frame-one";
    static const uint8_t sample2[] = "frame-two";
    datalink_simulator_t *simulator;
    int status;

    simulator = datalink_simulator_create(DLINK_MODE_GBN, DLINK_DEFAULT_WINDOW, 250, 0.20);
    if (simulator == NULL) {
        log_message(LOG_LEVEL_ERROR, "failed to create datalink simulator");
        return 1;
    }

    datalink_simulator_queue_payload(simulator, sample1, sizeof(sample1) - 1U);
    datalink_simulator_queue_payload(simulator, sample2, sizeof(sample2) - 1U);

    status = datalink_simulator_run(simulator);
    datalink_simulator_print_stats(simulator);
    datalink_simulator_destroy(simulator);
    return status;
}

static void fill_ping_payload(uint8_t *payload, size_t payload_length)
{
    size_t index;

    if (payload == NULL) {
        return;
    }

    for (index = 0; index < payload_length; ++index) {
        payload[index] = (uint8_t)('a' + (index % 26U));
    }
}

static int run_ping(const char *host, int continuous_mode)
{
    HANDLE icmp_handle;
    char resolved_ipv4[64];
    uint8_t payload[32];
    char reply_buffer[sizeof(ICMP_ECHO_REPLY) + 32 + 64];
    IPAddr destination_ip;
    PICMP_ECHO_REPLY echo_reply;
    int init_status;
    unsigned int sent_count;
    unsigned int received_count;
    int status;
    DWORD reply_count;

    init_status = nw_init_winsock();
    if (init_status != 0) {
        log_socket_error("WSAStartup", init_status);
        return 1;
    }

    if (nw_resolve_ipv4(host, resolved_ipv4, sizeof(resolved_ipv4)) != 0) {
        log_message(LOG_LEVEL_ERROR, "failed to resolve target host: %s", host);
        nw_cleanup_winsock();
        return 1;
    }

    icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) {
        log_message(LOG_LEVEL_ERROR, "failed to create ICMP handle, error=%lu", (unsigned long)GetLastError());
        nw_cleanup_winsock();
        return 1;
    }

    destination_ip = inet_addr(resolved_ipv4);
    if (destination_ip == INADDR_NONE && strcmp(resolved_ipv4, "255.255.255.255") != 0) {
        log_message(LOG_LEVEL_ERROR, "invalid destination IPv4 address: %s", resolved_ipv4);
        IcmpCloseHandle(icmp_handle);
        nw_cleanup_winsock();
        return 1;
    }

    fill_ping_payload(payload, sizeof(payload));
    sent_count = 0;
    received_count = 0;
    status = 0;

    printf("Pinging %s [%s] with %u bytes of data:\n",
        host,
        resolved_ipv4,
        (unsigned int)sizeof(payload));

    for (;;) {
        reply_count = IcmpSendEcho(
            icmp_handle,
            destination_ip,
            (LPVOID)payload,
            (WORD)sizeof(payload),
            NULL,
            reply_buffer,
            (DWORD)sizeof(reply_buffer),
            1000
        );

        sent_count++;
        if (reply_count > 0) {
            echo_reply = (PICMP_ECHO_REPLY)reply_buffer;
            received_count++;
            printf(
                "Reply from %s: bytes=%u time=%lums TTL=%u\n",
                resolved_ipv4,
                (unsigned int)echo_reply->DataSize,
                (unsigned long)echo_reply->RoundTripTime,
                (unsigned int)echo_reply->Options.Ttl
            );
        } else {
            printf("Request timed out.\n");
        }

        if (!continuous_mode && sent_count >= 4U) {
            break;
        }

        Sleep(1000);
    }

    printf("\nPing statistics for %s:\n", resolved_ipv4);
    printf("    Packets: Sent = %u, Received = %u, Lost = %u\n",
        sent_count,
        received_count,
        sent_count - received_count);

    IcmpCloseHandle(icmp_handle);
    nw_cleanup_winsock();
    return status;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "ftp-server") == 0) {
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }
        return ftp_server_run((unsigned short)atoi(argv[2]));
    }

    if (strcmp(argv[1], "ftp-client") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        return ftp_client_run(argv[2], (unsigned short)atoi(argv[3]));
    }

    if (strcmp(argv[1], "datalink-demo") == 0) {
        return run_datalink_demo();
    }

    if (strcmp(argv[1], "ping") == 0) {
        if (argc != 3 && argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        if (argc == 4 && strcmp(argv[3], "-t") != 0) {
            print_usage(argv[0]);
            return 1;
        }
        return run_ping(argv[2], argc == 4);
    }

    if (strcmp(argv[1], "capture") == 0) {
        log_message(LOG_LEVEL_WARN, "packet capture module is reserved for the next implementation step");
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
