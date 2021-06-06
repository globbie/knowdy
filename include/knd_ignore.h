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
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_ignore.h
 *   Knowdy Ignore Unrec GSL
 */
#pragma once

#include "knd_task.h"
#include "knd_config.h"

gsl_err_t knd_ignore_value(void *obj, const char *val, size_t val_size);
gsl_err_t knd_ignore_obj(void *obj, const char *rec, size_t *total_size);

gsl_err_t knd_ignore_named_area(void *obj, const char *name, size_t name_size,
                                const char *rec, size_t *total_size);
gsl_err_t knd_ignore_list(void *obj, const char *name, size_t name_size,
                          const char *rec, size_t *total_size);


