#ifndef COAP_SERVER_H
#define COAP_SERVER_H

#include <coap3/coap.h>
#include "BridgeUtils.h"

// Global variable containing the LwM2M to Matter mapping
inline MatterIpsoMapping coap_mapping;

/**
 * Function used to initialize the CoAP server
 */
int init_server(const char* ip_address);

/**
 * Function used to start the CoAP server
 * Remember that init_server has to be called first
 */
int start_server();

/**
 * Function used to register a LwM2M ressource for a Matter attribute
 * This function can register either a GET or a PUT ressource
 * The last parameter is the type of the resource as defined in the LwM2M resource
 */
int RegisterAttributeResource(const char* uri, coap_request_t method, std::string& type);

/**
 * Function used to register a LwM2M ressource for a Matter attribute
 * This function is to be used if the corresponding ressource is both, readable
 * and writable
 * The last parameter is the type of the resource as defined in the LwM2M resource
 */
int RegisterAttributeRWResource(const char* uri, std::string& type);

/**
 * Function used to register a LwM2M ressource for a Matter command
 */
int RegisterCommandResource(const char* uri);

#endif //COAP_SERVER_H
