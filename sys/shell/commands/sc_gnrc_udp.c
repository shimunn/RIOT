/*
 * Copyright (C) 2015-17 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_shell_commands
 * @{
 *
 * @file
 * @brief       Demonstrating the sending and receiving of UDP data
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Martine Lenders <m.lenders@fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>
#include <inttypes.h>

#include "net/gnrc.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/udp.h"
#include "net/gnrc/pktdump.h"
#include "net/utils.h"
#include "timex.h"
#include "utlist.h"
#if IS_USED(MODULE_ZTIMER)
#include "ztimer.h"
#else
#include "xtimer.h"
#endif

static gnrc_netreg_entry_t server =
                        GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
                                                   KERNEL_PID_UNDEF);

static void _send(const char *addr_str, const char *port_str,
                  const char *data, size_t num, unsigned int delay)
{
    netif_t *netif;
    uint16_t port;
    ipv6_addr_t addr;

    /* parse destination address */
    if (netutils_get_ipv6(&addr, &netif, addr_str) < 0) {
        puts("Error: unable to parse destination address");
        return;
    }
    /* parse port */
    port = atoi(port_str);
    if (port == 0) {
        puts("Error: unable to parse destination port");
        return;
    }

    while (num--) {
        gnrc_pktsnip_t *payload, *udp, *ip;
        unsigned payload_size;
        /* allocate payload */
        payload = gnrc_pktbuf_add(NULL, data, strlen(data), GNRC_NETTYPE_UNDEF);
        if (payload == NULL) {
            puts("Error: unable to copy data to packet buffer");
            return;
        }
        /* store size for output */
        payload_size = (unsigned)payload->size;
        /* allocate UDP header, set source port := destination port */
        udp = gnrc_udp_hdr_build(payload, port, port);
        if (udp == NULL) {
            puts("Error: unable to allocate UDP header");
            gnrc_pktbuf_release(payload);
            return;
        }
        /* allocate IPv6 header */
        ip = gnrc_ipv6_hdr_build(udp, NULL, &addr);
        if (ip == NULL) {
            puts("Error: unable to allocate IPv6 header");
            gnrc_pktbuf_release(udp);
            return;
        }
        /* add netif header, if interface was given */
        if (netif != NULL) {
            gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);

            gnrc_netif_hdr_set_netif(netif_hdr->data,
                                     container_of(netif, gnrc_netif_t, netif));
            ip = gnrc_pkt_prepend(ip, netif_hdr);
        }
        /* send packet */
        if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_UDP,
                                       GNRC_NETREG_DEMUX_CTX_ALL, ip)) {
            puts("Error: unable to locate UDP thread");
            gnrc_pktbuf_release(ip);
            return;
        }
        /* access to `payload` was implicitly given up with the send operation
         * above
         * => use temporary variable for output */
        printf("Success: sent %u byte(s) to [%s]:%u\n", payload_size, addr_str,
               port);
        if (num) {
#if IS_USED(MODULE_ZTIMER_USEC)
            ztimer_sleep(ZTIMER_USEC, delay);
#elif IS_USED(MODULE_XTIMER)
            xtimer_usleep(delay);
#elif IS_USED(MODULE_ZTIMER_MSEC)
            ztimer_sleep(ZTIMER_MSEC, (delay + US_PER_MS - 1) / US_PER_MS);
#endif
        }
    }
}

static void _start_server(const char *port_str)
{
    uint16_t port;

    /* check if server is already running */
    if (server.target.pid != KERNEL_PID_UNDEF) {
        printf("Error: server already running on port %" PRIu32 "\n",
               server.demux_ctx);
        return;
    }
    /* parse port */
    port = atoi(port_str);
    if (port == 0) {
        puts("Error: invalid port specified");
        return;
    }
    /* start server (which means registering pktdump for the chosen port) */
    server.target.pid = gnrc_pktdump_pid;
    server.demux_ctx = (uint32_t)port;
    gnrc_netreg_register(GNRC_NETTYPE_UDP, &server);
    printf("Success: started UDP server on port %" PRIu16 "\n", port);
}

static void _stop_server(void)
{
    /* check if server is running at all */
    if (server.target.pid == KERNEL_PID_UNDEF) {
        printf("Error: server was not running\n");
        return;
    }
    /* stop server */
    gnrc_netreg_unregister(GNRC_NETTYPE_UDP, &server);
    server.target.pid = KERNEL_PID_UNDEF;
    puts("Success: stopped UDP server");
}

int _gnrc_udp_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [send|server]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "send") == 0) {
        uint32_t num = 1;
        uint32_t delay;
        if (argc < 5) {
            printf("usage: %s send "
                   "<addr> <port> <data> [<num> [<delay in us>]]\n", argv[0]);
            return 1;
        }
        if (argc > 5) {
            num = atoi(argv[5]);
        }
        if (argc > 6) {
            delay = atoi(argv[6]);
        } else {
            delay = US_PER_SEC;
        }
        _send(argv[2], argv[3], argv[4], num, delay);
    }
    else if (strcmp(argv[1], "server") == 0) {
        if (argc < 3) {
            printf("usage: %s server [start|stop]\n", argv[0]);
            return 1;
        }
        if (strcmp(argv[2], "start") == 0) {
            if (argc < 4) {
                printf("usage %s server start <port>\n", argv[0]);
                return 1;
            }
            _start_server(argv[3]);
        }
        else if (strcmp(argv[2], "stop") == 0) {
            _stop_server();
        }
        else {
            puts("error: invalid command");
        }
    }
    else {
        puts("error: invalid command");
    }
    return 0;
}
