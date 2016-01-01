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

/* PACKET DEFINITION */

/* Session Registration Request/Response */
START_PACK typedef struct
{
    /* encap header */
    uint16_t encap_command;        /* ALWAYS 0x0065 Register Session*/
    uint16_t encap_length;         /* packet size in bytes - 24 */
    uint32_t encap_session_handle; /* from session set up */
    uint32_t encap_status;         /* always _sent_ as 0 */
    uint64_t encap_sender_context; /* whatever we want to set this to, used for
                                * identifying responses when more than one
                                * are in flight at once.
                                */
    uint32_t encap_options; /* 0, reserved for future use */

    /* session registration request */
    uint16_t eip_version;
    uint16_t option_flags;
} END_PACK eip_session_reg_req;

/* handler states */
#define ERROR_STATE (0)
#define START_STATE (1)
#define IDLE_STATE (2)
#define CLOSE_STATE (3)

static lock_t session_list_lock = LOCK_INIT;
static volatile ab_session_p session_list = NULL;
/*static volatile uint32_t session_handle = 0;*/

/* declare helper functions */
static int session_handler(tasklet_p me);
static int do_io(ab_session_p session);
static int open_session_socket(ab_session_p session);
static int build_session_request(ab_session_p session);
static int session_check_incoming_data(ab_session_p session);
static int request_check_outgoing_data(ab_session_p session, ab_request_p req);
static int recv_eip_response(ab_session_p session);
static int send_eip_request(ab_request_p req);

/*
 * Externally visible functions
 */

/*
 * find or create a new session that matches the passed tag info.  If one is
 * created, then the owner is set.  Other tasklets/tags will not try to set up
 * the session, just the one that created it.
 */

ab_session_p session_find_or_add(plc_tag tag)
{
    ab_session_p session = NULL;
    const char *gateway = get_gateway(tag);
    int port = attr_get_int(tag->attribs, "port", AB_EIP_DEFAULT_PORT);
    int debug = tag->debug;

    pdebug(debug, "Starting");

    /* lock the session list lock */
    /* FIXME - this is a lot of spinning.  Could be a problematic if a lot of tags are created
     * and destroyed.  Should be a mutex?
     */
    spin_block(session_list_lock)
    {
        session = session_list;

        while (session && (str_cmp_i(gateway, session->host) != 0) && (port != session->port)) {
            session = session->next;
        }

        if (!session) {
            pdebug(debug, "creating new session.");

            /* not found */
            session = (ab_session_p)mem_alloc(sizeof(struct ab_session_t));

            if (session) {
                tasklet_p session_tasklet = NULL;

                session->state = START_STATE;
                session->status = PLCTAG_STATUS_PENDING;
                str_copy(session->host, gateway, MAX_SESSION_HOST);
                session->port = port;
                session->debug = debug;
                session->tag_count = 1;

                /* fire off the tasklet for this session */
                session_tasklet = tasklet_create(session_handler, session);

                if (!session_tasklet) {
                    /* bad! */
                    mem_free(session);
                    session = NULL;
                } else {
                    /* everything went well, link the session into the list */
                    session->next = session_list;
                    session_list = session;

                    pdebug(1, "Session tasklet=%p", session_tasklet);
                }
            }
        } else {
            pdebug(debug, "reusing existing session.");
            session_inc_tag_count(session);
        }
    }

    pdebug(debug, "Done");

    return session;
}



void session_inc_tag_count(ab_session_p session)
{
    if(!session) {
        return;
    }
    
    spin_block(session->lock) {
        session->tag_count++;
    }
}

void session_dec_tag_count(ab_session_p session)
{
    if(!session) {
        return;
    }
    
    spin_block(session->lock) {
        session->tag_count--;
    }
}





/*
 * Request public routines.
 */


/*
 * request_create
 *
 * This does not do much for now other than allocate memory.  In the future
 * it may be desired to keep a pool of request buffers instead.  This shim
 * is here so that such a change can be done without major code changes
 * elsewhere.
 */
int request_create(ab_request_p *req)
{
    int rc = PLCTAG_STATUS_OK;
    ab_request_p res;

    res = (ab_request_p)mem_alloc(sizeof(struct ab_request_t));

    if (!res) {
        *req = NULL;
        rc = PLCTAG_ERR_NO_MEM;
    } else {
        *req = res;
    }

    return rc;
}

/*
 * request_add
 *
 * Add a request to the session's queue.
 */
int request_add(ab_session_p sess, ab_request_p req)
{
    int rc = PLCTAG_STATUS_OK;
    ab_request_p cur, prev;

    pdebug(sess->debug, "Starting.");

    /* make sure the request points to the session */
    req->session = sess;

    spin_block(sess->lock)
    {
        /* we add the request to the end of the list. */
        cur = sess->requests;
        prev = NULL;

        while (cur) {
            prev = cur;
            cur = cur->next;
        }

        if (!prev) {
            sess->requests = req;
        } else {
            prev->next = req;
        }
    }

    pdebug(sess->debug, "Done.");

    return rc;
}

/*
 * request_remove
 *
 * Remove the request from the session queue.
 */
int request_remove(ab_session_p sess, ab_request_p req)
{
    int rc = PLCTAG_STATUS_OK;
    ab_request_p cur, prev;

    if (sess == NULL || req == NULL) {
        return rc;
    }

    pdebug(sess->debug, "Starting.");

    spin_block(sess->lock)
    {
        /* find the request and remove it from the list. */
        cur = sess->requests;
        prev = NULL;

        while (cur && cur != req) {
            prev = cur;
            cur = cur->next;
        }

        if (cur == req) {
            if (!prev) {
                sess->requests = cur->next;
            } else {
                prev->next = cur->next;
            }
        } /* else not found */
    }

    req->next = NULL;
    req->session = NULL;

    pdebug(sess->debug, "Done.");

    return rc;
}

/*
 * request_destroy
 *
 * The request must be removed from any lists before this!
 */
int request_destroy(ab_request_p *req_pp)
{
    ab_request_p r;
    int debug;

    if (req_pp && *req_pp) {
        r = *req_pp;

        debug = r->debug;

        pdebug(debug, "Starting.");

        //request_remove(r->session, r);
        mem_free(r);
        *req_pp = NULL;

        pdebug(debug, "Done.");
    }

    return PLCTAG_STATUS_OK;
}

/***********************************************************************
 ********************** Session Support Functions **********************
 **********************************************************************/

int session_handler(tasklet_p me)
{
    ab_session_p session = (ab_session_p)(me->data);
    int rc;
    int debug = session->debug;

    pdebug(1, "Starting");

    /* loop.  Sometimes we want to process several states in one step. */
    if (1) {
        switch (session->state) {
        case START_STATE:
            /* open a socket */
            pdebug(debug, "START_STATE");

            rc = open_session_socket(session);

            if (rc != PLCTAG_STATUS_OK) {
                pdebug(debug, "Unable to setup socket for session!");
                session->state = ERROR_STATE;
                return TASKLET_RESCHED;
            }

            /* build the request */
            rc = build_session_request(session);

            if (rc != PLCTAG_STATUS_OK) {
                pdebug(debug, "Unable to build session request!");
                session->status = rc;
                session->state = ERROR_STATE;
                return TASKLET_RESCHED;
            }

            session->state = IDLE_STATE;

            /* don't give up the CPU since we just queued IO */
            // FIXME
            return TASKLET_RESCHED;

            break;

        case IDLE_STATE:
            pdebug(debug, "IDLE_STATE");
            /* if there are no tags attached to this session, try to remove it */
            if (session->tag_count <= 0) {
                session->state = CLOSE_STATE;
                return TASKLET_RESCHED;
            }

            rc = do_io(session);

            if (rc == PLCTAG_ERR_NO_DATA) {
                /* not really an error, just nothing to do */
                return TASKLET_RESCHED;
            }

            if (rc != PLCTAG_STATUS_OK) {
                session->status = rc;
                session->state = ERROR_STATE;
            }

            break;

        case ERROR_STATE:
            /* FIXME - lots more to do here */
            if (session->sock) {
                socket_close(session->sock);
                session->sock = NULL;
            }

            break;

        case CLOSE_STATE:
            /* FIXME - what to do here? */

            pdebug(debug, "Close state unimplemented!");

            break;

        default:
            pdebug(debug, "Unknown state %d!", session->state);
            session->state = ERROR_STATE;
            break;
        }
    }

    return TASKLET_RESCHED;
}

/*
 * open_session_socket
 *
 * Open the TCP socket for a session.
 */

int open_session_socket(ab_session_p session)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = session->debug;

    pdebug(debug, "Starting");

    /* Open a socket for communication with the gateway. */
    rc = socket_create(&(session->sock));

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(debug, "Unable to create socket for session!");
        session->status = rc;
        return rc;
    }

    rc = socket_connect_tcp(session->sock, session->host, session->port);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(debug, "Unable to connect socket for session!");
        session->status = rc;
        return rc;
    }

    return rc;
}

int build_session_request(ab_session_p session)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = session->debug;
    ab_request_p req = NULL;
    eip_session_reg_req *sess_req;

    pdebug(debug, "Starting.");

    /* get a request */
    rc = request_create(&req);

    if (rc != PLCTAG_STATUS_OK) {
        /* oops! */
        pdebug(debug, "Unable to create request status=%d", rc);
        return rc;
    }

    sess_req = (eip_session_reg_req *)(req->data);

    /* fill in the fields of the request */
    sess_req->encap_command = h2le16(AB_EIP_REGISTER_SESSION);
    sess_req->encap_length = h2le16(sizeof(eip_session_reg_req) - sizeof(eip_encap_t));
    sess_req->encap_session_handle = h2le32(0); /*session->session_handle;*/
    sess_req->encap_status = h2le32(0);
    sess_req->encap_sender_context = (uint64_t)0;
    sess_req->encap_options = h2le32(0);

    sess_req->eip_version = h2le16(AB_EIP_VERSION);
    sess_req->option_flags = 0;

    /* set up the request */
    req->request_size = sizeof(eip_session_reg_req);
    req->abort_after_send = 1;
    req->send_request = 1;
    req->debug = session->debug;

    /* add the request to the session's queue */
    rc = request_add(session, req);

    if (rc != PLCTAG_STATUS_OK) {
        mem_free(req);
        pdebug(debug, "Unable to add request to session, status=%d", rc);
        return rc;
    }

    return rc;
}

int do_io(ab_session_p session)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = session->debug;

    /* check for incoming data. */
    rc = session_check_incoming_data(session);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(debug, "Error when checking for incoming session data! %d", rc);

        return rc;
    }

    /* loop over the requests in the session */
    /* FIXME - this might be too much for other requests */
    spin_block(session->lock)
    {
        ab_request_p cur_req = session->requests;
        ab_request_p prev_req = NULL;

        /*pdebug(debug,"checking outstanding requests.");*/

        while (cur_req) {
            /* check for abort before anything else. */
            if (cur_req->abort_request) {
                ab_request_p tmp;

                pdebug(debug, "aborting request %p", cur_req);

                /*
                 * is this in the process of being sent?
                 * if so, abort the abort because otherwise we would send
                 * a partial packet and cause all kinds of problems.
                 */
                if (session->current_request != cur_req) {
                    if (prev_req) {
                        prev_req->next = cur_req->next;
                    } else {
                        /* cur is head of list */
                        session->requests = cur_req->next;
                    }

                    tmp = cur_req;
                    cur_req = cur_req->next;

                    /* free the the request */
                    request_destroy(&tmp);

                    continue;
                }
            }

            rc = request_check_outgoing_data(session, cur_req);

            /* move to the next request */
            prev_req = cur_req;
            cur_req = cur_req->next;
        }
    }

    return rc;
}

int session_check_incoming_data(ab_session_p session)
{
    int rc = PLCTAG_STATUS_OK;
    int debug = session->debug;

    /*
     * if there is no current received sequence ID, then
     * see if we can get some data.
     */

    /*pdebug(session->debug, "Starting.");*/

    if (!session->has_response) {
        rc = recv_eip_response(session);

        /* NO_DATA just means that there was nothing to read yet. */
        if (rc == PLCTAG_ERR_NO_DATA || rc >= 0) {
            rc = PLCTAG_STATUS_OK;
        }

        if (rc != PLCTAG_STATUS_PENDING && rc != PLCTAG_STATUS_OK) {
            /* error! */
            /* FIXME */
        }
    }

    /*
     * we may have read in enough data above to finish off a
     * response packet. If so, process it.
     */
    if (session->has_response) {
        /* check the response for a session registration response. */
        eip_encap_t *resp = (eip_encap_t *)(session->recv_data);

        if (le2h16(resp->encap_command) == AB_EIP_REGISTER_SESSION) {
            if (le2h16(resp->encap_status) != AB_EIP_OK) {
                pdebug(debug, "EIP session registration command failed, response code: %d", resp->encap_status);
                return PLCTAG_ERR_REMOTE_ERR;
            }

            /* after all that, save the session handle, we will
             * use it in future packets.
             */

            pdebug(debug, "EIP Session registration succeeded.");

            spin_block(session->lock) {
                session->session_handle = resp->encap_session_handle; /* opaque to us */
                session->status = PLCTAG_STATUS_OK;
            }
        } else {
            /* find the request for which there is a response pending. */
            spin_block(session->lock)
            {
                ab_request_p tmp = session->requests;

                while (tmp) {
                    eip_encap_t *encap = (eip_encap_t *)(session->recv_data);

                    /*
                     * if this is a connected send response, we can look at the
                     * connection sequence ID and connection ID to see if this
                     * response is for the request.
                     *
                     * FIXME - it appears that PCCC/DH+ requests do not have the cpf_conn_seq_num
                     * field.  Use the PCCC sequence in those cases??  How do we tell?
                     */
                    if (encap->encap_command == AB_EIP_CONNECTED_SEND) {
                        eip_cip_co_req *resp = (eip_cip_co_req *)(session->recv_data);

                        if (resp->cpf_targ_conn_id == tmp->conn_id && resp->cpf_conn_seq_num == tmp->conn_seq) {
                            break;
                        }
                    } else {
                        /*
                         * If we are not using a connected message, then the session context is meaningful and we can
                         * switch on that.
                         */

                        /*pdebug(session->debug,"encap->encap_sender_context=%lu
                         * tmp->session_seq_id=%lu",encap->encap_sender_context,tmp->session_seq_id);*/

                        if (encap->encap_sender_context != 0 && encap->encap_sender_context == tmp->session_seq_id) {
                            break;
                        }
                    }

                    tmp = tmp->next;
                }

                if (tmp) {
                    pdebug(tmp->debug, "got full packet of size %d", session->recv_offset);
                    /*pdebug_dump_bytes(tmp->debug, session->recv_data, session->recv_offset);*/

                    /* copy the data from the session's buffer */
                    mem_copy(tmp->data, session->recv_data, session->recv_offset);

                    tmp->resp_received = 1;
                    tmp->send_in_progress = 0;
                    tmp->send_request = 0;
                    tmp->request_size = session->recv_offset;
                } /*else {

                    pdebug(debug,"Response for unknown request.");
                }*/
            }

            /*
             * if we did not find a request, it may have already been aborted, so
             * just clean up.
             */
        }

        /* reset the session's buffer */
        
        pdebug(1, "clearing buffer");
        
        mem_set(session->recv_data, 0, MAX_REQ_RESP_SIZE);
        session->recv_offset = 0;
        session->resp_seq_id = 0;
        session->has_response = 0;        
    }

    /*pdebug(session->debug, "Done");*/

    return rc;
}

int request_check_outgoing_data(ab_session_p session, ab_request_p req)
{
    int rc = PLCTAG_STATUS_OK;

    /*pdebug(session->debug,"Starting.");*/

    /*
     * Check to see if we can send something.
     */

    if (!session->current_request && req->send_request /*&& session->num_reqs_in_flight < MAX_REQS_IN_FLIGHT*/) {
        /* nothing being sent and this request is outstanding */
        session->current_request = req;

        /*session->num_reqs_in_flight++;
        pdebug(debug,"num_reqs_in_flight=%d",session->num_reqs_in_flight);*/
    }

    /* if we are already sending this request, check its status */
    if (session->current_request == req) {
        /* is the request done? */
        if (req->send_request) {
            /* not done, try sending more */
            send_eip_request(req);
            /* FIXME - handle return code! */
        } else {
            /*
             * done in some manner, remove it from the session to let
             * another request get sent.
             */
            session->current_request = NULL;
        }
    }

    /*pdebug(session->debug,"Done.");*/

    return rc;
}

/* send a request on the socket */
int send_eip_request(ab_request_p req)
{
    int rc;

    pdebug(req->debug, "Starting.");

    /* if we have not already started, then start the send */
    if (!req->send_in_progress) {
        eip_encap_t *encap = (eip_encap_t *)(req->data);
        int payload_size = req->request_size - sizeof(eip_encap_t);

        /* set up the session sequence ID for this transaction */
        if (encap->encap_command == h2le16(AB_EIP_READ_RR_DATA)) {
            uint64_t session_seq_id;

            session_seq_id = req->session->session_seq_id++;

            req->session_seq_id = session_seq_id;
            encap->encap_sender_context = session_seq_id; /* link up the request seq ID and the packet seq ID */
        }

        /* set up the rest of the request */
        req->current_offset = 0; /* nothing written yet */

        /* fill in the header fields. */
        encap->encap_length = h2le16(payload_size);
        encap->encap_session_handle = req->session->session_handle;
        encap->encap_status = h2le32(0);
        encap->encap_options = h2le32(0);

        /* display the data */
        pdebug_dump_bytes(req->debug, req->data, req->request_size);

        req->send_in_progress = 1;
    }

    /* send the packet */
    rc = socket_write(req->session->sock, req->data + req->current_offset, req->request_size - req->current_offset);

    if (rc >= 0) {
        req->current_offset += rc;

        /* are we done? */
        if (req->current_offset >= req->request_size) {
            req->send_request = 0;
            req->send_in_progress = 0;
            req->current_offset = 0;

            /* set this request up for a receive action */
            if (req->abort_after_send) {
                req->abort_request = 1; /* for one shots */
            } else {
                req->recv_in_progress = 1;
            }
        }

        rc = PLCTAG_STATUS_OK;
    } else {
        /* oops, error of some sort. */
        req->status = rc;
        req->send_request = 0;
        req->send_in_progress = 0;
        req->recv_in_progress = 0;
    }

    pdebug(req->debug, "Done.");

    return rc;
}

/*
 * recv_eip_response
 *
 * Look at the passed session and read any data we can
 * to fill in a packet.  If we already have a full packet,
 * punt.
 */
int recv_eip_response(ab_session_p session)
{
    int data_needed = 0;
    int rc = PLCTAG_STATUS_OK;

    /*pdebug(session->debug,"Starting.");*/

    /*
     * Determine the amount of data to get.  At a minimum, we
     * need to get an encap header.  This will determine
     * whether we need to get more data or not.
     */
    if (session->recv_offset < sizeof(eip_encap_t)) {
        data_needed = sizeof(eip_encap_t);
    } else {
        data_needed = sizeof(eip_encap_t) + ((eip_encap_t *)(session->recv_data))->encap_length;
    }

    if (session->recv_offset < data_needed) {
        /* read everything we can */
        do {
            rc = socket_read(
                session->sock, session->recv_data + session->recv_offset, data_needed - session->recv_offset);

            /*pdebug(session->debug,"socket_read rc=%d",rc);*/

            if (rc < 0) {
                if (rc != PLCTAG_ERR_NO_DATA) {
                    /* error! */
                    pdebug(session->debug, "Error reading socket! rc=%d", rc);
                    return rc;
                }
            } else {
                session->recv_offset += rc;

                /*pdebug_dump_bytes(session->debug, session->recv_data, session->recv_offset);*/

                /* recalculate the amount of data needed if we have just completed the read of an encap header */
                if (session->recv_offset >= sizeof(eip_encap_t)) {
                    data_needed = sizeof(eip_encap_t) + ((eip_encap_t *)(session->recv_data))->encap_length;
                }
            }
        } while (rc > 0 && session->recv_offset < data_needed);
    }

    /* did we get all the data? */
    if (session->recv_offset >= data_needed) {
        session->resp_seq_id = ((eip_encap_t *)(session->recv_data))->encap_sender_context;
        session->has_response = 1;

        pdebug(session->debug, "request received all needed data.");
        pdebug_dump_bytes(session->debug, session->recv_data, session->recv_offset);
    }

    return rc;
}
