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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "junkie/tools/log.h"
#include "junkie/tools/miscmacs.h"
#include "junkie/proto/proto.h"
#include "proto/liner.h"

#undef LOG_CAT
#define LOG_CAT proto_log_category

/*
 * Tools
 */

unsigned copy_token(char *dest, size_t dest_sz, struct liner *liner)
{
    assert(dest_sz >= 1);
    size_t len = MIN(dest_sz-1, liner_tok_length(liner));
    memcpy(dest, liner->start, len);
    dest[len] = '\0';
    return len;
}

// Grow the liner up to the given position.
void liner_grow(struct liner *liner, char const *end)
{
    assert(end >= liner->start + liner->tok_size + liner->delim_size);
    ssize_t diff = end - (liner->start + liner->rem_size);
    liner->rem_size += diff;
    liner->tot_size += diff;
}

unsigned long long liner_strtoull(struct liner *liner, char const **end, int base)
{
    assert(base >= 0);
    unsigned long long ret = 0;
    unsigned o = 0;
    bool neg = false;

    // The string may begin with an arbitrary amount of white space (as determined by isspace(3)) ...
    while (o < liner->tok_size && isspace(liner->start[o])) o ++;

    // ... followed by a single optional '+' or '-' sign.
    if (o < liner->tok_size) {
        if (liner->start[o] == '+') {
            o ++;
        } else if (liner->start[o] == '-') {
            o ++;
            neg = true;
        }
    }

    // If base is zero or 16, the string may then include a "0x" prefix, and the number will be read in base 16;
    if (
        (base == 0 || base == 16) &&
        o + 1 < liner->tok_size &&
        liner->start[o] == '0' &&
        liner->start[o+1] == 'x'
    ) {
        base = 16;
        o += 2;
    }

    // otherwise, a zero base is taken as 10 (decimal) unless the next character is '0', in which case it is taken as 8 (octal).
    if (base == 0) {
        if (o < liner->tok_size && liner->start[o] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    }

    for (; o < liner->tok_size; o++) {
        char c = liner->start[o];
        unsigned digit = 0;
        if (c >= '0' && c <= (base < 10 ? '0'+base-1 : '9')) {
            digit = c - '0';
        } else if (base > 10 && c >= 'a' && c <= 'a' + (base-10)) {
            digit = (c - 'a') + 10;
        } else if (base > 10 && c >= 'A' && c <= 'A' + (base-10)) {
            digit = (c - 'A') + 10;
        } else {
            break;
        }
        ret = ret * base + digit;
    }
    if (end) *end = liner->start + o;

    return neg ? -ret : ret;
}

/*
 * Parse
 */

static int look_for_delim(size_t *tok_len, size_t *delim_len, char const *start, size_t rem_size, struct liner_delimiter_set const *delims)
{
#   define NB_MAX_DELIMS 4
    assert(delims->nb_delims <= NB_MAX_DELIMS);
    struct {
        unsigned matched;  // how many chars were already matched
        bool winner;
        size_t tok_len;
    } matches[NB_MAX_DELIMS];

    for (unsigned d = 0; d < delims->nb_delims; d++) {
        matches[d].matched = 0;
        matches[d].winner = false;
        assert(delims->delims[d].len > 0);  // or the following algo will fail
    }

    int best_winner = -1;
    unsigned nb_matching = 0;

    // Now scan the buffer until a match
    for (unsigned o = 0; o < rem_size; o++) {
        char const c = start[o];
        if (best_winner != -1 && nb_matching == 0) break; // nothing left matching

        for (unsigned d = 0; d < delims->nb_delims; d++) {
            struct liner_delimiter const *delim = delims->delims+d;
            if (! matches[d].winner) {
                if (c == delim->str[matches[d].matched]) {
                    nb_matching += matches[d].matched == 0;
                    if (++matches[d].matched >= delim->len) {   // we have a winner
                        matches[d].winner = true;  // but keep looking for a longer match
                        matches[d].tok_len = o - matches[d].matched + 1;
                        if (-1 == best_winner || matches[d].matched > matches[best_winner].matched) {
                            best_winner = d;
                        }
                        nb_matching --;
                    }
                } else if (matches[d].matched > 0) {
                    matches[d].matched = c == delim->str[0];
                    nb_matching -= matches[d].matched == 0;
                }
            }
        }
    }

    if (-1 == best_winner) return -1;

    *tok_len   = matches[best_winner].tok_len;
    *delim_len = matches[best_winner].matched;

    return 0;
}

// If we have another delimiter just after the delimiter, make it part of the delimiter
static void liner_skip_delimiters(struct liner *liner)
{
    if (! liner->delims->collapse) return;

    size_t const rem = liner->rem_size - liner->tok_size - liner->delim_size;
    char const *const after = liner->start + liner->tok_size + liner->delim_size;

    for (unsigned d = 0; d < liner->delims->nb_delims; d++) {
        struct liner_delimiter const *const delim = liner->delims->delims+d;
        if (delim->len > rem) continue;
        if (0 != strncmp(after, delim->str, delim->len)) continue;
        liner->delim_size += delim->len;
        SLOG(LOG_DEBUG, "absorbing one more delimiter (delim len now %zu)", liner->delim_size);
        liner_skip_delimiters(liner);
        break;
    }
}

void liner_next(struct liner *liner)
{
    // Skip previously found token
    liner_skip(liner, liner->tok_size + liner->delim_size);

    // And look for new one
    if (0 != look_for_delim(&liner->tok_size, &liner->delim_size, liner->start, liner->rem_size, liner->delims)) {
        // then all remaining bytes are the next token
        liner->tok_size = liner->rem_size;
        liner->delim_size = 0;
    }

    liner_skip_delimiters(liner);
}

/*
 * Construction/Destruction
 */

void liner_init(struct liner *liner, struct liner_delimiter_set const *delims, char const *buffer, size_t buffer_size)
{
    liner->start = buffer;
    liner->tok_size = liner->delim_size = 0;
    liner->tot_size = liner->rem_size = buffer_size;
    liner->delims = delims;

    // Get first line
    liner_next(liner);
}

extern inline bool liner_eof(struct liner *);
extern inline size_t liner_tok_length(struct liner *);
extern inline size_t liner_rem_length(struct liner *);
extern inline size_t liner_parsed(struct liner *);
extern inline void liner_expand(struct liner *);
extern void liner_skip(struct liner *, size_t len);

// FIXME: Should we keep both ": " and ":" for colons (and likewise
// for semicols), since spaces are skipped anyway?  A single case
// (":") should suffice.
static struct liner_delimiter
    eols[]     = { { STRING_AND_LEN("\r\n") }, { STRING_AND_LEN("\n") } },
    blanks[]   = { { STRING_AND_LEN(" ") }, { STRING_AND_LEN("\r\n") }, { STRING_AND_LEN("\n") } },
    spaces[]   = { { STRING_AND_LEN(" ") } },
    colons[]   = { { STRING_AND_LEN(": ") }, { STRING_AND_LEN(":") } },
    semicols[] = { { STRING_AND_LEN("; ") }, { STRING_AND_LEN(";") } };
struct liner_delimiter_set const
    delim_lines      = { NB_ELEMS(eols), eols, false },
    delim_blanks     = { NB_ELEMS(blanks), blanks, true },
    delim_spaces     = { NB_ELEMS(spaces), spaces, true },
    delim_colons     = { NB_ELEMS(colons), colons, true },
    delim_semicolons = { NB_ELEMS(semicols), semicols, true };

