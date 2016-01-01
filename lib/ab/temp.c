    if(elem_count <= 0) {
        return PLCTAG_ERR_BAD_PARAM;
    }
    
    /* clear the buffer area */
    mem_set(request_buf, 0 , sizeof(request_buf));

    /* point the request struct at the buffer */
    cip = (eip_cip_uc_req*)(&request_buf[0]);

    /* point to the end of the struct */
    data = (&request_buf[0]) + sizeof(eip_cip_uc_req);

    /*
     * set up the embedded CIP read packet
     * The format is:
     *
     * uint8_t cmd
     * LLA formatted name
     * uint16_t # of elements to read
     */

    embed_start = data;

    /* set up the CIP Read request */
    *data = AB_EIP_CMD_CIP_READ_FRAG;
    data++;
    
    /* encode the tag name into the request */
    data = cip_encode_tag_name(data,tag);
    
    if(!data) {
        return PLCTAG_ERR_BAD_PARAM;
    }

    /* add the count of elements to read. */
    *((uint16_t*)data) = h2le16(elem_count);
    data += sizeof(uint16_t);

    /* add the byte offset for this request */
    explicit_tag_context->read_request_byte_offset_index = data - (uint8_t*)cip;
    *((uint32_t*)data) = h2le32(byte_offset);
    data += sizeof(uint32_t);

    /* mark the end of the embedded packet */
    embed_end = data;
    
    explicit_tag_context->read_request_cip_index = embed_start - (uint8_t*)cip;
    explicit_tag_context->read_request_cip_length = embed_end - embed_start;

    /* Now copy in the routing information for the embedded message */
    /*
     * routing information.  Format:
     *
     * uint8_t path_size in 16-bit words
     * uint8_t reserved/pad (zero)
     * uint8_t[...] path (padded to even number of bytes)
     */
     
    data = cip_encode_path(data, tag, EXPLICIT_TAG);
    
    if(!data) {
        return PLCTAG_ERR_BAD_PARAM;
    }
    
    /* save the request size */
    explicit_tag_context->read_request_template_length = data - (uint8_t *)cip;

    /* now we go back and fill in the fields of the static part */

    /* encap fields */
    cip->encap_command = h2le16(AB_EIP_READ_RR_DATA); /* ALWAYS 0x0070 Unconnected Send*/

    /* router timeout */
    cip->router_timeout = h2le16(1); /* one second timeout, enough? */

    /* Common Packet Format fields for unconnected send. */
    cip->cpf_item_count = h2le16(2);                  /* ALWAYS 2 */
    cip->cpf_nai_item_type = h2le16(AB_EIP_ITEM_NAI); /* ALWAYS 0 */
    cip->cpf_nai_item_length = h2le16(0);             /* ALWAYS 0 */
    cip->cpf_udi_item_type = h2le16(AB_EIP_ITEM_UDI); /* ALWAYS 0x00B2 - Unconnected Data Item */
    cip->cpf_udi_item_length = h2le16(data - (uint8_t*)(&cip->cm_service_code)); /* REQ: fill in with length of remaining data. */

    /* CM Service Request - Connection Manager */
    cip->cm_service_code = AB_EIP_CMD_UNCONNECTED_SEND; /* 0x52 Unconnected Send */
    cip->cm_req_path_size = 2;                          /* 2, size in 16-bit words of path, next field */
    cip->cm_req_path[0] = 0x20;                         /* class */
    cip->cm_req_path[1] = 0x06;                         /* Connection Manager */
    cip->cm_req_path[2] = 0x24;                         /* instance */
    cip->cm_req_path[3] = 0x01;                         /* instance 1 */

    /* Unconnected send needs timeout information */
    cip->secs_per_tick = AB_EIP_SECS_PER_TICK; /* seconds per tick */
    cip->timeout_ticks = AB_EIP_TIMEOUT_TICKS; /* timeout = src_secs_per_tick * src_timeout_ticks */

    /* size of embedded packet */
    cip->uc_cmd_length = h2le16(embed_end - embed_start);

