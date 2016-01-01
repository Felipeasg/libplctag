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

#include <stdio.h>
#include <ab/ab.h>
#include <ab/common.h>
#include <ab/explicit/explicit.h>
#include <util/platform_ext.h>


static int determine_tag_type(plc_tag tag);
int check_dhp(attr attribs);
int check_implicit(attr attribs);
int check_read_group(attr attribs);


int ab_tag_create(plc_tag tag)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = tag->debug;
    
    pdebug(debug, "Starting");
    
    /* determine what kind of tag this is and dispatch */
    switch(determine_tag_type(tag)) {
        case GROUP_TAG:
            //group_tag_create(tag);
            break;
        case EXPLICIT_TAG:
            pdebug(debug, "Creating explicit tag.");
            rc = explicit_tag_create(tag);
            break;
        case IMPLICIT_TAG:
            //implicit_tag_create(tag);
            break;
        case PCCC_TAG:
            //pccc_tag_create(tag);
            break;
        case PCCC_DHP_TAG:
            //pccc_dhp_tag_create(tag);
            break;
        default:
            spin_block(tag->lock) {
                tag->status = PLCTAG_ERR_BAD_PARAM;
                tag->backend_started = 0;
            }
            break;
    }
    
    pdebug(debug, "Done");
    
    return rc;
}






/***************************************************************************************
***************************** Helper Functions *****************************************
***************************************************************************************/		
        
        


/*
 * Determine the tag sub-type.  All these tags are AB tags, but there are
 * several different kinds supported by the library.  These all need to be
 * handled differently.
 */

int determine_tag_type(plc_tag tag)
{
    int cpu_type;
    int uses_dhp = 0;
    int is_implicit = 0;
    int is_group = 0;
    
    cpu_type = get_cpu_type(tag);
    
    uses_dhp = check_dhp(tag->attribs);
    
    is_implicit = check_implicit(tag->attribs);
    
    is_group = check_read_group(tag->attribs);
    
    switch(cpu_type) {
        case CPU_PLC5:
            if(is_implicit || is_group) {
                return PLCTAG_ERR_BAD_PARAM;
            }
            
            if(uses_dhp) {
                return PCCC_DHP_TAG;
            } else {
                return PCCC_TAG;
            }
            break;
            
        case CPU_MLGX:
            if(is_implicit || is_group || uses_dhp) {
                return PLCTAG_ERR_BAD_PARAM;
            }
            
            return PCCC_TAG;
            break;
            
        case CPU_LGX:
            if(is_implicit && is_group) {
                return PLCTAG_ERR_BAD_PARAM;
            }
            
            if(is_group) {
                return GROUP_TAG;
            }
            
            if(is_implicit) {
                return IMPLICIT_TAG;
            } else {
                return EXPLICIT_TAG;
            }
            break;
            
        default:
            return PLCTAG_ERR_BAD_PARAM;
            break;
    }
}



/*
 * Determine if the tag uses DH+ at the end of the path.  This is used elsewhere
 * in conjunction with the check_cpu() function to determine if the PCCC-bridge-over-DH+
 * special case code needs to be used.
 */

int check_dhp(attr attribs)
{
    const char *cur;
    const char *last_comma = NULL;
    
    /* don't rely on POSIX functions here */
    cur = attr_get_str(attribs,"path",""); 
    
    while(*cur) {
        if(*cur == ',') {
            last_comma = cur;
        }
        
        cur++;
    }
    
    if(last_comma) {
        char dhp_channel;
        int src_addr;
        int dest_addr;
        
        if(sscanf(++last_comma,"%c:%d:%d",&dhp_channel,&src_addr,&dest_addr) == 3) {
            return 1;
        }
    }
    
    return 0;
}



/*
 * determine if the tag uses implicit or explicit messaging.  Explicit is TCP
 * and implicit is UDP/multicast (that's a bit simplistic, but close enough).
 * Implicit messaging only allows read-only tags.
 * 
 * The determination is made by checking for a specific protocol type: ab_io.
 */

int check_implicit(attr attribs)
{
    const char *protocol = attr_get_str(attribs,"protocol","ERROR");
    
    return (str_cmp_i(protocol,"ab_io") == 0);
}



/*
 * determine if the tag in question is part of a group.
 */
 
int check_read_group(attr attribs)
{
    const char *group = attr_get_str(attribs,"read_group",NULL);
    
    return (group != NULL);
}

