#ifndef COAP_CLIENT_H
#define COAP_CLIENT_H

#include <pugixml.hpp>
#include <nlohmann/json.hpp>
#include <coap3/coap.h>
#include <support/logging/CHIPLogging.h>
#include "converter.h"

#define BUFSIZE 100

inline int have_response = 0;

// Global variables containing the loaded definitions
inline nlohmann::ordered_json sdf_model_file;
inline nlohmann::ordered_json sdf_mapping_lwm2m_file;
inline nlohmann::ordered_json sdf_mapping_matter_file;
inline pugi::xml_document lwm2m_xml_file;
inline pugi::xml_document cluster_xml;

/**
 * Function used to resolve the address of the CoAP server
 */
static inline int resolve_address(coap_str_const_t *host, uint16_t port, coap_address_t *dst, int scheme_hint_bits) {
    int ret = 0;
    coap_addr_info_t *addr_info;

    addr_info = coap_resolve_address_info(host, port, port,  port, port,
                                          AF_UNSPEC, scheme_hint_bits,
                                          COAP_RESOLVE_TYPE_REMOTE);
    if (addr_info) {
        ret = 1;
        *dst = addr_info->addr;
    }

    coap_free_address_info(addr_info);
    return ret;
}

/**
 * Function used to load the Cluster xml from the CoAP server
 */
int LoadClusterXmlFile(const char* client_uri);

/**
 * Function used to load the sdf-model from the CoAP server
 */
int LoadSdfModelFile(const char* client_uri);

/**
 * Function used to load the LwM2M to Matter merged mapping from the CoAP server
 */
int LoadSdfMappingLwm2mFile(const char* client_uri);

/**
 * Function used to load the Matter to LwM2M merged mapping from the CoAP server
 */
int LoadSdfMappingMatterFile(const char* client_uri);

/**
 * Function used to load the converted LwM2M definition from the CoAP server
 */
int LoadLwm2mFile(const char* client_uri);

/**
 * Function used to send a simple CoAP GET request without a payload
 */
int CoapClientGet(const char* client_uri);

/**
 * Function used to send a simple CoAP PUT request without a payload
 */
int CoapClientPut(const char* client_uri);

/**
 * Function used to send a simple CoAP GET request with a payload
 */
int CoapClientGet(const char* client_uri, char* answer, size_t answer_size);

/**
 * Function used to send a simple CoAP PUT request with a payload
 */
int CoapClientPut(const char* client_uri, char* data, size_t data_size);

#endif //COAP_CLIENT_H
