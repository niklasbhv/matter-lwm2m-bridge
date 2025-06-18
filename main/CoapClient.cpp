#include "CoapClient.h"
#include <iostream>

#include "pugixml.hpp"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include <cstdio>

static coap_context_t *ctx = NULL;
static coap_optlist_t *optlist = NULL;

/**
 * Handler invoked if a confirmable message is dropped after all retries have been exhausted
 */
static void nack_handler(coap_session_t *session COAP_UNUSED, const coap_pdu_t *sent COAP_UNUSED, const coap_nack_reason_t reason, const coap_mid_t id COAP_UNUSED)
{
    switch (reason) {
        case COAP_NACK_TOO_MANY_RETRIES:
        case COAP_NACK_NOT_DELIVERABLE:
        case COAP_NACK_RST:
        case COAP_NACK_TLS_FAILED:
        case COAP_NACK_TLS_LAYER_FAILED:
        case COAP_NACK_WS_LAYER_FAILED:
        case COAP_NACK_WS_FAILED:
            ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
            break;
        case COAP_NACK_ICMP_ISSUE:
        case COAP_NACK_BAD_RESPONSE:
        default:
            break;
    }
    return;
}

/**
 * Function used to load the Cluster xml from the CoAP server
 */
int LoadClusterXmlFile(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    coap_register_response_handler(ctx,
                                [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                    const uint8_t *data;
                                    size_t len;
                                    size_t offset;
                                    size_t total;

                                    (void)session;
                                    (void)sent;
                                    (void)id;
                                    have_response = 1;
                                    if (coap_get_data_large(received, &len, &data, &offset, &total)) {
                                        pugi::xml_parse_result result = cluster_xml.load_buffer(data, len);
                                        std::cout << "DATA SIZE: " << len << std::endl;
                                        ChipLogProgress(DeviceLayer, "CoAP Client: Result of the parsing: %s", result.description());
                                        ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                    }
                                    return COAP_RESPONSE_OK;
                                });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, &dst, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to load the sdf-model from the CoAP server
 */
int LoadSdfModelFile(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s\n", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s\n", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context\n");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session\n");
        goto finish;
    }

    /* coap_register_response_handler(ctx, response_handler); */
    coap_register_response_handler(ctx,
                            [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                const uint8_t *data;
                                size_t len;
                                size_t offset;
                                size_t total;

                                (void)session;
                                (void)sent;
                                (void)id;
                                have_response = 1;
                                if (coap_get_data_large(received, &len, &data, &offset, &total)) {
                                    sdf_model_file = nlohmann::json::parse((const char *)data, (const char *)data + (int)len);
                                    ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                }
                                return COAP_RESPONSE_OK;
                            });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));

    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU\n");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, &dst, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options\n");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU\n");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP PDU\n");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to load the LwM2M to Matter merged mapping from the CoAP server
 */
int LoadSdfMappingLwm2mFile(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s\n", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s\n", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context\n");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session\n");
        goto finish;
    }

    /* coap_register_response_handler(ctx, response_handler); */
    coap_register_response_handler(ctx,
                            [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                const uint8_t *data;
                                size_t len;
                                size_t offset;
                                size_t total;

                                (void)session;
                                (void)sent;
                                (void)id;
                                have_response = 1;
                                if (coap_get_data_large(received, &len, &data, &offset, &total)) {
                                    sdf_mapping_lwm2m_file = nlohmann::json::parse((const char *)data, (const char *)data + (int)len);
                                    ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                }
                                return COAP_RESPONSE_OK;
                            });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));

    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU\n");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, &dst, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options\n");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU\n");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP PDU\n");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to load the Matter to LwM2M merged mapping from the CoAP server
 */
int LoadSdfMappingMatterFile(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s\n", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s\n", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context\n");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session\n");
        goto finish;
    }

    /* coap_register_response_handler(ctx, response_handler); */
    coap_register_response_handler(ctx,
                            [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                const uint8_t *data;
                                size_t len;
                                size_t offset;
                                size_t total;

                                (void)session;
                                (void)sent;
                                (void)id;
                                have_response = 1;
                                if (coap_get_data_large(received, &len, &data, &offset, &total)) {
                                    sdf_mapping_matter_file = nlohmann::json::parse((const char *)data, (const char *)data + (int)len);
                                    ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                }
                                return COAP_RESPONSE_OK;
                            });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));

    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU\n");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, &dst, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options\n");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU\n");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP PDU\n");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to load the converted LwM2M definition from the CoAP server
 */
int LoadLwm2mFile(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    coap_register_response_handler(ctx,
                                [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                    const uint8_t *data;
                                    size_t len;
                                    size_t offset;
                                    size_t total;

                                    (void)session;
                                    (void)sent;
                                    (void)id;
                                    have_response = 1;
                                    if (coap_get_data_large(received, &len, &data, &offset, &total)) {
                                        pugi::xml_parse_result result = lwm2m_xml_file.load_buffer(data, len);
                                        std::cout << "DATA SIZE: " << len << std::endl;
                                        ChipLogProgress(DeviceLayer, "CoAP Client: Result of the parsing: %s", result.description());
                                        ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                    }
                                    return COAP_RESPONSE_OK;
                                });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, &dst, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to send a simple CoAP GET request without a payload
 */
int CoapClientGet(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    /* coap_register_response_handler(ctx, response_handler); */
    coap_register_response_handler(ctx,
                                 [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                        const uint8_t *data;
                                        size_t len;

                                        (void)session;
                                        (void)sent;
                                        (void)id;
                                        have_response = 1;
                                        if (coap_get_data(received, &len, &data)) {
                                            ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                        }
                                        return COAP_RESPONSE_OK;
                                  });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, NULL, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to send a simple CoAP PUT request without a payload
 */
int CoapClientPut(const char* client_uri)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    const char *message = "off";
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_PUT, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */   
    len = coap_uri_into_options(&uri, NULL, &optlist, 1, scratch, sizeof(scratch));
    if (len < 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    len = coap_add_data(pdu, sizeof(message), (const uint8_t*)message);
    if (!len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to add large data");
        goto finish;
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to send a simple CoAP GET request with a payload
 */
int CoapClientGet(const char* client_uri, char* answer, size_t answer_size)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    unsigned int wait_ms;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    // Set the result buffer as application data for the session
    coap_session_set_app_data(session, answer);

    coap_register_response_handler(ctx,
                                 [](coap_session_t *session, const coap_pdu_t *sent, const coap_pdu_t *received, const coap_mid_t id) {
                                        const uint8_t *data;
                                        size_t len;

                                        (void)session;
                                        (void)sent;
                                        (void)id;
                                        have_response = 1;
                                        if (coap_get_data(received, &len, &data)) {
                                            char *result_buffer = (char *)coap_session_get_app_data(session);
                                            strncpy(result_buffer, (const char *)data, len);
                                            result_buffer[len] = '\0';
                                            ChipLogProgress(DeviceLayer, "%*.*s", (int)len, (int)len, (const char *)data);
                                        }
                                        return COAP_RESPONSE_OK;
                                  });
    coap_register_nack_handler(ctx, nack_handler);

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_GET, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    len = coap_uri_into_options(&uri, NULL, &optlist, 1, scratch, sizeof(scratch));
    if (len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

    wait_ms = (coap_session_get_default_leisure(session).integer_part + 1) * 1000;

    while (have_response == 0) {
        res = coap_io_process(ctx, 1000);
        if (res >= 0) {
            if (wait_ms > 0) {
                if ((unsigned)res >= wait_ms) {
                    ChipLogError(DeviceLayer, "CoAP Client: Timeout");
                    break;
                } else {
                    wait_ms -= res;
                }
            }
        }
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}

/**
 * Function used to send a simple CoAP PUT request with a payload
 */
int CoapClientPut(const char* client_uri, char* data, size_t data_size)
{
    coap_session_t *session = NULL;
    coap_pdu_t *pdu = nullptr;
    coap_address_t dst;

    int result = EXIT_FAILURE;;
    int len;
    int res;
    coap_uri_t uri;
    const char *coap_uri = client_uri;
    unsigned char scratch[BUFSIZE];

    /* Initialize libcoap library */
    coap_startup();

    /* Parse the URI */
    len = coap_split_uri((const unsigned char *)coap_uri, strlen(coap_uri), &uri);
    if (len != 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to parse uri %s", coap_uri);
        goto finish;
    }

    /* resolve destination address where server should be sent */
    len = resolve_address(&uri.host, uri.port, &dst, 1 << uri.scheme);
    if (len <= 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to resolve address %*.*s", (int)uri.host.length, (int)uri.host.length, (const char *)uri.host.s);
        goto finish;
    }

    /* create CoAP context and a client session */
    if (!(ctx = coap_new_context(nullptr))) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create libcoap context");
        goto finish;
    }

    /* Support large responses */
    session = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!session) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create client session");
        goto finish;
    }

    /* construct CoAP message */
    pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_PUT, coap_new_message_id(session), coap_session_max_pdu_size(session));
    if (!pdu) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot create PDU");
        goto finish;
    }

    /* Add option list (which will be sorted) to the PDU */
    
    len = coap_uri_into_options(&uri, NULL, &optlist, 1, scratch, sizeof(scratch));
    if (len < 0) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to create options");
        goto finish;
    }

    if (optlist) {
        res = coap_add_optlist_pdu(pdu, &optlist);
        if (res != 1) {
            ChipLogError(DeviceLayer, "CoAP Client: Failed to add options to PDU");
            goto finish;
        }
    }

    len = coap_add_data(pdu, data_size, (const uint8_t*)data);
    if (!len) {
        ChipLogError(DeviceLayer, "CoAP Client: Failed to add large data");
        goto finish;
    }

    coap_show_pdu(COAP_LOG_WARN, pdu);

    /* and send the PDU */
    if (coap_send(session, pdu) == COAP_INVALID_MID) {
        ChipLogError(DeviceLayer, "CoAP Client: Cannot send CoAP pdu");
        goto finish;
    }

finish:
    have_response = 0;
    coap_delete_optlist(optlist);
    optlist = NULL;
    coap_session_release(session);
    session = NULL;
    coap_free_context(ctx);
    ctx = NULL;
    coap_cleanup();

    return result;
}