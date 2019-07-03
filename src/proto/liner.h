// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
#ifndef LINER_H_100429
#define LINER_H_100429
#include <unistd.h>
#include <stdbool.h>
#include "junkie/cpp.h"

struct liner {
    char const *start;
    // You should not access other fields directly (as they are subject to change)
    size_t tok_size;    // token length
    size_t delim_size;  // and its delimiter
    size_t rem_size;    // remaining size, including the current token
    size_t tot_size;    // initial buffer size
    struct liner_delimiter_set {
        unsigned num_delims;
        struct liner_delimiter {
            char const *str;    // This works only if all delimiter chars are different
            size_t len;
#           define STRING_AND_LEN(s) s, (sizeof(s)-1) // So that we don't have to count chars manually
        } const *delims;
        bool collapse;    // several successive delimiters count as one
    } const *delims;
};

/** Returns the number of chars written, exluding the terminal nul
 * As this function will write a nul byte no matter what the given
 * buffer must be at least 1 byte long. */
unsigned copy_token(char *, size_t, struct liner *);

void liner_init(struct liner *, struct liner_delimiter_set const *, char const *, size_t);

void liner_next(struct liner *);

void liner_grow(struct liner *, char const *end);

static inline bool liner_eof(struct liner *liner)
{
    return liner->tok_size == 0 && liner->delim_size == 0;
}

static inline size_t liner_tok_length(struct liner *liner)
{
    return liner->tok_size;
}

static inline size_t liner_rem_length(struct liner *liner)
{
    return liner->rem_size;
}

static inline size_t liner_parsed(struct liner *liner)
{
    return (liner->tot_size - liner->rem_size) + liner->tok_size + liner->delim_size;
}

static inline void liner_expand(struct liner *liner)
{
    liner->tok_size = liner->rem_size;
    liner->delim_size = 0;
}

static inline void liner_skip(struct liner *liner, size_t len)
{
    assert(liner->rem_size >= len);
    liner->start += len;
    liner->rem_size -= len;
    liner->tok_size -= len; // not necessarily defined but still
}

unsigned long long liner_strtoull(struct liner *, char const **end, int base);

// Some widely used delimiters
extern struct liner_delimiter_set const delim_lines, delim_blanks, delim_spaces, delim_colons, delim_semicolons;

#endif
