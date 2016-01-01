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


#include <limits.h>
#include <float.h>
#include <libplctag.h>
#include <libplctag_tag.h>
#include <platform.h>
#include <util/attr.h>
#include <util/platform_ext.h>
#include <ab/ab.h>




/**************************************************************************
 ***************************  API Functions  ******************************
 **************************************************************************/

/*
 * plc_tag_create()
 *
 * This is where the dispatch occurs to the protocol specific implementation.
 */

LIB_EXPORT plc_tag plc_tag_create(const char *attrib_str)
{
    plc_tag tag = PLC_TAG_NULL;
    attr attribs = NULL;
    int rc = PLCTAG_STATUS_OK;
    int debug = 0;
    const char *protocol = NULL;

    if (!attrib_str || !str_length(attrib_str)) {
        return PLC_TAG_NULL;
    }
    attribs = attr_create_from_str(attrib_str);

    if (!attribs) {
        return PLC_TAG_NULL;
    }

    debug = attr_get_int(attribs, "debug", 0);
    
    pdebug(debug, "Starting");

    /* create the tag, this not protocol specific. */
    tag = (plc_tag)mem_alloc(sizeof(struct plc_tag_t));
    if (!tag) {
        pdebug(debug, "Insufficient memory to create tag!");
        return NULL;
    }

    tag->read_cache_ms = attr_get_int(attribs, "read_cache_ms", 0);
    tag->attribs = attribs;
    tag->debug = debug;

    /* set up the mutex for the tag */
    rc = mutex_create(&tag->mut);
    tag->status = rc;
    if (rc != PLCTAG_STATUS_OK) {
        pdebug(debug, "Unable to set up tag mutex!");
        return tag;
    }

    /* get the protocol type and switch on that */
    protocol = attr_get_str(attribs, "protocol", NULL);
    if (!protocol) {
        pdebug(debug, "Unable to find protocol attribute!");
        tag->status = PLCTAG_ERR_BAD_PARAM;
        return tag;
    }

    pdebug(debug, "Calling protocol-specific creation function.");
    if ((str_cmp_i(protocol, "ab_eip") == 0) || (str_cmp_i(protocol, "ab-eip") == 0)) {
        tag->status = ab_tag_create(tag);
    } else {
        pdebug(debug, "Unknown protocol: %s", protocol);
        tag->status = PLCTAG_ERR_BAD_PARAM;
    }

    return tag;
}



/*
 * plc_tag_lock
 *
 * Lock the tag against use by other threads.  Because operations on a tag are
 * very much asynchronous, actions like getting and extracting the data from
 * a tag take more than one API call.  If more than one thread is using the same tag,
 * then the internal state of the tag will get broken and you will probably experience
 * a crash.
 *
 * This should be used to initially lock a tag when starting operations with it
 * followed by a call to plc_tag_unlock when you have everything you need from the tag.
 */

LIB_EXPORT int plc_tag_lock(plc_tag tag)
{
    int debug = tag->debug;

    pdebug(debug, "Starting.");

    if (!tag || !tag->mut)
        return PLCTAG_ERR_NULL_PTR;

    /* lock the mutex */
    tag->status = mutex_lock(tag->mut);

    pdebug(debug, "Done.");

    return tag->status;
}

/*
 * plc_tag_unlock
 *
 * The opposite action of plc_tag_unlock.  This allows other threads to access the
 * tag.
 */

LIB_EXPORT int plc_tag_unlock(plc_tag tag)
{
    int debug = tag->debug;

    pdebug(debug, "Starting.");

    if (!tag || !tag->mut) {
        return PLCTAG_ERR_NULL_PTR;
    }

    /* unlock the mutex */
    tag->status = mutex_unlock(tag->mut);

    pdebug(debug, "Done.");

    return tag->status;
}

/*
 * plc_tag_abort()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.
 *
 * The implementation must do whatever is necessary to abort any
 * ongoing IO.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_abort(plc_tag tag)
{
    int debug = tag->debug;

    pdebug(debug, "Starting.");

    if (!tag) {
        return PLCTAG_ERR_NULL_PTR;
    }

    spin_block(tag->lock)
    {
        tag->abort_requested = 1;
        tag->status = PLCTAG_STATUS_OK;
    }

    /* this may be synchronous. */
    return plc_tag_status(tag);
}

/*
 * plc_tag_destroy()
 *
 * Remove all implementation specific details about a tag and clear its
 * memory.
 *
 * FIXME - this leaves a dangling pointer.  Should we take the address
 * of the tag pointer as an arg and zero out the pointer?  That may not be
 * as portable.
 */

LIB_EXPORT int plc_tag_destroy(plc_tag tag)
{
    int debug = tag->debug;

    pdebug(debug, "Starting.");

    if (!tag) {
        return PLCTAG_STATUS_OK;
    }

    /* if there is a backend tasklet for the tag, use it. */
    if (tag->backend_started) {
        spin_block(tag->lock)
        {
            tag->destroy_requested = 1;
            tag->status = PLCTAG_STATUS_OK;
        }

        pdebug(debug, "Done, passed request to backend.");
        return plc_tag_status(tag);
    } else {
        pdebug(debug, "Done, no backend, so destroying tag directly.");
        return plc_tag_destroy_generic(tag);
    }
}

/*
 * plc_tag_destroy_generic
 *
 * This routine frees the resources associated with a tag.
 * It _MUST_ be called only after the back end has done all
 * the resource releasing for the protocol-specific layer.
 * 
 * FIXME - there is a race condition here if multiple threads
 * call this!
 */
int plc_tag_destroy_generic(plc_tag tag)
{
    mutex_p temp_mut;
    int rc = PLCTAG_STATUS_OK;
    int debug = tag->debug;
    
    pdebug(debug, "Starting");

    /* clear the mutex */
    if (tag->mut) {
        temp_mut = tag->mut;
        tag->mut = NULL;

        critical_block(temp_mut)
        {
            if (tag->data) {
                mem_free(tag->data);
                tag->data = NULL;
                tag->size = 0;
            }

            if (tag->attribs) {
                attr_destroy(tag->attribs);
            }

            mem_free(tag);
        }

        mutex_destroy(&temp_mut);
    }

    pdebug(debug, "Done");

    return rc;
}

/*
 * plc_tag_read()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.  That starts the read operation.
 * If there is a timeout passed, then this routine waits for either
 * a timeout or an error.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_read(plc_tag tag, int timeout)
{
    int debug = 0;
    int rc;

    if (!tag) {
        return PLCTAG_ERR_NULL_PTR;
    }
    
    debug = tag->debug;

    pdebug(debug, "Starting.");

    /* if there is no backend, then punt */
    if (!tag->backend_started) {
        return tag->status;
    }

    /* check read cache, if not expired, return existing data. */
    if (tag->last_read_time_ms + tag->read_cache_ms > time_ms()) {
        pdebug(debug, "Returning cached data.");
        tag->status = PLCTAG_STATUS_OK;
        pdebug(debug,"Done");
        return tag->status;
    }

    /* trigger a read */
    spin_block(tag->lock) {
        tag->read_requested = 1;
        rc = tag->status = PLCTAG_STATUS_PENDING;
    }

    /*
     * if there is a timeout, then loop until we get
     * an error or we timeout.
     */
    if (timeout) {
        uint64_t timeout_time = timeout + time_ms();
        uint64_t start_time = time_ms();

        while (rc == PLCTAG_STATUS_PENDING && timeout_time > time_ms()) {
            rc = plc_tag_status(tag);

            /*
             * terminate early and do not wait again if the
             * IO is done.
             */
            if (rc != PLCTAG_STATUS_PENDING) {
                break;
            }

            sleep_ms(5); /* MAGIC */
        }

        /*
         * if we dropped out of the while loop but the status is
         * still pending, then we timed out.
         *
         * Abort the operation and set the status to show the timeout.
         */
        if (rc == PLCTAG_STATUS_PENDING) {
            plc_tag_abort(tag);
            
            spin_block(tag->lock) {
                rc = tag->status = PLCTAG_ERR_TIMEOUT;
            }
        }

        pdebug(debug, "elapsed time %ldms", (time_ms() - start_time));
    }

    pdebug(debug, "Done");

    return rc;
}

/*
 * plc_tag_status
 *
 * Return the current status of the tag.  This will be PLCTAG_STATUS_PENDING if there is
 * an uncompleted IO operation.  It will be PLCTAG_STATUS_OK if everything is fine.  Other
 * errors will be returned as appropriate.
 *
 * This is a function provided by the underlying protocol implementation.
 */
LIB_EXPORT int plc_tag_status(plc_tag tag)
{
    int rc;

    if (!tag) {
        return PLCTAG_ERR_NULL_PTR;
    }
    
    spin_block(tag->lock) {
        rc = tag->status;
    }
    
    return rc;
}

/*
 * plc_tag_write()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.  That starts the write operation.
 * If there is a timeout passed, then this routine waits for either
 * a timeout or an error.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_write(plc_tag tag, int timeout)
{
    int debug = 0;
    int rc;

    if (!tag) {
        return PLCTAG_ERR_NULL_PTR;
    }

    debug = tag->debug;

    pdebug(debug, "Starting.");

    /* if there is no backend, then punt */
    if (!tag->backend_started) {
        pdebug(debug,"No backend tasklet created!");
        return PLCTAG_ERR_CREATE;
    }
    
    spin_block(tag->lock) {
        tag->write_requested = 1;
        rc = tag->status = PLCTAG_STATUS_PENDING;
    }

    /*
     * if there is a timeout, then loop until we get
     * an error or we timeout.
     */
    if (timeout) {
        uint64_t timeout_time = timeout + time_ms();

        while (rc == PLCTAG_STATUS_PENDING && timeout_time > time_ms()) {
            rc = plc_tag_status(tag);

            /*
             * terminate early and do not wait again if the
             * IO is done.
             */
            if (rc != PLCTAG_STATUS_PENDING) {
                break;
            }

            sleep_ms(5); /* MAGIC */
        }

        /*
         * if we dropped out of the while loop but the status is
         * still pending, then we timed out.
         *
         * Abort the operation and set the status to show the timeout.
         */
        if (rc == PLCTAG_STATUS_PENDING) {
            plc_tag_abort(tag);
            
            spin_block(tag->lock) {
                rc = tag->status = PLCTAG_ERR_TIMEOUT;
            }
        }
    }

    pdebug(debug, "Done");

    return rc;
}

/*
 * Tag data accessors.
 */

LIB_EXPORT int plc_tag_get_size(plc_tag tag)
{
    if (!tag) {
        return PLCTAG_ERR_NULL_PTR;
    }

    return tag->size;
}



LIB_EXPORT uint32_t plc_tag_get_uint32(plc_tag t, int offset)
{
    uint32_t res = UINT32_MAX;

    /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        res = ((uint32_t)(t->data[offset])) + ((uint32_t)(t->data[offset + 1]) << 8) +
              ((uint32_t)(t->data[offset + 2]) << 16) + ((uint32_t)(t->data[offset + 3]) << 24);
    } else {
        res = ((uint32_t)(t->data[offset]) << 24) + ((uint32_t)(t->data[offset + 1]) << 16) +
              ((uint32_t)(t->data[offset + 2]) << 8) + ((uint32_t)(t->data[offset + 3]));
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return res;
}

LIB_EXPORT int plc_tag_set_uint32(plc_tag t, int offset, uint32_t val)
{
    int rc;

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        t->data[offset] = (uint8_t)(val & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
    } else {
        t->data[offset + 3] = (uint8_t)(val & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset] = (uint8_t)((val >> 24) & 0xFF);
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

LIB_EXPORT int32_t plc_tag_get_int32(plc_tag t, int offset)
{
    int32_t res = INT32_MIN;

    /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        res = (int32_t)(((uint32_t)(t->data[offset])) + 
                        ((uint32_t)(t->data[offset + 1]) << 8) +
                        ((uint32_t)(t->data[offset + 2]) << 16) + 
                        ((uint32_t)(t->data[offset + 3]) << 24));
    } else {
        res = (int32_t)(((uint32_t)(t->data[offset]) << 24) + 
                        ((uint32_t)(t->data[offset + 1]) << 16) +
                        ((uint32_t)(t->data[offset + 2]) << 8) + 
                        ((uint32_t)(t->data[offset + 3])));
    }

    t->status = PLCTAG_STATUS_OK;

    return res;
}

LIB_EXPORT int plc_tag_set_int32(plc_tag t, int offset, int32_t ival)
{
    int rc;

    uint32_t val = (uint32_t)(ival);

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        t->data[offset] = (uint8_t)(val & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
    } else {
        t->data[offset + 3] = (uint8_t)(val & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset] = (uint8_t)((val >> 24) & 0xFF);
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

LIB_EXPORT uint16_t plc_tag_get_uint16(plc_tag t, int offset)
{
    uint16_t res = UINT16_MAX;

    /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset + 1 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }
    
    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        res = ((uint16_t)(t->data[offset])) + ((uint16_t)(t->data[offset + 1]) << 8);
    } else {
        res = ((uint16_t)(t->data[offset + 2]) << 8) + ((uint16_t)(t->data[offset + 3]));
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }
    
    return res;
}

LIB_EXPORT int plc_tag_set_uint16(plc_tag t, int offset, uint16_t val)
{
    int rc;

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset + 1 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        t->data[offset] = (uint8_t)(val & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    } else {
        t->data[offset + 1] = (uint8_t)(val & 0xFF);
        t->data[offset] = (uint8_t)((val >> 8) & 0xFF);
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

LIB_EXPORT int16_t plc_tag_get_int16(plc_tag t, int offset)
{
    int16_t res = INT16_MIN;

    /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset + 1 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        res = (int16_t)(((uint16_t)(t->data[offset])) + ((uint16_t)(t->data[offset + 1]) << 8));
    } else {
        res = (int16_t)(((uint16_t)(t->data[offset + 2]) << 8) + ((uint16_t)(t->data[offset + 3])));
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return res;
}

LIB_EXPORT int plc_tag_set_int16(plc_tag t, int offset, int16_t ival)
{
    int rc;

    uint16_t val = (uint16_t)ival;

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset + 1 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        t->data[offset] = (uint8_t)(val & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    } else {
        t->data[offset + 1] = (uint8_t)(val & 0xFF);
        t->data[offset] = (uint8_t)((val >> 8) & 0xFF);
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

LIB_EXPORT uint8_t plc_tag_get_uint8(plc_tag t, int offset)
{
    uint8_t res = UINT8_MAX;

   /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    res = t->data[offset];

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return res;
}

LIB_EXPORT int plc_tag_set_uint8(plc_tag t, int offset, uint8_t val)
{
    int rc;

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    t->data[offset] = val;

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

LIB_EXPORT int8_t plc_tag_get_int8(plc_tag t, int offset)
{
    int8_t res = INT8_MIN;

   /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    res = (int8_t)(t->data[offset]);

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return res;
}

LIB_EXPORT int plc_tag_set_int8(plc_tag t, int offset, int8_t val)
{
    int rc;

    /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    t->data[offset] = (uint8_t)val;

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}

/*
 * FIXME FIXME FIXME
 *
 * This is not portable!
 */
LIB_EXPORT float plc_tag_get_float32(plc_tag t, int offset)
{
    uint32_t ures;
    float res = FLT_MAX;

   /* is there a tag? */
    if (!t) {
        return res;
    }

    /* is the tag ready for this operation? */
    if (plc_tag_status(t) != PLCTAG_STATUS_OK) {
        return res;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) { 
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return res;
    }

    /* is there enough data */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return res;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        ures = ((uint32_t)(t->data[offset])) + ((uint32_t)(t->data[offset + 1]) << 8) +
               ((uint32_t)(t->data[offset + 2]) << 16) + ((uint32_t)(t->data[offset + 3]) << 24);
    } else {
        ures = ((uint32_t)(t->data[offset]) << 24) + ((uint32_t)(t->data[offset + 1]) << 16) +
               ((uint32_t)(t->data[offset + 2]) << 8) + ((uint32_t)(t->data[offset + 3]));
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    /* FIXME - this is not portable! */
    return *((float *)(&ures));
}

/*
 * FIXME FIXME FIXME
 *
 * This is not portable!
 */
LIB_EXPORT int plc_tag_set_float32(plc_tag t, int offset, float fval)
{
    int rc;

    uint32_t val = *((uint32_t *)(&fval));

   /* is there a tag? */
    if (!t) {
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = plc_tag_status(t);

    /* is the tag ready for this operation? */
    if (rc != PLCTAG_STATUS_OK) {
        return rc;
    }

    /* is there data? */
    if (!t->data) {
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_NULL_PTR;
        }
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there enough data space to write the value? */
    if ((offset < 0) || (offset + 3 >= t->size)) { /*MAGIC*/
        spin_block(t->lock) {
            t->status = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* check whether data is little endian or big endian */
    if (t->endian == PLCTAG_DATA_LITTLE_ENDIAN) {
        t->data[offset] = (uint8_t)(val & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
    } else {
        t->data[offset + 3] = (uint8_t)(val & 0xFF);
        t->data[offset + 2] = (uint8_t)((val >> 8) & 0xFF);
        t->data[offset + 1] = (uint8_t)((val >> 16) & 0xFF);
        t->data[offset] = (uint8_t)((val >> 24) & 0xFF);
    }

    spin_block(t->lock) {
        t->status = PLCTAG_STATUS_OK;
    }

    return PLCTAG_STATUS_OK;
}
