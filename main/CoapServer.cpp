#include <support/logging/CHIPLogging.h>
#include "CoapServer.h"
#include "BindingHandler.h"
#include <platform/CHIPDeviceLayer.h>
#include "freertos/FreeRTOS.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

#include <cstdint>
#include <cstring>

coap_resource_t *resource = nullptr;
coap_context_t  *coap_ctx = nullptr;

using namespace chip;
using namespace chip::app;

// Map that links resources with their respective type
std::map<std::string, std::string> type_map;

/**
 * Function used to split a string accoring to a delimiter into a vector containing the resulting substrings 
 */ 
std::vector<std::string> SplitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }

    tokens.push_back(str.substr(start)); // Add the last token
    return tokens;
}

/**
 * Function used to forward messages based on the uri of the resource that was accessed 
 * This function is used in combination with a CoAP resource handler
 */ 
void ForwardAttributeWriteMessage(coap_string_t* uri_path, char payload[])
{
    // Convert the URI where the request was made into a string
    std::string uri = std::string(reinterpret_cast<const char*>(uri_path->s), uri_path->length);
    // Split the string into their ids.
    // The URI should always compose of three IDs, the first being the object and the last being the resource
    std::vector<std::string> split_string = SplitString(uri, '/');
    int object_id = std::stoi(split_string.at(0));
    int resource_id = std::stoi(split_string.at(2));
    
    // Determine the cluster and the attribute id from the object and the resource id using the mapper structure
    std::cout << "Got request on object id: " << object_id << " and resource id: " << resource_id << std::endl;
    int cluster_id = coap_mapping.cluster_object_map.get_matter_id(object_id);
    int attribute_id = coap_mapping.attribute_resource_map.get_matter_id(resource_id);
    std::cout << "Sending request to cluster: " << cluster_id << " with attribute: " << attribute_id << std::endl;

    // Prepare the data
    BindingCommandData * data = chip::Platform::New<BindingCommandData>();
    data->attributeId         = attribute_id;
    data->clusterId           = cluster_id;
    data->writeAttribute      = true;

    // Depending on the type we set the type of our data
    // This way the Matter function will be invoked with the correct type
    if (type_map.at(uri) == "Boolean") {
        data->data = *reinterpret_cast<bool*>(payload);
    } else if (type_map.at(uri) == "Unsigned Integer") {
        data->data = *reinterpret_cast<uint16_t*>(payload);
    }

    // Schedule sending of the command
    chip::DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
}

/**
 * Function used to forward messages based on the uri of the resource that was accessed
 * This function is used in combination with a CoAP resource handler
 */
void ForwardAttributeReadMessage(coap_string_t* uri_path, uint8_t* buffer, size_t buf_len, size_t& size)
{
    // Convert the URI where the request was made into a string
    std::string uri = std::string(reinterpret_cast<const char*>(uri_path->s), uri_path->length);
    // Split the string into their ids.
    // The URI should always compose of three IDs, the first being the object and the last being the resource
    std::vector<std::string> split_string = SplitString(uri, '/');
    int object_id = std::stoi(split_string.at(0));
    int resource_id = std::stoi(split_string.at(2));
    
    // Determine the cluster and the attribute id from the object and the resource id using the mapper structure
    std::cout << "Got request on object id: " << object_id << " and resource id: " << resource_id << std::endl;
    int cluster_id = coap_mapping.cluster_object_map.get_matter_id(object_id);
    int attribute_id = coap_mapping.attribute_resource_map.get_matter_id(resource_id);
    std::cout << "Sending request to cluster: " << cluster_id << " with attribute: " << attribute_id << std::endl;

    // Prepare the data
    BindingCommandData * data = chip::Platform::New<BindingCommandData>();
    data->attributeId         = attribute_id;
    data->clusterId           = cluster_id;
    data->readAttribute       = true;

    // Depending on the type we set the type of our data
    // This way the Matter function will be invoked with the correct type
    if (type_map.at(uri) == "Boolean") {
        data->data = *reinterpret_cast<bool*>(buffer);
    } else if (type_map.at(uri) == "Unsigned Integer") {
        data->data = *reinterpret_cast<uint16_t*>(buffer);
    }

    // Schedule sending of the command
    chip::DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));

    // Afterwards we wait for the response of the command and write it to the buffer
    int tries = 10;
    // Wait for the response
    while (tries > 0) {
        // Wait 5 seconds after each try
        std::this_thread::sleep_for(std::chrono::seconds(5));
        // We check the contents of a global pointer that will contain the result
        if (result_ptr != nullptr)
        {
            if (std::holds_alternative<uint16_t>(*result_ptr))
            {
                size = snprintf((char *)buffer, buf_len, std::to_string(std::get<uint16_t>(*result_ptr)).c_str());
                break;
            } else if (std::holds_alternative<bool>(*result_ptr)) {
                size = snprintf((char *)buffer, buf_len, std::to_string(std::get<bool>(*result_ptr)).c_str());
                break;
            }
        }
        
        tries--;
    }
    // Reset the result pointer
    result_ptr = nullptr;
}

/**
 * Function used to forward messages based on the uri of the resource that was accessed 
 * This function is used in combination with a CoAP resource handler
 */
void ForwardCommandMessage(coap_string_t* uri_path)
{
    // Convert the URI where the request was made into a string
    std::string uri = std::string(reinterpret_cast<const char*>(uri_path->s), uri_path->length);
    // Split the string into their ids.
    // The URI should always compose of three IDs, the first being the object and the last being the resource
    std::vector<std::string> split_string = SplitString(uri, '/');
    int object_id = std::stoi(split_string.at(0));
    int resource_id = std::stoi(split_string.at(2));
    
    std::cout << "Got request on object id: " << object_id << " and resource id: " << resource_id << std::endl;
    int cluster_id = coap_mapping.cluster_object_map.get_matter_id(object_id);
    int command_id = coap_mapping.command_resource_map.get_matter_id(resource_id);
    std::cout << "Sending request to cluster: " << cluster_id << " with command: " << command_id << std::endl;

    // Prepare the data
    BindingCommandData * data = chip::Platform::New<BindingCommandData>();
    data->commandId           = command_id;
    data->clusterId           = cluster_id;

    // Schedule sending of the command
    chip::DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
}

/**
 * Handler used for attribute GET requests
 */
void hnd_attribute_get(coap_resource_t *resource, coap_session_t  *session,
             const coap_pdu_t *request, const coap_string_t *query,
             coap_pdu_t *response) {

    unsigned char buf[40];
    size_t response_len;

    (void)resource;
    (void)session;
    (void)request;

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT, coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_TEXT_PLAIN), buf);
    coap_add_option(response, COAP_OPTION_MAXAGE, coap_encode_var_safe(buf, sizeof(buf), 0x01), buf);
    
    ForwardAttributeReadMessage(coap_get_uri_path(request), buf, sizeof(buf), response_len);

    coap_add_data(response, response_len, buf);
}

/**
 * Handler used for attribute PUT requests
 */ 
void hnd_attribute_put(coap_resource_t *resource, coap_session_t  *session,
             const coap_pdu_t *request, const coap_string_t *query,
             coap_pdu_t *response) {

    size_t size;
    const uint8_t *data;

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    coap_show_pdu(COAP_LOG_WARN, request);

    if (coap_get_data(request, &size, &data)) {
        // Copy the payload to a buffer (if needed)
        char payload[size + 1];
        memcpy(payload, data, size);
        payload[size] = '\0';  // Null-terminate the payload for printing

        printf("Received PUT data: %s\n", payload);
        ForwardAttributeWriteMessage(coap_get_uri_path(request), payload);

        // Process the payload as needed
    } else {
        printf("No data received in PUT request.\n");
        char payload[0];
        ForwardAttributeWriteMessage(coap_get_uri_path(request), payload);
    }
    
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
}

/**
 * Handler used for command PUT requests
 */ 
void hnd_command_put(coap_resource_t *resource, coap_session_t  *session,
             const coap_pdu_t *request, const coap_string_t *query,
             coap_pdu_t *response) {

    (void)resource;
    (void)session;
    (void)request;

    ForwardCommandMessage(coap_get_uri_path(request));

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
}

/**
 * Function used to register a c attribute resource that can be read and written 
 */ 
int RegisterAttributeRWResource(const char* uri, std::string& type)
{
    type_map[uri] = type;
    /* Create a resource that the server can respond to with information */
    resource = coap_resource_init(coap_make_str_const(uri), 0);

    coap_register_handler(resource, COAP_REQUEST_GET, hnd_attribute_get);
    coap_register_handler(resource, COAP_REQUEST_PUT, hnd_attribute_put);
    
    coap_add_resource(coap_ctx, resource);

    return 0;
}

/**
 * Function used to register a attribute ressource
 */
int RegisterAttributeResource(const char* uri, coap_request_t method, std::string& type)
{
    type_map[uri] = type;
    /* Create a resource that the server can respond to with information */
    resource = coap_resource_init(coap_make_str_const(uri), 0);
    if (method == COAP_REQUEST_GET) {
        coap_register_handler(resource, method, hnd_attribute_get);
    }
    else if (method == COAP_REQUEST_PUT) {
        coap_register_handler(resource, method, hnd_attribute_put);
    }
    
    coap_add_resource(coap_ctx, resource);

    return 0;
}

/**
 * Function used to register a command ressource
 */
int RegisterCommandResource(const char* uri)
{
    /* Create a resource that the server can respond to with information */
    resource = coap_resource_init(coap_make_str_const(uri), 0);

    coap_register_handler(resource, COAP_REQUEST_PUT, hnd_command_put);
    
    coap_add_resource(coap_ctx, resource);

    return 0;
}

/**
 * Function used to cleanup the CoAP server
 */
void cleanup_server()
{
    coap_free_context(coap_ctx);
    coap_cleanup();
}

/**
 * Function used to initialize the CoAP server
 */
int init_server(const char* ip_address)
{
    int result = EXIT_FAILURE;
    uint32_t scheme_hint_bits;
    coap_addr_info_t *info = nullptr;
    coap_addr_info_t *info_list = nullptr;
    coap_str_const_t *my_address = coap_make_str_const(ip_address);
    bool have_ep = false;

    /* Initialize libcoap library */
    coap_startup();

    /* Create CoAP context */
    coap_ctx = coap_new_context(nullptr);
    if (!coap_ctx) {
        ChipLogError(DeviceLayer, "CoAP Server: Cannot initialize context");
        cleanup_server();

        return result;
    }

    /* Let libcoap do the multi-block payload handling (if any) */
    coap_context_set_block_mode(coap_ctx, COAP_BLOCK_USE_LIBCOAP|COAP_BLOCK_SINGLE_BODY);

    scheme_hint_bits = coap_get_available_scheme_hint_bits(0, 0, COAP_PROTO_NONE);
    info_list = coap_resolve_address_info(my_address, 0, 0, 0, 0,
                                          0,
                                          scheme_hint_bits, COAP_RESOLVE_TYPE_LOCAL);
    /* Create CoAP listening endpoint(s) */
    for (info = info_list; info != NULL; info = info->next) {
        coap_endpoint_t *ep;

        ep = coap_new_endpoint(coap_ctx, &info->addr, info->proto);
        if (!ep) {
            ChipLogError(DeviceLayer, "CoAP Server: Cannot create endpoint for CoAP proto %u\n",
                          info->proto);
        } else {
            have_ep = true;
        }
    }
    coap_free_address_info(info_list);
    if (have_ep == false) {
        ChipLogError(DeviceLayer, "CoAP Server: No context available for interface '%s'\n",
                     (const char *)my_address->s);
        cleanup_server();

        return result;
    }

    /* Add in Multicast listening as appropriate */
#ifdef COAP_LISTEN_MULTICAST_IPV4
    coap_join_mcast_group_intf(coap_ctx, COAP_LISTEN_MULTICAST_IPV4, NULL);
#endif /* COAP_LISTEN_MULTICAST_IPV4 */
#ifdef COAP_LISTEN_MULTICAST_IPV6
    coap_join_mcast_group_intf(coap_ctx, COAP_LISTEN_MULTICAST_IPV6, NULL);
#endif /* COAP_LISTEN_MULTICAST_IPV6 */

    return EXIT_SUCCESS;
}

/**
 * Function starts the coap server
 * Note that init_server must be called beforehand
*/
int start_server() 
{
    ChipLogProgress(DeviceLayer, "CoAP Server: Starting CoAP Server");
    if (coap_ctx == nullptr)
    {
        ChipLogError(DeviceLayer, "CoAP Server: Tried to start the CoAP Server before initializing it");
        return EXIT_FAILURE;
    }
    
    /* Handle any libcoap I/O requirements */
    while (true) {
        coap_io_process(coap_ctx, COAP_IO_WAIT);
    }
    ChipLogProgress(DeviceLayer, "CoAP Server: CoAP Server terminated");
    return EXIT_SUCCESS;
}
