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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <libplctag.h>
#include <ab/common.h>
#include <libplctag_tag.h>

/*****************************************************************************
 ******************  Common AB Support Routines ******************************
 ****************************************************************************/

char *get_gateway(plc_tag tag)
{
    attr attribs = tag->attribs;
    const char *gateway_field = attr_get_str(attribs, "gateway", NULL);
    const char *start, *cur;
    char *ret = NULL;

    if (gateway_field) {
        return str_dup(gateway_field);
    }

    /* No special field, let's try the first element of the path. */
    start = cur = attr_get_str(attribs, "path", NULL);

    if (!cur) {
        /* oops, bad attributes */
        return NULL;
    }

    /* go to the first comma, could use strchr? but Windows POSIX is lousy... */
    while (*cur && *cur != ',') {
        cur++;
    }

    if (*cur != ',') {
        /* oops */
        return NULL;
    }

    ret = (char *)mem_alloc((cur - start) + 1);

    if (ret) {
        str_copy(ret, start, (cur - start));
    }

    return ret;
}

#ifdef START
#undef START
#endif
#define START 1

#ifdef ARRAY
#undef ARRAY
#endif
#define ARRAY 2

#ifdef DOT
#undef DOT
#endif
#define DOT 3

#ifdef NAME
#undef NAME
#endif
#define NAME 4

uint8_t *cip_encode_tag_name(uint8_t *data, plc_tag front_end_tag)
{
    const char *p = attr_get_str(front_end_tag->attribs, "name", NULL);
    uint8_t *word_count = NULL;
    uint8_t *dp = NULL;
    uint8_t *name_len;
    int state;
    int debug = front_end_tag->debug;

    if (!p) {
        /* no name param! */
        pdebug(debug, "Name parameter missing!");
        return NULL;
    }

    /* reserve room for word count for IOI string. */
    word_count = data;
    dp = data + 1;

    state = START;

    while (*p) {
        switch (state) {
        case START:

            /* must start with an alpha character or _ or :. */
            if (isalpha(*p) || *p == '_' || *p == ':') {
                state = NAME;
            } else if (*p == '.') {
                state = DOT;
            } else if (*p == '[') {
                state = ARRAY;
            } else {
                return 0;
            }

            break;

        case NAME:
            *dp = 0x91; /* start of ASCII name */
            dp++;
            name_len = dp;
            *name_len = 0;
            dp++;

            while (isalnum(*p) || *p == '_' || *p == ':') {
                *dp = *p;
                dp++;
                p++;
                (*name_len)++;
            }

            /* must pad the name to a multiple of two bytes */
            if (*name_len & 0x01) {
                *dp = 0;
                dp++;
            }

            state = START;

            break;

        case ARRAY:
            /* move the pointer past the [ character */
            p++;

            do {
                uint32_t val;
                char *np = NULL;
                val = (uint32_t)strtol(p, &np, 0);

                if (np == p) {
                    /* we must have a number */
                    return 0;
                }

                p = np;

                if (val > 0xFFFF) {
                    *dp = 0x2A;
                    dp++; /* 4-byte value */
                    *dp = 0;
                    dp++; /* padding */

                    /* copy the value in little-endian order */
                    *dp = val & 0xFF;
                    dp++;
                    *dp = (val >> 8) & 0xFF;
                    dp++;
                    *dp = (val >> 16) & 0xFF;
                    dp++;
                    *dp = (val >> 24) & 0xFF;
                    dp++;
                } else if (val > 0xFF) {
                    *dp = 0x29;
                    dp++; /* 2-byte value */
                    *dp = 0;
                    dp++; /* padding */

                    /* copy the value in little-endian order */
                    *dp = val & 0xFF;
                    dp++;
                    *dp = (val >> 8) & 0xFF;
                    dp++;
                } else {
                    *dp = 0x28;
                    dp++; /* 1-byte value */
                    *dp = val;
                    dp++; /* value */
                }

                /* eat up whitespace */
                while (isspace(*p))
                    p++;
            } while (*p == ',');

            if (*p != ']')
                return 0;

            p++;

            state = START;

            break;

        case DOT:
            p++;
            state = START;
            break;

        default:
            /* this should never happen */
            return 0;

            break;
        }
    }

    /* word_count is in units of 16-bit integers, do not
     * count the word_count value itself.
     */
    *word_count = ((dp - data) - 1) / 2;

    return dp;
}

/*
 * Encode a CIP-style path.  This is not complete.
 *
 * The path ends up in memory looking like this:
 *
 * Byte     Meaning
 *   0      path size in 16-bit words
 *   1      reserved/padding byte, always 0.
 * 2-n      encoded path.
 *
 * The routine returns a pointer to the byte after the encoded path.
 */

int cip_encode_path(int debug, uint8_t *data, const char *path, int cpu_type)
{
    uint8_t *path_size = data;
    int ioi_size = 0;
    int link_index = 0;
    int last_is_dhp = 0;
    int has_dhp = 0;
    char dhp_channel;
    int src_addr = 0, dest_addr = 0;
    int tmp = 0;
    char **links = NULL;
    char *link = NULL;

    /* skip the path size*/
    data[0] = 0;
    //data[1] = 0;
    data++;

    if(path) {
        /* split the path */
        links = str_split(path, ",");

        if (links == NULL) {
            pdebug(debug, "Bad path format!");
            return PLCTAG_ERR_BAD_PARAM;
        }

        /* work along each string. */
        link = links[link_index];

        while (link && ioi_size < (MAX_CONN_PATH - 2)) { /* MAGIC -2 to allow for padding */
            if (sscanf(link, "%c:%d:%d", &dhp_channel, &src_addr, &dest_addr) == 3) {
                /* DHP link */
                switch (dhp_channel) {
                case 'a':
                case 'A':
                case '2':
                    dhp_channel = 1;
                    break;

                case 'b':
                case 'B':
                case '3':
                    dhp_channel = 2;
                    break;

                default:

                    /* unknown port! */
                    if (links)
                        mem_free(links);

                    pdebug(debug, "Bad DHP format!");

                    return PLCTAG_ERR_BAD_PARAM;
                    break;
                }

                last_is_dhp = 1;
                has_dhp = 1;
            } else {
                last_is_dhp = 0;

                if (str_to_int(link, &tmp) != 0) {
                    if (links)
                        mem_free(links);

                    pdebug(debug, "Bad path element format!");

                    return PLCTAG_ERR_BAD_PARAM;
                }

                *data = tmp;

                /*printf("convert_links() link(%d)=%s (%d)\n",i,*links,tmp);*/

                data++;
                ioi_size++;
            }

            /* FIXME - handle case where IP address is in path */

            link_index++;
            link = links[link_index];
        }

        /* we do not need the split string anymore. */
        if (links) {
            mem_free(links);
            links = NULL;
        }
    } /* else no path such as for PLC5 or Micro 850 */
    
    /* 
     * Add to the path based on the protocol type and
     * whether the last part is DH+.  Only some combinations of
     * DH+ and PLC type work.
     */
    if (last_is_dhp && cpu_type == PCCC_DHP_TAG) {
        /* We have to make the difference from the more
         * generic case.
         */
        /* try adding this onto the end of the path */
        *data = 0x20;
        data++;
        *data = 0xA6;
        data++;
        *data = 0x24;
        data++;
        *data = dhp_channel;
        data++;
        *data = 0x2C;
        data++;
        *data = 0x01;
        data++;
        ioi_size += 6;
    } else {
        /* add the standard message router path */
        *data = 0x20; /* class */
        data++;
        *data = 0x02; /* message router */
        data++;
        *data = 0x24; /* instance */
        data++;
        *data = 0x01; /* instance #1 */
        data++;
        ioi_size += 4;
    }

    if (has_dhp && cpu_type != PCCC_DHP_TAG) {
        /* illegal! */
        pdebug(debug, "Path includes PLC5-specific DH+ element for non-PLC5 CPU!");
        return PLCTAG_ERR_BAD_PARAM;
    }

    /*
     * zero out the last byte if we need to.
     * This pads out the path to a multiple of 16-bit
     * words.
     */
    if (ioi_size & 0x01) {
        *data = 0;
        data++;
        ioi_size++;
    }

    *path_size = ioi_size / 2;

    return PLCTAG_STATUS_OK;
}


/*
 * determine the cpu type based on the attributes passed.  There are many variations
 * that are similar.  There are three fundamental CPU type: Logix-class, MicroLogix-class, and
 * PLC5-class.  Other CPUs either fit into this such as SLC or are not supported such as PLC3.
 */
        
int get_cpu_type(plc_tag tag)
{
    const char* cpu_type = attr_get_str(tag->attribs, "cpu", "NONE");

    if (!str_cmp_i(cpu_type, "plc") || !str_cmp_i(cpu_type, "plc5") || !str_cmp_i(cpu_type, "slc") ||
        !str_cmp_i(cpu_type, "slc500")) {
        return CPU_PLC5;
    } else if (!str_cmp_i(cpu_type, "micrologix") || !str_cmp_i(cpu_type, "mlgx")) {
        return CPU_MLGX;
    } else if (!str_cmp_i(cpu_type, "micro800") || !str_cmp_i(cpu_type, "m800")) {
        return CPU_M800;
    } else if (!str_cmp_i(cpu_type, "compactlogix") || !str_cmp_i(cpu_type, "clgx") || !str_cmp_i(cpu_type, "lgx") ||
               !str_cmp_i(cpu_type, "controllogix") || !str_cmp_i(cpu_type, "contrologix") ||
               !str_cmp_i(cpu_type, "flexlogix") || !str_cmp_i(cpu_type, "flgx")) {
        return CPU_LGX;
    } else {
        return PLCTAG_ERR_BAD_DEVICE;
    }
}


