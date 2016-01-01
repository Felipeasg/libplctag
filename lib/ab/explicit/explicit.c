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


#include <libplctag.h>
#include <libplctag_tag.h>
#include <platform.h>
#include <util/platform_ext.h>
#include <ab/common.h>
#include <ab/session.h>
#include <ab/connection.h>
#include <ab/explicit/explicit.h>



/* struct to maintain tasklet state */
struct tag_context_t {
    plc_tag tag;
    
    ab_session_p session;
    ab_connection_p connection;
    
    uint8_t *read_template;
    int read_template_size;
    uint8_t *read_conn_seq_num;
    
    uint8_t *write_template;
    int write_template_size;
    uint8_t *write_conn_seq_num;

    int state;
};
typedef struct tag_context_t *tag_context_p;



/* States for the handler */
#define ERROR_STATE     (0)
#define IDLE_STATE      (1)
#define DESTROY_STATE   (2)




/* helper functions */
static void cleanup_context(tag_context_p context);
static int tag_handler(tasklet_p me);



//static int build_read_request(plc_tag tag, tag_context_p context);


/*
 * Create a CIP-native explicit tag.  This uses unconnected messaging via TCP.
 */

int explicit_tag_create(plc_tag tag)
{
    tag_context_p context = NULL;
    tasklet_p tag_tasklet = NULL;
    int debug = tag->debug;
    
    pdebug(debug, "Starting");
    
    /* allocate a new back end tag */
    context = (tag_context_p)mem_alloc(sizeof(struct tag_context_t));
    
    if(!context) {
        pdebug(debug, "Unable to allocate memory for new explicit tag context!");
        
        spin_block(tag->lock) {
            tag->status = PLCTAG_ERR_NO_MEM;
            tag->backend_started = 0;
        }
        
        return PLCTAG_ERR_NO_MEM;
    }
    
    /* get a session */
    context->session = session_find_or_add(tag);

    if(!context->session) {
        pdebug(debug, "Unable to get session!");
        
        cleanup_context(context);
        
        spin_block(tag->lock) {
            tag->status = PLCTAG_ERR_CREATE;
            tag->backend_started = 0;
        }
        
        return PLCTAG_ERR_CREATE;
    }
    
    context->connection = connection_find_or_add(context->session, tag);
    
    if(!context->connection) {
        pdebug(debug, "Unable to get connection!");
        
        cleanup_context(context);
        
        spin_block(tag->lock) {
            tag->status = PLCTAG_ERR_CREATE;
            tag->backend_started = 0;
        }
        
        return PLCTAG_ERR_CREATE;
    }


    /* tie the two tags together */
    tag->impl_data = context;
     
    /* set up the first state */
    context->state = IDLE_STATE;
    
    pdebug(debug, "Step 1");
    
    /* create a tasklet to handle the back end processing.  This starts the tag tasklet. */		
    tag_tasklet = tasklet_create(tag_handler, tag);
    
    if(!tag_tasklet) {
        pdebug(debug,"Unable to create explicit tag tasklet!");
        
        cleanup_context(context);

        spin_block(tag->lock) {
            tag->status = PLCTAG_ERR_CREATE;
            tag->backend_started = 0;
        }

        return PLCTAG_ERR_CREATE;
    } 
    
    pdebug(1, "tag handler tasklet=%p", tag_tasklet);
    
    /* let the front end know that the back end is running */
    tag->backend_started = 1;
    
    pdebug(debug, "Done");
    
    return PLCTAG_STATUS_OK;
}




/*****************************************************************
*********************** Support Functions ************************
*****************************************************************/



void cleanup_context(tag_context_p context) 
{
    if(context) {
        if(context->connection) {
            connection_dec_tag_count(context->connection);
            context->connection = NULL;
        }
        
        if(context->session) {
            session_dec_tag_count(context->session);
            context->session = NULL;
        }
        
        if(context->read_template) {
            mem_free(context->read_template);
            context->read_template = NULL;
        }
        
        if(context->write_template) {
            mem_free(context->write_template);
            context->write_template = NULL;
        }
        
        /* clear the back end up */
        if(context->tag) {
            context->tag->impl_data = NULL;
            context->tag->backend_started = 0;
        }
        
        mem_free(context);
    }
}



/*
 * tag_handler
 * 
 * This is the entry point for the tasklet runner for this tasklet.
 */
 
int tag_handler(tasklet_p me)
{
    plc_tag tag = (plc_tag)(me->data);
    tag_context_p context = (tag_context_p)(tag->impl_data);
    int debug = tag->debug;
    int read_requested;
    int write_requested;
    int abort_requested;
    int destroy_requested;

    pdebug(1, "Starting");
   
    switch(context->state) {
            
        case IDLE_STATE:
            pdebug(debug, "tag tasklet idling");
            
            spin_block(tag->lock) {
                read_requested = tag->read_requested;
                write_requested = tag->write_requested;
                abort_requested = tag->abort_requested;
                destroy_requested = tag->destroy_requested;
            }
            
            if(destroy_requested) {
                context->state = DESTROY_STATE;
            } else if(abort_requested) {
                // FIXME - do abort stuff
            } else if(write_requested) {
                // FIXME - do write stuff
            } else if(read_requested) {
                // FIXME - do read stuff
            }
            
            return TASKLET_RESCHED;
            
            break;
            
        case DESTROY_STATE:
            /* we need to do all the deallocation here */
            plc_tag_destroy_generic(tag);
            
            /* free up the local resources */
            connection_dec_tag_count(context->connection);
            session_dec_tag_count(context->session);
            
            mem_free(context->read_template);
            mem_free(context->write_template);
            
            /* final clean up */
            mem_free(context);
            
            /* tell the tasklet system to take us out of the list */
            return TASKLET_DONE;
            
            break;
            
        case ERROR_STATE:
            pdebug(debug, "tag tasklet error state");
            
            return TASKLET_RESCHED;
            
            break;
            
        default:
            pdebug(debug, "Unknown state %d!", context->state);
            break;
    }

    return TASKLET_RESCHED;
}



/******************************************************************************
************************* Helper Functions ************************************
******************************************************************************/

int build_read_request(tag_context_p context)
{
    ab_session_p session = context->session;
    plc_tag tag = context->tag;
    int debug = tag->debug;
    eip_cip_co_req* cip;
    uint8_t* data;
    uint8_t* embed_start, *embed_end;
    int rc;
    int elem_count = attr_get_int(tag->attribs,"elem_count",1);
    uint8_t request_buf[MAX_PACKET_SIZE];

    pdebug(debug, "Starting.");
    
    pdebug(debug, "Done");

    return PLCTAG_STATUS_OK;
}


int build_write_request(tag_context_p context)
{
    ab_session_p session = context->session;
    plc_tag tag = context->tag;
    int debug = tag->debug;
    eip_cip_co_req* cip;
    uint8_t* data;
    uint8_t* embed_start, *embed_end;
    int rc;
    int elem_count = attr_get_int(tag->attribs,"elem_count",1);
    uint8_t request_buf[MAX_PACKET_SIZE];

    pdebug(debug, "Starting.");
    
    pdebug(debug, "Done");

    return PLCTAG_STATUS_OK;
}



