/***************************************************************************
 *   Copyright (C) 2015 by OmanTek                                         *
 *   Author Kyle Hayes  kylehayes@omantek.com                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/**************************************************************************
 * CHANGE LOG                                                             *
 *                                                                        *
 * 2012-02-23  KRH - Created file.                                        *
 *                                                                        *
 **************************************************************************/

#ifndef __LIBPLCTAG_TAG_H__
#define __LIBPLCTAG_TAG_H__

#include "libplctag.h"
#include <platform.h>
#include <util/attr.h>

#define PLCTAG_DATA_LITTLE_ENDIAN (0)
#define PLCTAG_DATA_BIG_ENDIAN (1)

/*
 * plc_err
 *
 * Use this routine to dump errors into the logs yourself.  Note
 * that it is a macro and thus the line number and function
 * will only be those of the C code.  If you use a wrapper around
 * this, you may want to call the underlying plc_err_impl
 * routine directly.
 */

/* helper functions for logging/errors */
/*LIB_EXPORT void plc_err_impl(int level, const char *func, int line_num, int err_code, const char *fmt, ...);
#if defined(USE_STD_VARARG_MACROS) || defined(_WIN32)
#define plc_err(lib,level,e,f,...) \
   plc_err_impl(lib,level,__PRETTY_FUNCTION__,__LINE__,e,f,__VA_ARGS__)
#else
#define plc_err(lib,level,e,f,a...) \
   plc_err_impl(lib,level,__PRETTY_FUNCTION__,__LINE__,e,f,##a )
#endif
*/

struct plc_tag_t
{
    mutex_p mut;

    /* all these fields are protected by the lock */
    lock_t lock;
    int status;
    int read_requested;
    int write_requested;
    int abort_requested;
    int destroy_requested;
    uint64_t last_read_time_ms;  /* set by back end */
    uint64_t last_write_time_ms; /* set by back end */
    
    /* information set at tag creation time */
    int debug;
    attr attribs; /* attributes for the tag. */

    /* how long to cache a read */
    uint64_t read_cache_ms;

    /* protocol specific! */
    int endian;
    int backend_started;
    void *impl_data;
    int size; /* size in byte of the data */
    uint8_t *data;
};

/* used by protocol implementations from the back end */
extern int plc_tag_destroy_generic(plc_tag tag);

#endif
