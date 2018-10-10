/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   -----------
 *   knd_err.h
 *   Knowdy Error codes
 */

#ifndef KND_ERR_H
#define KND_ERR_H

#include <assert.h>

#include <gsl-parser/gsl_err.h>

/* return error codes */
typedef enum { knd_OK, knd_FAIL, knd_NOMEM, knd_CONFLICT, knd_LIMIT, knd_RANGE, knd_AUTH,
        knd_INVALID_DATA, knd_ACCESS, knd_NO_MATCH, knd_MATCH_FOUND, knd_FORMAT,
        knd_IO_FAIL, knd_EXISTS, knd_EOB, knd_STOP, knd_NEED_WAIT, 
        knd_EXPIRED } 
  knd_err_codes;

static inline int gsl_err_to_knd_err_codes(gsl_err_t gsl_err) {
    switch (gsl_err.code) {
    case gsl_OK:       return knd_OK;
    case gsl_FAIL:     return knd_FAIL;
    case gsl_LIMIT:    return knd_LIMIT;
    case gsl_NO_MATCH: return knd_NO_MATCH;
    case gsl_FORMAT:   return knd_FORMAT;
    case gsl_EXISTS:   return knd_EXISTS;
    default:
        if (is_gsl_err_external(gsl_err))
            return gsl_err_external_to_ext_code(gsl_err);
        assert(0 && "unknown gsl_err");
        return knd_FAIL;
    }
}

#endif
