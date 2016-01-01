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



/* Forward Open Request */
START_PACK typedef struct {
	/* encap header */
	uint16_t encap_command;    /* ALWAYS 0x006f Unconnected Send*/
	uint16_t encap_length;   /* packet size in bytes - 24 */
	uint32_t encap_session_handle;  /* from session set up */
	uint32_t encap_status;          /* always _sent_ as 0 */
	uint64_t encap_sender_context;  /* whatever we want to set this to, used for
                                     * identifying responses when more than one
                                     * are in flight at once.
                                     */
	uint32_t encap_options;         /* 0, reserved for future use */

	/* Interface Handle etc. */
	uint32_t interface_handle;      /* ALWAYS 0 */
	uint16_t router_timeout;        /* in seconds */

	/* Common Packet Format - CPF Unconnected */
	uint16_t cpf_item_count;        /* ALWAYS 2 */
	uint16_t cpf_nai_item_type;     /* ALWAYS 0 */
	uint16_t cpf_nai_item_length;   /* ALWAYS 0 */
	uint16_t cpf_udi_item_type;     /* ALWAYS 0x00B2 - Unconnected Data Item */
	uint16_t cpf_udi_item_length;   /* REQ: fill in with length of remaining data. */

	/* CM Service Request - Connection Manager */
	uint8_t cm_service_code;        /* ALWAYS 0x54 Forward Open Request */
	uint8_t cm_req_path_size;       /* ALWAYS 2, size in words of path, next field */
	uint8_t cm_req_path[4];         /* ALWAYS 0x20,0x06,0x24,0x01 for CM, instance 1*/

	/* Forward Open Params */
	uint8_t secs_per_tick;       	/* seconds per tick */
	uint8_t timeout_ticks;       	/* timeout = srd_secs_per_tick * src_timeout_ticks */
	uint32_t orig_to_targ_conn_id;  /* 0, returned by target in reply. */
	uint32_t targ_to_orig_conn_id;  /* what is _our_ ID for this connection, use ab_connection ptr as id ? */
	uint16_t conn_serial_number;    /* our connection serial number ?? */
	uint16_t orig_vendor_id;        /* our unique vendor ID */
	uint32_t orig_serial_number;    /* our unique serial number */
	uint8_t conn_timeout_multiplier;/* timeout = mult * RPI */
	uint8_t reserved[3];            /* reserved, set to 0 */
	uint32_t orig_to_targ_rpi;      /* us to target RPI - Request Packet Interval in microseconds */
	uint16_t orig_to_targ_conn_params; /* some sort of identifier of what kind of PLC we are??? */
	uint32_t targ_to_orig_rpi;      /* target to us RPI, in microseconds */
	uint16_t targ_to_orig_conn_params; /* some sort of identifier of what kind of PLC the target is ??? */
	uint8_t transport_class;        /* ALWAYS 0xA3, server transport, class 3, application trigger */
	/*uint8_t path_size;*/              /* size of connection path in 16-bit words
                                     * connection path from MSG instruction.
                                     *
                                     * EG LGX with 1756-ENBT and CPU in slot 0 would be:
                                     * 0x01 - backplane port of 1756-ENBT
                                     * 0x00 - slot 0 for CPU
                                     * 0x20 - class
                                     * 0x02 - MR Message Router
                                     * 0x24 - instance
                                     * 0x01 - instance #1.
                                     */

	//uint8_t conn_path[ZLA_SIZE];    /* connection path as above */
} END_PACK eip_forward_open_request_t;



/* Forward Open Response */
START_PACK typedef struct {
	/* encap header */
	uint16_t encap_command;    /* ALWAYS 0x006f Unconnected Send*/
	uint16_t encap_length;   /* packet size in bytes - 24 */
	uint32_t encap_session_handle;  /* from session set up */
	uint32_t encap_status;          /* always _sent_ as 0 */
	uint64_t encap_sender_context;/* whatever we want to set this to, used for
                                     * identifying responses when more than one
                                     * are in flight at once.
                                     */
	uint32_t options;               /* 0, reserved for future use */

	/* Interface Handle etc. */
	uint32_t interface_handle;      /* ALWAYS 0 */
	uint16_t router_timeout;        /* in seconds */

	/* Common Packet Format - CPF Unconnected */
	uint16_t cpf_item_count;        /* ALWAYS 2 */
	uint16_t cpf_nai_item_type;     /* ALWAYS 0 */
	uint16_t cpf_nai_item_length;   /* ALWAYS 0 */
	uint16_t cpf_udi_item_type;     /* ALWAYS 0x00B2 - Unconnected Data Item */
	uint16_t cpf_udi_item_length;   /* REQ: fill in with length of remaining data. */

	/* Forward Open Reply */
	uint8_t resp_service_code;      /* returned as 0xD4 */
	uint8_t reserved1;               /* returned as 0x00? */
	uint8_t general_status;         /* 0 on success */
	uint8_t status_size;            /* number of 16-bit words of extra status, 0 if success */
	uint32_t orig_to_targ_conn_id;  /* target's connection ID for us, save this. */
	uint32_t targ_to_orig_conn_id;  /* our connection ID back for reference */
	uint16_t conn_serial_number;    /* our connection serial number from request */
	uint16_t orig_vendor_id;        /* our unique vendor ID from request*/
	uint32_t orig_serial_number;    /* our unique serial number from request*/
	uint32_t orig_to_targ_api;      /* Actual packet interval, microsecs */
	uint32_t targ_to_orig_api;      /* Actual packet interval, microsecs */
	uint8_t app_data_size;          /* size in 16-bit words of send_data at end */
	uint8_t reserved2;
	uint8_t app_data[ZLA_SIZE];
} END_PACK eip_forward_open_response_t;



static int connection_handler(tasklet_p me);
static ab_connection_p find_connection_unsafe(ab_session_p session, const char *path);
static void connection_add_unsafe(ab_session_p session, ab_connection_p connection);
static void connection_remove(ab_session_p session, ab_connection_p connection);
static int build_forward_open_request(ab_connection_p connection);


#define ERROR_STATE     (0)
#define START_STATE     (1)
#define BUILD_FO_STATE  (2)
#define FO_WAIT_STATE   (3)
#define IDLE_STATE      (4)


/*
 * Public routines
 */
 
ab_connection_p connection_find_or_add(ab_session_p session, plc_tag tag)
{
    int debug = tag->debug;
    const char* path = attr_get_str(tag->attribs, "path", "");
    ab_connection_p connection;

    pdebug(debug, "Starting.");

    /* 
     * lock the session while this is happening because we do not
     * want a race condition where two tags try to create the same
     * connection at the same time.
     * 
     * FIXME - this is a very long time to hold a spin lock.
     */

    spin_block(session->lock) {
        connection = find_connection_unsafe(session, path);

        if (!connection) {
            connection = (ab_connection_p)mem_alloc(sizeof(struct ab_connection_t));
            
            if(connection) {
                tasklet_p connection_tasklet = NULL;
                
                connection->debug = debug;
                connection->session = session;
                connection->conn_seq_num = 1;
                connection->orig_connection_id = ++session->conn_serial_number;
                connection->status = PLCTAG_STATUS_PENDING;
                connection->state = START_STATE;
                connection->cpu_type = get_cpu_type(tag);
                
                connection->tag_count = 1;

                /* only copy the path if there is one */
                if(path) {
                    connection->path = str_dup(path);
                }
                                
                connection_tasklet = tasklet_create(connection_handler, connection);
                
                if(!connection_tasklet) {
                    /* this is bad */
                    if(connection->path) {
                        mem_free(connection->path);
                        connection->path = NULL;
                    }

                    // FIXME - what else do we need to free?
                    mem_free(connection);                    
                    
                    pdebug(debug, "Unable to create connection tasklet!");
                    
                    return NULL;
                }

                /* add the connection to the session */
                connection_add_unsafe(session, connection);
            }
        } else {
            /* found a connection, nothing more to do. */
            connection_inc_tag_count(connection);

            pdebug(debug, "find_or_create_connection() reusing existing connection.");
        }
    }
 
    if (!connection) {
        pdebug(debug, "unable to create or find a connection!");
    } 
    
    pdebug(debug, "Done.");

    return connection;
}





/*
 * Helper Functions
 */

/* called while session spin lock is held */
ab_connection_p find_connection_unsafe(ab_session_p session, const char *path)
{
    ab_connection_p tmp = session->connections;
    
    while(tmp && str_cmp(tmp->path,path)) {
        tmp = tmp->next;
    }
    
    return tmp;
}




int connection_handler(tasklet_p me)
{
    ab_connection_p connection = (ab_connection_p)(me->data);
    int debug = connection->debug;
    int rc = PLCTAG_STATUS_OK;
    
    pdebug(debug, "Starting");
    
    switch(connection->state) {
        case START_STATE:
            pdebug(1, "START_STATE");
            
            /* wait for the session to be ready */
            if(connection->session->status == PLCTAG_STATUS_OK) {
                connection->state = BUILD_FO_STATE;
            }
            
            return TASKLET_RESCHED;
            
            break;
            
        case BUILD_FO_STATE:
            pdebug(1, "BUILD_FO_STATE");

            rc = build_forward_open_request(connection);
            
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(debug, "Unable to build forward open request!");
                
                connection->state = ERROR_STATE;
            } else {            
                connection->state = FO_WAIT_STATE;
            }
            
            return TASKLET_RESCHED;
            
            break;
            
        case FO_WAIT_STATE:
            pdebug(1, "FO_WAIT_STATE");
            
            /* check the connection's fo request */
            if(connection->fo_request->resp_received) {
                eip_forward_open_response_t *fo_resp;

                fo_resp = (eip_forward_open_response_t*)(connection->fo_request->data);

                if(le2h16(fo_resp->encap_command) != AB_EIP_READ_RR_DATA) {
                    pdebug(debug,"Unexpected EIP packet type received: %d!",fo_resp->encap_command);

                    connection->status = PLCTAG_ERR_BAD_DATA;
                    connection->state = ERROR_STATE;
                } else if(le2h16(fo_resp->encap_status) != AB_EIP_OK) {
                    pdebug(debug,"EIP command failed, response code: %d",fo_resp->encap_status);

                    connection->status = PLCTAG_ERR_REMOTE_ERR;
                    connection->state = ERROR_STATE;
                } else if(fo_resp->general_status != AB_EIP_OK) {
                    pdebug(debug,"Forward Open command failed, response code: %d",fo_resp->general_status);
                    connection->status = PLCTAG_ERR_REMOTE_ERR;
                    connection->state = ERROR_STATE;
                } else {
                    pdebug(debug,"Connection set up succeeded.");

                    connection->orig_connection_id = le2h32(fo_resp->orig_to_targ_conn_id);
                    //connection->targ_connection_id = le2h32(fo_resp->targ_to_orig_conn_id);
                    connection->status = PLCTAG_STATUS_OK;
                    connection->state = IDLE_STATE;
                    
                    /* clean up the request */
                    request_remove(connection->fo_request->session, connection->fo_request);
                    request_destroy(&connection->fo_request);
                }
            }
            
            return TASKLET_RESCHED;
            
            break;
            
        case IDLE_STATE:
            pdebug(1, "IDLE_STATE");
            
            // FIXME - check tag count
            
            return TASKLET_RESCHED;
            
            break;
            
        case ERROR_STATE:
            pdebug(1, "ERROR_STATE");
            
            // FIXME - handle errors
            
            return TASKLET_RESCHED;
            
            break;
            
        default:
            pdebug(debug, "Unknown state!");
            
            return TASKLET_RESCHED;
            
            break;
    }
}



int build_forward_open_request(ab_connection_p connection)
{
    ab_request_p req;
    int rc = PLCTAG_STATUS_OK;
    int debug = connection->debug;
    eip_forward_open_request_t *fo = NULL;
    uint8_t *data = NULL;
    uint16_t conn_params = 0;
    uint8_t conn_path[MAX_CONN_PATH];
    uint8_t conn_path_size = 0;
    int cpu_type = 0;

    pdebug(debug, "Starting.");
    
    /* 
     * Determine the right param for the connection.
     * This sets up the packet size, among other things.
     */
    switch(connection->cpu_type) {
        case CPU_PLC5:
        case CPU_MLGX:
            conn_params = AB_EIP_PLC5_PARAM;
            break;
            
        case CPU_LGX:
            conn_params = AB_EIP_LGX_PARAM;
            break;
            
        case CPU_M800:
            conn_params = AB_EIP_LGX_PARAM;
            break;
            
        default:
            pdebug(debug,"Unknown protocol/cpu type!");
            return PLCTAG_ERR_BAD_PARAM;
            break;
    }
    
    pdebug(debug, "conn_params=%x", conn_params);

    /* encode data from the tag, if any */
    rc = cip_encode_path(debug, &conn_path[0], connection->path, cpu_type);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(debug, "Unable to parse connection path!");
        return rc;
    }
    
    /* get the path size */
    conn_path_size = (conn_path[0] * 2) + 1; /* 
                                              * size is in words,
                                              * plus one for the count
                                              */
    
    pdebug(debug, "conn path size = %d", conn_path_size);
    
    for(int i=0; i < conn_path_size; i++) {
        pdebug(debug, "conn_path[%d]=%x",i,conn_path[i]);
    }
    
    
    /* get a request buffer */
    rc = request_create(&req);
    
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(debug,"Unable to get new request.  rc=%d",rc);
        return rc;
    }

    req->debug = debug;

    fo = (eip_forward_open_request_t*)(req->data);
    
    
    pdebug(1, "sizeof(eip_forward_open_request_t)=%d",sizeof(eip_forward_open_request_t));

    /* point to the end of the struct */
    data = (req->data) + sizeof(eip_forward_open_request_t);

    /* set up the path information. */
    mem_copy(data, &conn_path[0], conn_path_size);
    data += conn_path_size;

    /* fill in the static parts */

    /* encap header parts */
    fo->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* 0x006F EIP Send RR Data command */
    fo->encap_length =
        h2le16(data - (uint8_t*)(&fo->interface_handle)); /* total length of packet except for encap header */
    
    /* get a special session sequence ID, used to identify unconnected packets. */
    spin_block(connection->session->lock) {
        fo->encap_sender_context = (++ connection->session->session_seq_id);
    }
    
    fo->router_timeout = h2le16(1);                       /* one second is enough ? */

    /* CPF parts */
    fo->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    fo->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* null address item type */
    fo->cpf_nai_item_length = h2le16(0);             /* no data, zero length */
    fo->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* unconnected data item, 0x00B2 */
    fo->cpf_udi_item_length =
        h2le16(data - (uint8_t*)(&fo->cm_service_code)); /* length of remaining data in UC data item */

    /* Connection Manager parts */
    fo->cm_service_code = AB_EIP_CMD_FORWARD_OPEN; /* 0x54 Forward Open Request */
    fo->cm_req_path_size = 2;                      /* size of path in 16-bit words */
    fo->cm_req_path[0] = 0x20;                     /* class */
    fo->cm_req_path[1] = 0x06;                     /* CM class */
    fo->cm_req_path[2] = 0x24;                     /* instance */
    fo->cm_req_path[3] = 0x01;                     /* instance 1 */

    /* Forward Open Params */
    fo->secs_per_tick = AB_EIP_SECS_PER_TICK;         /* seconds per tick, no used? */
    fo->timeout_ticks = AB_EIP_TIMEOUT_TICKS;         /* timeout = srd_secs_per_tick * src_timeout_ticks, not used? */
    fo->orig_to_targ_conn_id = h2le32(0);             /* is this right?  Our connection id or the other machines? */
    fo->targ_to_orig_conn_id = h2le32(connection->orig_connection_id); /* connection id in the other direction. */
    fo->conn_serial_number = h2le16((uint16_t)(intptr_t)(connection)); /* our connection SEQUENCE number. */
    fo->orig_vendor_id = h2le16(AB_EIP_VENDOR_ID);               /* our unique :-) vendor ID */
    fo->orig_serial_number = h2le32(AB_EIP_VENDOR_SN);           /* our serial number. */
    fo->conn_timeout_multiplier = AB_EIP_TIMEOUT_MULTIPLIER;     /* timeout = mult * RPI */
    fo->orig_to_targ_rpi = h2le32(AB_EIP_RPI); /* us to target RPI - Request Packet Interval in microseconds */
    fo->orig_to_targ_conn_params = h2le16(conn_params); /* packet size and some other things, based on protocol/cpu type */
    fo->targ_to_orig_rpi = h2le32(AB_EIP_RPI); /* target to us RPI - not really used for explicit messages? */
    fo->targ_to_orig_conn_params = h2le16(conn_params); /* packet size and some other things, based on protocol/cpu type */
    fo->transport_class = AB_EIP_TRANSPORT_CLASS_T3; /* 0xA3, server transport, class 3, application trigger */
    //fo->path_size = connection->conn_path_size/2; /* size in 16-bit words */

    /* set the size of the request */
    req->request_size = data - (req->data);

    /* mark it as ready to send */
    req->send_request = 1;

    /* add the request to the session's list. */
    rc = request_add(connection->session, req);
    
    if(rc == PLCTAG_STATUS_OK) {
        connection->fo_request = req;
    }

    pdebug(debug, "Done");

    return rc;
}

/*
 * add a connection to the list in a session.  The session lock MUST be held
 * before calling this!
 */
void connection_add_unsafe(ab_session_p session, ab_connection_p connection)
{
    connection->next = session->connections;
    session->connections = connection;
}


void connection_remove(ab_session_p session, ab_connection_p connection)
{
    ab_connection_p prev, cur;
    
    spin_block(session->lock) {
        prev = NULL;
        cur = session->connections;
        
        while(cur && cur != connection) {
            prev = cur;
            cur = cur->next;
        }
        
        if(cur == connection) {
            if(!prev) {
                /* first in the list */
                session->connections = cur->next;
            } else {
                prev->next = cur->next;
            }
        }
    }
}


void connection_inc_tag_count(ab_connection_p connection)
{
    if(!connection) {
        return;
    }
    
    spin_block(connection->lock) {
        connection->tag_count++;
    }
}

void connection_dec_tag_count(ab_connection_p connection)
{
    if(!connection) {
        return;
    }
    
    spin_block(connection->lock) {
        connection->tag_count--;
    }
}

