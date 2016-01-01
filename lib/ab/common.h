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


#ifndef __AB_COMMON_DEFS_H__
#define __AB_COMMON_DEFS_H__ 1


#include <platform.h>
#include <util/attr.h>

/*
 * Base tag types supported by this driver.
 */
 
#define GROUP_TAG       (1)
#define EXPLICIT_TAG    (2)
#define IMPLICIT_TAG    (3)
#define PCCC_TAG        (4)
#define PCCC_DHP_TAG    (5)


/*
 * Common CPU types
 * 
 * To some extent (and only some extent) these determine the protocol
 * used to talk with the PLC.
 */

#define CPU_PLC5 (1)
#define CPU_MLGX (2)
#define CPU_LGX  (3)
#define CPU_M800 (4)



#define MAX_PACKET_SIZE (600) /* EIP/CIP max packet size, should be 540 or so?? */


#define MAX_SESSION_HOST    (128)
#define MAX_CONN_PATH 		(128)

#define AB_EIP_DEFAULT_PORT (44818)


/* AB Constants*/
#define AB_EIP_OK   (0)
#define AB_EIP_VERSION ((uint16_t)0x0001)

/* in milliseconds */
#define AB_EIP_DEFAULT_TIMEOUT 2000 /* in ms */



/* AB EIP commands */
#define AB_EIP_REGISTER_SESSION 	((uint16_t)0x0065)
#define AB_EIP_UNREGISTER_SESSION 	((uint16_t)0x0066)
#define AB_EIP_READ_RR_DATA 		((uint16_t)0x006F)
#define AB_EIP_CONNECTED_SEND 		((uint16_t)0x0070)


/* CIP Item Types */
#define AB_EIP_ITEM_NAI ((uint16_t)0x0000) /* NULL Address Item */
#define AB_EIP_ITEM_CAI ((uint16_t)0x00A1) /* connected address item */
#define AB_EIP_ITEM_CDI ((uint16_t)0x00B1) /* connected data item */
#define AB_EIP_ITEM_UDI ((uint16_t)0x00B2) /* unconnected data item */

/* specific CM sub-commands */
#define AB_EIP_CMD_PCCC_EXECUTE     	((uint8_t)0x4B)
#define AB_EIP_CMD_FORWARD_CLOSE    	((uint8_t)0x4E)
#define AB_EIP_CMD_UNCONNECTED_SEND 	((uint8_t)0x52)
#define AB_EIP_CMD_FORWARD_OPEN     	((uint8_t)0x54)


/* common packet definitions */
#define AB_EIP_SECS_PER_TICK    (0x0A)
#define AB_EIP_TIMEOUT_TICKS    (0x05)
#define AB_EIP_VENDOR_ID        (0xF33D) /*tres 1337 */
#define AB_EIP_VENDOR_SN        (0x21504345)  /* the string !PCE */
#define AB_EIP_TIMEOUT_MULTIPLIER (0x01)
#define AB_EIP_RPI              (1000000)
#define AB_EIP_PLC5_PARAM       (0x4302) /* lowest 9 bits are packet size, just payload? */
#define AB_EIP_SLC_PARAM        (0x4302) /* lowest 9 bits are packet size, just payload? */
#define AB_EIP_LGX_PARAM        (0x43F8) /* lowest 9 bits are packet size, just payload? */
#define AB_EIP_TRANSPORT_CLASS_T3   ((uint8_t)0xA3)

extern char *get_gateway(plc_tag tag);
extern int cip_encode_path(int debug, uint8_t *data, const char *path, int cpu_type);
extern int get_cpu_type(plc_tag tag);



/*
 * Commonly used packet formats
 */


/* EIP Encapsulation Header */
START_PACK typedef struct {
	uint16_t encap_command;
	uint16_t encap_length;
	uint32_t encap_session_handle;
	uint32_t encap_status;
	uint64_t encap_sender_context;
	uint32_t encap_options;
} END_PACK eip_encap_t;



/* CIP "native" Request */
START_PACK typedef struct {
	/* encap header */
	uint16_t encap_command;         /* ALWAYS 0x0070 Connected Send */
	uint16_t encap_length;          /* packet size in bytes less the header size, which is 24 bytes */
	uint32_t encap_session_handle;  /* from session set up */
	uint32_t encap_status;          /* always _sent_ as 0 */
	uint64_t encap_sender_context;  /* whatever we want to set this to, used for
                                     * identifying responses when more than one
                                     * are in flight at once.
                                     */
	uint32_t options;               /* 0, reserved for future use */

	/* Interface Handle etc. */
	uint32_t interface_handle;      /* ALWAYS 0 */
	uint16_t router_timeout;        /* in seconds, zero for Connected Sends! */

	/* Common Packet Format - CPF Connected */
	uint16_t cpf_item_count;        /* ALWAYS 2 */
	uint16_t cpf_cai_item_type;     /* ALWAYS 0x00A1 Connected Address Item */
	uint16_t cpf_cai_item_length;   /* ALWAYS 2  */
	uint32_t cpf_targ_conn_id;      /* the connection id from Forward Open */
	uint16_t cpf_cdi_item_type;     /* ALWAYS 0x00B1, Connected Data Item type */
	uint16_t cpf_cdi_item_length;   /* length in bytes of the rest of the packet */

	/* Connection sequence number */
	uint16_t cpf_conn_seq_num;      /* connection sequence ID, inc for each message */
} END_PACK eip_cip_co_req;





/* CIP Response */
START_PACK typedef struct {
	/* encap header */
	uint16_t encap_command;         /* ALWAYS 0x0070 Connected Send */
	uint16_t encap_length;          /* packet size in bytes less the header size, which is 24 bytes */
	uint32_t encap_session_handle;  /* from session set up */
	uint32_t encap_status;          /* always _sent_ as 0 */
	uint64_t encap_sender_context;  /* whatever we want to set this to, used for
                                     * identifying responses when more than one
                                     * are in flight at once.
                                     */
	uint32_t options;               /* 0, reserved for future use */

	/* Interface Handle etc. */
	uint32_t interface_handle;      /* ALWAYS 0 */
	uint16_t router_timeout;        /* in seconds, zero for Connected Sends! */

	/* Common Packet Format - CPF Connected */
	uint16_t cpf_item_count;        /* ALWAYS 2 */
	uint16_t cpf_cai_item_type;     /* ALWAYS 0x00A1 Connected Address Item */
	uint16_t cpf_cai_item_length;   /* ALWAYS 2 ? */
	uint32_t cpf_orig_conn_id;      /* our connection ID, NOT the target's */
	uint16_t cpf_cdi_item_type;     /* ALWAYS 0x00B1, Connected Data Item type */
	uint16_t cpf_cdi_item_length;   /* length in bytes of the rest of the packet */

	/* connection ID from request */
	uint16_t cpf_conn_seq_num;      /* connection sequence ID, inc for each message */

	/* CIP Reply */
	uint8_t reply_service;          /* 0xCC CIP READ Reply */
	uint8_t reserved;              	/* 0x00 in reply */
	uint8_t status;                 /* 0x00 for success */
	uint8_t num_status_words;	   	/* number of 16-bit words in status */

	/* CIP Data*/
	uint8_t resp_data[ZLA_SIZE];
} END_PACK eip_cip_co_resp;



#endif
