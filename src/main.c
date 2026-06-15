#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datalink_sim.h"
#include "ftp_app.h"
#include "network_io.h"
#include "protocol_structs.h"
#include "utils.h"

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

static int receive_ping_reply(
    SOCKET raw_socket,
    uint16_t identifier,
    uint16_t expected_sequence,
    DWORD *elapsed_ms
)
{
    uint8_t receive_buffer[2048];
    nw_endpoint_t remote_endpoint;
    ULONGLONG start_tick;
    ULONGLONG end_tick;
    uint16_t received_sequence;
    nw_ssize_t received_length;

    start_tick = GetTickCount64();

    for (;;) {
        received_length = nw_recv_raw_bytes(
            raw_socket,
            receive_buffer,
            sizeof(receive_buffer),
            &remote_endpoint
        );

        if (received_length <= 0) {
            return 0;
        }

        if (parse_icmp_echo_reply(
            receive_buffer,
            (size_t)received_length,
            identifier,
            &received_sequence
        ) == 0) {
            continue;
        }

        if (received_sequence != expected_sequence) {
            continue;
        }

        end_tick = GetTickCount64();
        if (elapsed_ms != NULL) {
            *elapsed_ms = (DWORD)(end_tick - start_tick);
        }

        printf(
            "Reply from %s: bytes=%d time=%lums icmp_seq=%u\n",
            remote_endpoint.host,
            ICMP_DEFAULT_DATA_SIZE,
            (unsigned long)((elapsed_ms == NULL) ? 0U : *elapsed_ms),
            (unsigned int)received_sequence
        );
        return 1;
    }
}

static int run_ping(const char *host, int continuous_mode)
{
    SOCKET raw_socket;
    char resolved_ipv4[64];
    uint8_t payload[ICMP_DEFAULT_DATA_SIZE];
    uint8_t packet_buffer[sizeof(icmp_header_t) + ICMP_DEFAULT_DATA_SIZE];
    size_t packet_length;
    int init_status;
    uint16_t identifier;
    uint16_t sequence_number;
    unsigned int sent_count;
    unsigned int received_count;
    int status;
    DWORD elapsed_ms;

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

    raw_socket = nw_create_raw_ipv4_socket(IPPROTO_ICMP);
    if (raw_socket == INVALID_SOCKET) {
        log_message(
            LOG_LEVEL_ERROR,
            "failed to create raw ICMP socket for %s (%s), error=%d",
            host,
            resolved_ipv4,
            nw_last_error()
        );
        log_message(LOG_LEVEL_WARN, "raw socket usually requires administrator privileges on Windows");
        nw_cleanup_winsock();
        return 1;
    }

    fill_ping_payload(payload, sizeof(payload));
    identifier = (uint16_t)(GetCurrentProcessId() & 0xFFFFU);
    sequence_number = 1;
    sent_count = 0;
    received_count = 0;

    printf("Pinging %s [%s] with %u bytes of data:\n",
        host,
        resolved_ipv4,
        (unsigned int)sizeof(payload));

    for (;;) {
        packet_length = build_icmp_echo_packet(
            packet_buffer,
            sizeof(packet_buffer),
            identifier,
            sequence_number,
            payload,
            sizeof(payload)
        );

        if (packet_length == 0) {
            log_message(LOG_LEVEL_ERROR, "failed to build ICMP echo request");
            status = 1;
            break;
        }

        if (nw_send_raw_bytes(raw_socket, packet_buffer, packet_length, resolved_ipv4) < 0) {
            log_socket_error("send raw icmp", nw_last_error());
            status = 1;
            break;
        }

        sent_count++;
        elapsed_ms = 0;
        if (receive_ping_reply(raw_socket, identifier, sequence_number, &elapsed_ms) != 0) {
            received_count++;
        } else {
            printf("Request timed out. icmp_seq=%u\n", (unsigned int)sequence_number);
        }

        sequence_number++;
        if (!continuous_mode && sent_count >= 4U) {
            status = 0;
            break;
        }

        Sleep(1000);
    }

    printf("\nPing statistics for %s:\n", resolved_ipv4);
    printf("    Packets: Sent = %u, Received = %u, Lost = %u\n",
        sent_count,
        received_count,
        sent_count - received_count);

    nw_close_socket(raw_socket);
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
