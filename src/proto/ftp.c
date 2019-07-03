// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
/* Copyright 2018, SecurActive.
 *
 * This file is part of Junkie.
 *
 * Junkie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Junkie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Junkie.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "junkie/tools/ip_addr.h"
#include "junkie/proto/tcp.h"
#include "junkie/proto/ip.h"
#include "junkie/proto/ftp.h"
#include "junkie/proto/cnxtrack.h"

#undef LOG_CAT
#define LOG_CAT proto_ftp_log_category

LOG_CATEGORY_DEF(proto_ftp);

/*
 * Proto Infos
 */

static void ftp_proto_info_ctor(struct ftp_proto_info *info, struct parser *parser, struct proto_info *parent, size_t head_len, size_t payload)
{
    proto_info_ctor(&info->info, parser, parent, head_len, payload);
}

/*
 * Parse
 */

static int parse_brokendown_addr(char const *data, char const *fmt, size_t rem_len, struct ip_addr *new_addr, uint16_t *new_port)
{
    if (rem_len < 13) return -1;

    // Copy a reasonable amount so that we can null terminate it for sscanf
    char str[64];
    size_t copy_len = MIN(rem_len, sizeof(str)-1);
    memcpy(str, data, copy_len);
    str[copy_len] = '\0';

    unsigned brok_ip[4], brok_port[2];
    int const num_matches = sscanf(str, fmt,
        brok_ip+0, brok_ip+1, brok_ip+2, brok_ip+3,
        brok_port+0, brok_port+1);
    if (num_matches != 6) {
        SLOG(LOG_DEBUG, "Cannot match format %s in payload", fmt);
        return -1;
    }

    // Build corresponding ip/tcp key segments
    uint8_t *a;
    uint32_t new_ip4;
    unsigned i;
    // The following work for any endianess
    for (i = 0, a = (uint8_t *)&new_ip4; i < NB_ELEMS(brok_ip); i++) a[i] = brok_ip[i];
    for (i = 0, a = (uint8_t *)new_port; i < NB_ELEMS(brok_port); i++) a[i] = brok_port[i];
    ip_addr_ctor_from_ip4(new_addr, new_ip4);
    *new_port = ntohs(*new_port);

    return 0;
}

static void check_for_pasv(struct ip_proto_info const *ip, struct proto *requestor, uint8_t const *packet, size_t packet_len, struct timeval const *now)
{
    // Merely check for passive mode transition
#   define PASV "Entering Passive Mode"
    size_t const pasv_len = strlen(PASV);
    char const *pasv = memmem(packet, packet_len, PASV, pasv_len);
    if (! pasv) return;
    pasv += pasv_len;
    size_t rem_len = packet_len - (pasv - (char *)packet);

    // Get advertised address for future cnx destination
    if (rem_len >= 1 && *pasv == '.') {
        pasv ++;
        rem_len --;
    }

    struct ip_addr new_addr;
    uint16_t new_port;
    if (0 != parse_brokendown_addr(pasv, " (%u,%u,%u,%u,%u,%u)", rem_len, &new_addr, &new_port)) return;

    SLOG(LOG_DEBUG, "New passive cnx to %s:%"PRIu16, ip_addr_2_str(&new_addr), new_port);

    // So we are looking for a cnx between the client (dest IP here) and the given ip/port.
    (void)cnxtrack_ip_new(IPPROTO_TCP, &new_addr, new_port, ip->key.addr+1, PORT_UNKNOWN, false, proto_ftp, now, requestor);
#   undef PASV
}

static void check_for_port(struct ip_proto_info const *ip, struct tcp_proto_info const *tcp, struct proto *requestor, uint8_t const *packet, size_t packet_len, struct timeval const *now)
{
    // Merely check for passive mode transition
#   define PORT_CMD "PORT "
    size_t const cmd_len = strlen(PORT_CMD);
    char const *cmd = memmem(packet, packet_len, PORT_CMD, cmd_len);
    if (! cmd) return;
    cmd += cmd_len;
    size_t rem_len = packet_len - (cmd - (char *)packet);

    struct ip_addr usr_addr;
    uint16_t usr_port;
    if (0 != parse_brokendown_addr(cmd, "%u,%u,%u,%u,%u,%u\r\n", rem_len, &usr_addr, &usr_port)) return;

    SLOG(LOG_DEBUG, "New client data link on %s:%"PRIu16, ip_addr_2_str(&usr_addr), usr_port);

    // So we are looking for a cnx from the destinator IP, port 20, and the advertised IP and port.
    (void)cnxtrack_ip_new(IPPROTO_TCP, &usr_addr, usr_port, ip->key.addr+1, tcp->key.port[1]-1, false, proto_ftp, now, requestor);
}

static enum proto_parse_status ftp_parse(struct parser *parser, struct proto_info *parent, unsigned way, uint8_t const *packet, size_t cap_len, size_t wire_len, struct timeval const *now, size_t tot_cap_len, uint8_t const *tot_packet)
{
    // Sanity Checks
    ASSIGN_INFO_CHK(tcp, parent, -1);
    ASSIGN_INFO_CHK(ip, &tcp->info, -1);
    (void)tcp;

    // nope

    // Parse

    struct ftp_proto_info info;
    ftp_proto_info_ctor(&info, parser, parent, 0, wire_len);

    check_for_pasv(ip, parser->proto, packet, cap_len, now);
    check_for_port(ip, tcp, parser->proto, packet, cap_len, now);

    return proto_parse(NULL, &info.info, way, NULL, 0, 0, now, tot_cap_len, tot_packet);
}

/*
 * Init
 */

static struct uniq_proto uniq_proto_ftp;
struct proto *proto_ftp = &uniq_proto_ftp.proto;
static struct port_muxer tcp_port_muxer;

void ftp_init(void)
{
    log_category_proto_ftp_init();

    static struct proto_ops const ops = {
        .parse       = ftp_parse,
        .parser_new  = uniq_parser_new,
        .parser_del  = uniq_parser_del,
        .info_2_str  = proto_info_2_str,
        .info_addr   = proto_info_addr
    };
    uniq_proto_ctor(&uniq_proto_ftp, &ops, "FTP", PROTO_CODE_FTP);
    port_muxer_ctor(&tcp_port_muxer, &tcp_port_muxers, 21, 21, proto_ftp);
}

void ftp_fini(void)
{
#   ifdef DELETE_ALL_AT_EXIT
    port_muxer_dtor(&tcp_port_muxer, &tcp_port_muxers);
    uniq_proto_dtor(&uniq_proto_ftp);
#   endif
    log_category_proto_ftp_fini();
}
