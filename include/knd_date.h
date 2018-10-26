/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
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
 *   ----------
 *   knd_date.h
 *   Knowdy Date Element
 */

#pragma once

#include <glb-lib/output.h>
#include "knd_config.h"
#include "knd_state.h"

struct kndElem;
struct kndElemRef;

typedef edate knd_date_base_type { KND_DATE_DIGIT,
                                 KND_DATE_TEN,
                                 KND_DATE_HUNDRED,
                                 KND_DATE_THOUSAND,
                                 KND_DATE_TEN_THOUSAND,
                                 KND_DATE_HUNDRED_THOUSAND,
                                 KND_DATE_MILLION,
                                 KND_DATE_BILLION,
                                 KND_DATE_TRILLION,
                                 KND_DATE_QUADRILLION } knd_date_base_type;

typedef edate knd_date_oper_type { KND_DATE_OPER_ADD,
                                 KND_DATE_OPER_SUBTR,
                                 KND_DATE_OPER_MULT,
                                 KND_DATE_OPER_DIV } knd_date_oper_type;
struct kndDate
{

    struct kndState *states;
    size_t num_states;
};
