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
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "junkie/cpp.h"
#include "junkie/tools/tempstr.h"
#include "junkie/proto/proto.h"
#include "junkie/proto/udp.h"
#include "junkie/proto/dns.h"

#undef LOG_CAT
#define LOG_CAT proto_dns_log_category

LOG_CATEGORY_DEF(proto_dns);

struct dns_hdr {
    uint16_t transaction_id;
    uint16_t flags;
    uint16_t num_questions;
    uint16_t num_answers;
    uint16_t num_auths;
    uint16_t num_adds;
} packed_;

#define FLAG_QR     0x8000U
#define FLAG_Opcode 0x7800U
#define FLAG_AA     0x0400U
#define FLAG_TC     0x0200U
#define FLAG_RD     0x0100U
#define FLAG_RA     0x0080U
#define FLAG_Z      0x0070U
#define FLAG_RCODE  0x000fU

/*
 * Proto Infos
 */

static char const *dns_req_type_2_str(enum dns_req_type type)
{
    switch (type) {
        case DNS_TYPE_UNSET: return "UNSET";
        case DNS_TYPE_A: return "A";
        case DNS_TYPE_NS: return "NS";
        case DNS_TYPE_MD: return "MD";
        case DNS_TYPE_MF: return "MF";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_SOA: return "SOA";
        case DNS_TYPE_MB: return "MB";
        case DNS_TYPE_MG: return "MG";
        case DNS_TYPE_MR: return "MR";
        case DNS_TYPE_NULL: return "NULL";
        case DNS_TYPE_WKS: return "WKS";
        case DNS_TYPE_PTR: return "PTR";
        case DNS_TYPE_HINFO: return "HINFO";
        case DNS_TYPE_MINFO: return "MINFO";
        case DNS_TYPE_MX: return "MX";
        case DNS_TYPE_TXT: return "TXT";
        case DNS_TYPE_AAAA: return "AAAA";
        case DNS_TYPE_NBNS: return "NB";
        case DNS_TYPE_SRV: return "SRV";
        case DNS_TYPE_A6: return "A6";
        case DNS_TYPE_IXFR: return "IXFR";
        case DNS_TYPE_AXFR: return "AXFR";
        case DNS_TYPE_ANY: return "ANY";
    }
    return tempstr_printf("UNKNOWN(0x%04x)", (unsigned)type);
}

static char const *dns_class_2_str(enum dns_class class)
{
    switch (class) {
        case DNS_CLASS_UNSET: return "UNSET";
        case DNS_CLASS_IN: return "IN";
        case DNS_CLASS_CS: return "CS";
        case DNS_CLASS_CH: return "CH";
        case DNS_CLASS_HS: return "HS";
        case DNS_CLASS_ANY: return "ANY";
    }
    return tempstr_printf("UNKNOWN(0x%04x)", (unsigned)class);
}

static void const *dns_info_addr(struct proto_info const *info_, size_t *size)
{
    struct dns_proto_info const *info = DOWNCAST(info_, info, dns_proto_info);
    if (size) *size = sizeof(*info);
    return info;
}

static char const *dns_info_2_str(struct proto_info const *info_)
{
    struct dns_proto_info const *info = DOWNCAST(info_, info, dns_proto_info);
    return tempstr_printf("%s, %s, tx_id=%"PRIu16", err_code=%"PRIu16", request_type=%s, dns_class=%s, name=%s",
        proto_info_2_str(info_),
        info->query ? "QUERY":"ANSWER",
        info->transaction_id, info->error_code,
        dns_req_type_2_str(info->request_type),
        dns_class_2_str(info->dns_class),
        info->name);
}

static void dns_proto_info_ctor(struct dns_proto_info *info, struct parser *parser, struct proto_info *parent, size_t packet_len, uint16_t transaction_id, uint16_t flags)
{
    proto_info_ctor(&info->info, parser, parent, packet_len, 0);
    info->transaction_id = transaction_id;
    info->query = 0 == (flags & FLAG_QR);
    info->error_code = flags & FLAG_RCODE;
    info->name[0] = '\0';
    info->request_type = DNS_TYPE_UNSET;
    info->dns_class = DNS_CLASS_UNSET;
    // Other fields are extracted later
}

/*
 * Parse
 */

ssize_t extract_qname(char *name, size_t name_len, uint8_t const *buf, size_t buf_len, bool prepend_dot)
{
    if (buf_len == 0) return -1;

    // read length
    uint8_t len = *buf++;
    buf_len--;
    // Consider a name pointer as valid. We advance of the size of pointer (2 bytes) which finish the name
    if ((len & 0xc0) == 0xc0) return 2;
    if (len > buf_len || len >= 64) return -1;

    if (len == 0) {
        if (name_len > 0) *name = '\0';
        else name[-1] = '\0';   // if name_len == 0 then the name buffer is full, nul term it.
        return 1;
    }

    if (prepend_dot && name_len > 0) {
        *name++ = '.';
        name_len--;
    }

    size_t const copy_len = MIN(name_len, len);
    memcpy(name, buf, copy_len);

    ssize_t ret = extract_qname(name + copy_len, name_len - copy_len, buf+len, buf_len-len, true);
    return ret < 0 ? ret : len + 1 + ret;
}

static void strmove(char *d, char *s)
{
    while (*s) *d ++ = *s ++;
    *d = '\0';
}

bool looks_like_netbios(char const *name)
{
    unsigned len = 0;
    while (*name != '\0' && *name != '.') {
        if (*name < 'A' || *name > 'P') return false;
        name ++;
        len ++;
    }
    return len >= 32;
}

enum proto_parse_status dns_parse(struct parser *parser, struct proto_info *parent, unsigned way, uint8_t const *packet, size_t cap_len, size_t wire_len, struct timeval const *now, size_t tot_cap_len, uint8_t const *tot_packet)
{
    struct dns_hdr *dnshdr = (struct dns_hdr *)packet;

    // Sanity checks
    if (wire_len < sizeof(*dnshdr)) return PROTO_PARSE_ERR;
    if (cap_len < sizeof(*dnshdr)) return PROTO_TOO_SHORT;

    size_t parsed = sizeof(*dnshdr);
    uint16_t const flags = READ_U16N(&dnshdr->flags);
    uint16_t const transaction_id = READ_U16N(&dnshdr->transaction_id);
    uint16_t const num_questions = READ_U16N(&dnshdr->num_questions);
    uint16_t const num_answers = READ_U16N(&dnshdr->num_answers);
    uint16_t const num_auths = READ_U16N(&dnshdr->num_auths);
    uint16_t const num_adds = READ_U16N(&dnshdr->num_adds);

    struct dns_proto_info info;
    dns_proto_info_ctor(&info, parser, parent, wire_len, transaction_id, flags);

    // Improbable number of questions and answers
    if (num_questions > 255 || num_answers > 255 || num_auths > 255 || num_adds > 255) return PROTO_PARSE_ERR;

    char tmp_name[sizeof(info.name)];    // to skip all but first names

    // Extract queried name
    for (unsigned q = 0; q < num_questions; q++) {
        ssize_t ret = extract_qname(q == 0 ? info.name : tmp_name, sizeof(info.name), packet+parsed, cap_len-parsed, false);
        if (ret < 0) return PROTO_TOO_SHORT;    // FIXME: or maybe this is a parse error ? extract_qname should tell the difference
        parsed += ret;
        uint16_t tmp;
        if (wire_len-parsed < 2*sizeof(tmp)) return PROTO_PARSE_ERR;
        if (cap_len-parsed < 2*sizeof(tmp)) return PROTO_TOO_SHORT;
        memcpy(&tmp, packet+parsed, sizeof(tmp));
        parsed += sizeof(tmp);
        if (q == 0) info.request_type = ntohs(tmp);
        memcpy(&tmp, packet+parsed, sizeof(tmp));
        parsed += sizeof(tmp);
        if (q == 0) info.dns_class = ntohs(tmp);

        // now fix the netbios name (according to RFC 1001)
        if (q == 0 && ((info.request_type == DNS_TYPE_NBNS || info.request_type == DNS_TYPE_SRV) && looks_like_netbios(info.name))) {
            // convert inplace up to the end or the first dot
            char *s = info.name;
            char *d = s;
            while (
                s[0] != '\0' && s[1] != '\0' &&
                s[0] != '.' && s[1] != '.' &&
                s[0] >= 'A' && s[1] >= 'A' &&
                s[0] <= 'P' && s[1] <= 'P'
            ) {
                *d ++ = ((s[0] - 'A') << 4) | (s[1] - 'A');
                s += 2;
            }
            strmove(d, s);  // copy the rest of the name
        }
    }

    // We don't care that much about the answer.
    return proto_parse(NULL, &info.info, way, NULL, 0, 0, now, tot_cap_len, tot_packet);
}

/*
 * Init
 */

static struct uniq_proto uniq_proto_dns;
struct proto *proto_dns = &uniq_proto_dns.proto;
static struct port_muxer dns_port_muxer, mdns_port_muxer, nbns_port_muxer, llmnr_port_muxer;

void dns_init(void)
{
    log_category_proto_dns_init();

    static struct proto_ops const ops = {
        .parse       = dns_parse,
        .parser_new  = uniq_parser_new,
        .parser_del  = uniq_parser_del,
        .info_2_str  = dns_info_2_str,
        .info_addr   = dns_info_addr
    };
    uniq_proto_ctor(&uniq_proto_dns, &ops, "DNS", PROTO_CODE_DNS);
    port_muxer_ctor(&dns_port_muxer, &udp_port_muxers, DNS_PORT, DNS_PORT, proto_dns);
    port_muxer_ctor(&mdns_port_muxer, &udp_port_muxers, MDNS_PORT, MDNS_PORT, proto_dns);
    port_muxer_ctor(&nbns_port_muxer, &udp_port_muxers, NBNS_PORT, NBNS_PORT, proto_dns);
    port_muxer_ctor(&llmnr_port_muxer, &udp_port_muxers, LLMNR_PORT, LLMNR_PORT, proto_dns);
}

void dns_fini(void)
{
#   ifdef DELETE_ALL_AT_EXIT
    port_muxer_dtor(&llmnr_port_muxer, &udp_port_muxers);
    port_muxer_dtor(&nbns_port_muxer, &udp_port_muxers);
    port_muxer_dtor(&mdns_port_muxer, &udp_port_muxers);
    port_muxer_dtor(&dns_port_muxer, &udp_port_muxers);
    uniq_proto_dtor(&uniq_proto_dns);
#   endif
    log_category_proto_dns_fini();
}

