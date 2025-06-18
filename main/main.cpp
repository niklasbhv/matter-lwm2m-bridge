/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "Device.h"
#include "DeviceCallbacks.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/reporting/reporting.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/util/attribute-storage.h>
#include <common/Esp32AppServer.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/ZclString.h>
#include <platform/ESP32/ESP32Utils.h>
#include <common/Esp32ThreadInit.h>
#include <app/InteractionModelEngine.h>
#include <lib/core/ErrorStr.h>
#include <app/server/Server.h>
#include "LwM2MObject.hpp"
#include <list>
#include <optional>
#include "matter.h"

#include "BindingHandler.h"
#include "AppTask.h"
#include <app/clusters/bindings/bindings.h>

#include "esp_netif.h"
#include "esp_pthread.h"
#include "BridgeUtils.h"
#include "CoapServer.h"
#include "CoapClient.h"
#include <coap3/coap.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
#include <platform/ESP32/ESP32DeviceInfoProvider.h>
#else
#include <DeviceInfoProviderImpl.h>
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

namespace {
#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
chip::DeviceLayer::ESP32FactoryDataProvider sFactoryDataProvider;
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
chip::DeviceLayer::ESP32DeviceInfoProvider gExampleDeviceInfoProvider;
#else
chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
} // namespace

extern const char TAG[] = "bridge-app";

using namespace ::chip;
using namespace ::chip::DeviceManager;
using namespace ::chip::Platform;
using namespace ::chip::Credentials;
using namespace ::chip::app::Clusters;

using namespace chip::app;
using namespace chip::app::Clusters::OnOff;

static AppDeviceCallbacks AppCallback;

static const int kNodeLabelSize = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
static const int kDescriptorAttributeArraySize = 254;
static const int kBindingAttributeArraySize = 254;

static EndpointId gCurrentEndpointId;
static EndpointId gFirstDynamicEndpointId;
static Device * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT]; // number of dynamic endpoints count

// A single bridged device
// Left to showcase the original implementation
static Device gLight1("Light 1", "Office");

// Mapping between Matter and LwM2M
static MatterIpsoMapping matter_mapping;

// (taken from chip-devices.xml)
#define DEVICE_TYPE_BRIDGED_NODE 0x0013
// (taken from lo-devices.xml)
#define DEVICE_TYPE_LO_ON_OFF_LIGHT 0x0100

// (taken from chip-devices.xml)
#define DEVICE_TYPE_ROOT_NODE 0x0016
// (taken from chip-devices.xml)
#define DEVICE_TYPE_BRIDGE 0x000e

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1

/**
 * This code is left to showcase the original intended use of the bridge in comparison 
 * to the newly added dynamic generation of an endpoint based on an converted sdf-model
 */
/* BRIDGED DEVICE ENDPOINT: contains the following clusters:
   - On/Off
   - Descriptor
   - Bridged Device Basic Information
*/

// Create a device type
const EmberAfDeviceType gBridgedCustomDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                                    { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

// Declare On/Off cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), /* on/off */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic Information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),              /* Reachable */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Cluster List for Bridged Light endpoint
// TODO: It's not clear whether it would be better to get the command lists from
// the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedLightClusters)
    DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Bridged Light endpoint
DECLARE_DYNAMIC_ENDPOINT(bridgedLightEndpoint, bridgedLightClusters);

DataVersion gLight1DataVersions[ArraySize(bridgedLightClusters)];

// REVISION definitions:
#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION (2u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)

/**
 * Function used to add an device type definition to an endpoint
 */
int AddDeviceEndpoint(Device * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (NULL == gDevices[index])
        {
            gDevices[index] = dev;
            EmberAfStatus ret;
            while (true)
            {
                dev->SetEndpointId(gCurrentEndpointId);
                ret =
                    emberAfSetDynamicEndpoint(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
                if (ret == EMBER_ZCL_STATUS_SUCCESS)
                {
                    ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                                    gCurrentEndpointId, index);
                    return index;
                }
                else if (ret != EMBER_ZCL_STATUS_DUPLICATE_EXISTS)
                {
                    return -1;
                }
                // Handle wrap condition
                if (++gCurrentEndpointId < gFirstDynamicEndpointId)
                {
                    gCurrentEndpointId = gFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

/**
 * Function used to remove a device type definition from an endpoint
 */
CHIP_ERROR RemoveDeviceEndpoint(Device * dev)
{
    for (uint8_t index = 0; index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; index++)
    {
        if (gDevices[index] == dev)
        {
            EndpointId ep   = emberAfClearDynamicEndpoint(index);
            gDevices[index] = NULL;
            ChipLogProgress(DeviceLayer, "Removed device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep, index);
            // Silence complaints about unused ep when progress logging
            // disabled.
            UNUSED_VAR(ep);
            return CHIP_NO_ERROR;
        }
    }
    return CHIP_ERROR_INTERNAL;
}

/**
 * Function used to load and parse the cluster definition.
 * The targeted cluster definition is later used as the client cluster that is used to communicate with the server cluster
 * of the targeted Matter device
 */
matter::Cluster LoadClusterDefinition()
{
    const char* cluster_xml_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/xml/cluster-xml";
    LoadClusterXmlFile(cluster_xml_uri);
    // Parse the cluster xml into a cluster object
    matter::Cluster cluster = matter::ParseCluster(cluster_xml.document_element());
    cluster_xml.reset();
    return cluster;
}

/**
 * Function used to generate a custom bridged device based on the given device type and list of clusters
 */
int CreateCustomDevice(matter::Device& device, std::list<matter::Cluster>& clusters) {   
    ChipLogError(DeviceLayer, "Creating a custom endpoint");

    // Set the device type for the bridged endpoint
    const EmberAfDeviceType gBridgedCustomDeviceTypes[] = { { static_cast<chip::DeviceTypeId>(device.id), DEVICE_VERSION_DEFAULT },
                                                    { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };
    // We limit this to the first cluster for this poc as the device type definition only contains two clusters
    matter::Cluster cluster;
    if (!clusters.empty()) {
        cluster = clusters.front();
    } else {
        return -1;
    }
    
    // Create everything neccessary for the server Cluster
    // Create the list of attributes
    std::vector<EmberAfAttributeMetadata> cluster_attributes;
    for (const auto& attribute : cluster.attributes) {
        // Just for demonstrating purposes
        // In a fully featured version, there would exist a mapper from the type to its ZAP_TYPE
        if (attribute.type == "bool") {
            cluster_attributes.push_back(DECLARE_DYNAMIC_ATTRIBUTE(attribute.id, BOOLEAN, 1, 0));
        } else if (attribute.type == "uint16") {
            cluster_attributes.push_back(DECLARE_DYNAMIC_ATTRIBUTE(attribute.id, INT16U, 16, 0));
        }
    }
    cluster_attributes.push_back({ZAP_EMPTY_DEFAULT(), 0xFFFD, 2, ZAP_TYPE(INT16U), ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE)}); // Cluster Revision

    // Create the list of commands
    std::vector<CommandId> cluster_incomming_commands;
    for (const auto& command : cluster.client_commands) {
        cluster_incomming_commands.push_back(command.id);
    }
    // Invalid command id
    cluster_incomming_commands.push_back(kInvalidCommandId);
    
    // Client cluster loaded from the definition of the Matter device
    // This is part of the PoC as normally this information would also be available if a LwM2M converter would be usable on the bridge
    // This object has to be static to ensure its lifetime beyond this function
    static matter::Cluster client_cluster = LoadClusterDefinition();
    
    // Create everything neccessary for the client Cluster
    // Create the list of attributes
    std::vector<EmberAfAttributeMetadata> client_cluster_attributes;
    for (const auto& attribute : client_cluster.attributes) {
        // Just for demonstrating purposes
        // In a fully featured version, there would exist a mapper from the type to its ZAP_TYPE
        if (attribute.type == "bool") {
            client_cluster_attributes.push_back(DECLARE_DYNAMIC_ATTRIBUTE(attribute.id, BOOLEAN, 1, 0));
        } else if (attribute.type == "uint16") {
            client_cluster_attributes.push_back(DECLARE_DYNAMIC_ATTRIBUTE(attribute.id, INT16U, 16, 0));
        }
    }
    client_cluster_attributes.push_back({ZAP_EMPTY_DEFAULT(), 0xFFFD, 2, ZAP_TYPE(INT16U), ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE)}); // Cluster Revision

    // Create the list of commands
    std::vector<CommandId> client_cluster_incomming_commands;
    for (const auto& command : client_cluster.client_commands) {
        client_cluster_incomming_commands.push_back(command.id);
    }
    // Invalid command id
    client_cluster_incomming_commands.push_back(kInvalidCommandId);

    // Declare the Descriptor cluster attributes
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorCustomAttrs)
        DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), // device list
        DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),     // server list
        DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),     // client list
        DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),      // parts list
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

    // Declare the Bridged Device Basic Information cluster attributes
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedCustomDeviceBasicAttrs)
        DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0), // NodeLabel
        DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),                  // Reachable
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

    // Declare the Binding cluster attribute
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedBindingClusterAttributes)
        DECLARE_DYNAMIC_ATTRIBUTE(Binding::Attributes::Binding::Id, ARRAY, kBindingAttributeArraySize, 1), // Binding
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

    // Were adding the generated cluster in combination with other utility clusters
    // Keep in mind that this demonstration only supports a single cluster
    DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(BridgedCustomClusters)
        DECLARE_DYNAMIC_CUSTOM_CLUSTER(cluster.id, cluster_attributes.data(), static_cast<uint16_t>(cluster_attributes.size()), cluster_incomming_commands.data(), nullptr), // Custom Server Cluster
        DECLARE_DYNAMIC_CUSTOM_CLUSTER(client_cluster.id, client_cluster_attributes.data(), static_cast<uint16_t>(client_cluster_attributes.size()), client_cluster_incomming_commands.data(), nullptr), // Custom Client Cluster
        DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorCustomAttrs, nullptr, nullptr),                      // Descriptor Cluster
        DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedCustomDeviceBasicAttrs, nullptr, nullptr), // Bridged Device Basic Information Cluster
        DECLARE_DYNAMIC_CLUSTER(Binding::Id, bridgedBindingClusterAttributes, nullptr, nullptr) // Binding Cluster
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

    // Declare the dynamic endpoint
    DECLARE_DYNAMIC_ENDPOINT(BridgedCustomEndpoint, BridgedCustomClusters);
    
    DataVersion gCustomDataVersions[ArraySize(BridgedCustomClusters)];
    
    // Add the endpoint to the node of the bridge
    static Device bridged_custom_device(device.name.c_str(), "No Location");
    AddDeviceEndpoint(&bridged_custom_device, &BridgedCustomEndpoint, Span<const EmberAfDeviceType>(gBridgedCustomDeviceTypes),
                      Span<DataVersion>(gCustomDataVersions), 1);
    
    // Set the device as reachable
    bridged_custom_device.SetReachable(true);

    return 0;
}

/**
 * Callback function that is invoked if a device tries to read from an attribute that is bridged
 * The function will translate the read interaction into a CoAP GET request and return the response to Matter device
 */ 
Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != NULL))
    {
        AttributeId attribute_id = attributeMetadata->attributeId;
        // Translate the cluster and attribute id into a object and a resource id
        int ipso_object_id = matter_mapping.cluster_object_map.get_ipso_id(clusterId);
        int ipso_resource_id = matter_mapping.attribute_resource_map.get_ipso_id(attribute_id);
        std::string target = "coap://[fd73:13f6:c3ed:1:d8bd:9673:d9cd:a562]:5184/";
        // Build the target uri basd on the translated ids
        target.append(std::to_string(ipso_object_id));
        target.append("/0/");
        target.append(std::to_string(ipso_resource_id));
        // Send the CoAP GET request
        CoapClientGet(target.c_str(), reinterpret_cast<char*>(buffer), maxReadLength);
        return Protocols::InteractionModel::Status::Success;
    }

    return Protocols::InteractionModel::Status::Failure;
}

/**
 * Callback function that is invoked if a device tries to write an attribute that is bridged
 * The function will translate the write interaction into a CoAP PUT request
 */ 
Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        AttributeId attribute_id = attributeMetadata->attributeId;
        // Translate the cluster and attribute id into a object and a resource id
        int ipso_object_id = matter_mapping.cluster_object_map.get_ipso_id(clusterId);
        int ipso_resource_id = matter_mapping.attribute_resource_map.get_ipso_id(attribute_id);
        std::string target = "coap://[fd73:13f6:c3ed:1:d8bd:9673:d9cd:a562]:5184/";
        // Build the target uri basd on the translated ids
        target.append(std::to_string(ipso_object_id));
        target.append("/0/");
        target.append(std::to_string(ipso_resource_id));
        // Send the CoAP PUT request
        CoapClientPut(target.c_str(), reinterpret_cast<char*>(buffer), attributeMetadata->size);
        return Protocols::InteractionModel::Status::Success;
    }

    return Protocols::InteractionModel::Status::Failure;
}

/**
 * These are functions leftover from the original implementation
 * These are left to showcase the original function of the bridge
 */
namespace {
void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<app::ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(Device * dev, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<app::ConcreteAttributePath>(dev->GetEndpointId(), cluster, attribute);
    DeviceLayer::PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}
} // anonymous namespace

void HandleDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask)
{
    if (itemChangedMask & Device::kChanged_Reachable)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & Device::kChanged_State)
    {
        ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }

    if (itemChangedMask & Device::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

/**
 * Callback function that is invoked if a device tries to invoke a command of a device that is bridged
 * The function will translate the invoke interaction into a CoAP PUT request
 */ 
bool emberAfActionsClusterInstantActionCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                const Actions::Commands::InstantAction::DecodableType & commandData)
{
    // Get the cluster id as well as the command id from the incomming command 
    ClusterId cluster_id = commandPath.mClusterId;
    CommandId command_id = commandPath.mCommandId;
    // Translate the cluster and attribute id into a object and a resource id
    int ipso_object_id = matter_mapping.cluster_object_map.get_ipso_id(cluster_id);
    int ipso_resource_id = matter_mapping.command_resource_map.get_ipso_id(command_id);
    std::string target = "coap://[fd73:13f6:c3ed:1:d8bd:9673:d9cd:a562]:5184/";
    // Build the target uri basd on the translated ids
    target.append(std::to_string(ipso_object_id));
    target.append("/0/");
    target.append(std::to_string(ipso_resource_id));
    // commandData contains the data of the command
    // For this PoC we limited the PUT request to a request without a payload
    // Send the CoAP PUT request
    CoapClientPut(target.c_str());

    // Return the status success
    commandObj->AddStatus(commandPath, Protocols::InteractionModel::Status::Success);
    return true;
}

// Set the device types for the endpoints
// These are partially leftover from the original implementation
const EmberAfDeviceType gRootDeviceTypes[]          = { { DEVICE_TYPE_ROOT_NODE, DEVICE_VERSION_DEFAULT } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { DEVICE_TYPE_BRIDGE, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gBridgedOnOffDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                                       { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };
/**
 * Function used to generate a mapping between Matter and LwM2M based on the combined sdf-mappings
 * The function creates a special structure that can be used to easily translate the ids of both ecosystems
 */ 
MatterIpsoMapping GenerateMatterIpsoMapping(const json& json)
{
    MatterIpsoMapping mapping;
    if (json.contains("map")) {
        // Iterate through the map section
        for (auto it = json.at("map").begin(); it != json.at("map").end(); ++it) {
            int matter_id = 0;
            int oma_id = 0;
            if (it.value().contains("matter:id") and it.value().contains("oma:id")) {
                // Create the mapping and insert the id in the corresponding map
                it.value().at("matter:id").get_to(matter_id);
                it.value().at("oma:id").get_to(oma_id);
                if (ExtractBetweenSlashes(it.key()) == "sdfThing") {
                    // As there is no equivalent for sdfThing in LwM2M, we ignore its id
                } else if (ExtractBetweenSlashes(it.key()) == "sdfObject") {
                    mapping.cluster_object_map.insert(matter_id, oma_id);
                } else if (ExtractBetweenSlashes(it.key()) == "sdfProperty") {
                    mapping.attribute_resource_map.insert(matter_id, oma_id);
                } else if (ExtractBetweenSlashes(it.key()) == "sdfAction") {
                    mapping.command_resource_map.insert(matter_id, oma_id);
                } else if (ExtractBetweenSlashes(it.key()) == "sdfEvent") {
                    mapping.event_resource_map.insert(matter_id, oma_id);
                }
            }
        }
    }

    return mapping;
}
 
/**
 * Function used to generate CoAP resources based on an LwM2M object definition
 */ 
void GenerateCoapResource(ObjectDefinition& object_definition)
{
    // Register a CoAP resource for every resource defined in the object
    for (auto& resource : object_definition.resources) {
        // Create a URI of the format /<OBJECT_ID>/0/<RESOURCE_ID>
        // Depending on the operations, we register different coap ressources
        // Register a read ressource
        if (resource.operations.find('R') != std::string::npos)
        {
            // Register a read write resource
            if (resource.operations.find('W') != std::string::npos)
            {
                std::string uri_str;
                uri_str.append(std::to_string(object_definition.id)).append("/0/").append(std::to_string(resource.id));
                RegisterAttributeRWResource(uri_str.c_str(), resource.type);
            } 
            // register a read resource
            else {
                std::string uri_str;
                uri_str.append(std::to_string(object_definition.id)).append("/0/").append(std::to_string(resource.id));
                RegisterAttributeResource(uri_str.c_str(), COAP_REQUEST_GET, resource.type);
            }
        }

        // Register a read write resource
        else if (resource.operations.find('W') != std::string::npos)
        {
            std::string uri_str;
            uri_str.append(std::to_string(object_definition.id)).append("/0/").append(std::to_string(resource.id));
            RegisterAttributeResource(uri_str.c_str(), COAP_REQUEST_PUT, resource.type);
        }

        // Register a execute resource
        if (resource.operations.find('E') != std::string::npos)
        {
            std::string uri_str;
            uri_str.append(std::to_string(object_definition.id)).append("/0/").append(std::to_string(resource.id));
            RegisterCommandResource(uri_str.c_str());
        }
    }
}

/**
 * Function used to completely initialize and start the CoAP server
 * This includes the generation of the CoAP resources based on the LwM2M object definition
 */
static void InitCoapServer(void *args)
{
    while (true)
    {
        if (esp_netif_is_netif_up(esp_netif_get_default_netif())) {
            ChipLogProgress(DeviceLayer, "CoAP Server: Primary interface is up!");
            esp_ip6_addr_t ip6_addr[10];
            int ret;
            ret = esp_netif_get_all_ip6(esp_netif_get_default_netif(), ip6_addr);
            if (ret > 1) {
                ChipLogProgress(DeviceLayer, "CoAP Server: Found: %i Adresses", ret);
                for (int i = 0; i < ret; i++) {
                    ChipLogProgress(DeviceLayer, "CoAP Server: Address: %s", Ip6ToStr(ip6_addr[i]).c_str());
                }

                if (ret >= 3)
                {
                    ChipLogProgress(DeviceLayer, "CoAP Client: Loading LwM2M configuration file as well as the SDF-Mapping");
                    vTaskDelay(1000);

                    // Load the LwM2M configuration via CoAP
                    const char* lwm2m_xml_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/xml/lwm2m-xml";
                    LoadLwm2mFile(lwm2m_xml_uri);

                    // Parse the loaded xml file into an object
                    ObjectDefinition object_definition;
                    object_definition = ParseObjectDefinition(lwm2m_xml_file);
                    lwm2m_xml_file.reset();

                    // Initialize the CoAP Server
                    ChipLogProgress(DeviceLayer, "CoAP Server: Starting CoAP Server!");
                    ChipLogProgress(DeviceLayer, "CoAP Server: Using Address: %s", Ip6ToStr(ip6_addr[2]).c_str());
                    init_server(Ip6ToStr(ip6_addr[2]).c_str());

                    // Generate the custom ressources based on the parsed LwM2M object definition
                    ChipLogProgress(DeviceLayer, "Generating Custom Resources");
                    GenerateCoapResource(object_definition);
                    ChipLogProgress(DeviceLayer, "Generated Custom Resources");

                    start_server();
                    break;
                } else {
                    vTaskDelay(5000);
                }
                

            } else {
                ChipLogProgress(DeviceLayer, "CoAP Server: No global IPv6 Address configured, cannot initialize CoAP Server!");
                vTaskDelay(5000);
            }
        } else {
            ChipLogProgress(DeviceLayer, "CoAP Server: Primary interface is currently down, retrying in 10 seconds...");
            vTaskDelay(5000);
        }
    }
    ChipLogProgress(DeviceLayer, "CoAP Server: CoAP Server has been initialized!");
}

/**
 * Function used to convert a sdf-model and sdf-mapping to the Matter data model
 * The function also creates a dynamic endpoint based on these converted definitions
 */
int ConvertAndDeployMatter()
{
    ChipLogProgress(DeviceLayer, "CoAP Client: Loading SDF configuration files");
    vTaskDelay(1000);

    // Load the sdf-model
    const char* sdf_model_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/sdf/sdf-model";
    LoadSdfModelFile(sdf_model_uri);
    vTaskDelay(1000);

    // Load the Matter specific sdf-mapping
    const char* sdf_mapping_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/sdf/sdf-mapping";
    LoadSdfMappingMatterFile(sdf_mapping_uri);

    ChipLogProgress(DeviceLayer, "CoAP Client: Finished loading configuration files");

    // Convert the sdf-model and the sdf-mapping to a device type definition and a list of cluster definitions
    ChipLogProgress(DeviceLayer, "SDF-Matter-Converter: Converting SDF to Matter");
    static matter::Device converted_device;
    static std::list<matter::Cluster> clusters;
    ConvertSdfToMatter(sdf_model_file, sdf_mapping_matter_file, converted_device, clusters);
    sdf_model_file.clear();
    sdf_mapping_matter_file.clear();
    ChipLogProgress(DeviceLayer, "SDF-Matter-Converter: Converted Device: %s", converted_device.name.c_str());
    ChipLogProgress(DeviceLayer, "SDF-Matter-Converter: Converted SDF to Matter!");

    // Create a dynamic endpoint based on the converted device type definition and the list of cluster definitions
    ChipLogProgress(DeviceLayer, "Generating and deploying converted Matter device");
    CreateCustomDevice(converted_device, clusters);
    ChipLogProgress(DeviceLayer, "Deployed converted Matter device");

    return 0;
}

/**
 * Function used to initialize the Matter bridge
 */
static void InitServer(intptr_t context)
{
    PrintOnboardingCodes(chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

    Esp32AppServer::Init(); // Init ZCL Data Model and CHIP App Server AND Initialize device attestation config

    // Initialize the Binding Handler
    InitBindingHandler();

    // Set starting endpoint id where dynamic endpoints will be assigned, which
    // will be the next consecutive endpoint id after the last fixed endpoint.
    gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generate the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // A bridge has root node device type on EP0 and aggregate node device type (bridge) at EP1
    emberAfSetDeviceTypeList(0, Span<const EmberAfDeviceType>(gRootDeviceTypes));
    emberAfSetDeviceTypeList(1, Span<const EmberAfDeviceType>(gAggregateNodeDeviceTypes));

    // Add lights 1
    // Still remaining as part of the original bridge to showcase the original usecase
    AddDeviceEndpoint(&gLight1, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                      Span<DataVersion>(gLight1DataVersions), 1);
    
    // Convert SDF to Matter and generate a endpoint based on the given information
    ConvertAndDeployMatter();

    // Generate the link between LwM2M and Matter data model elements by utilizing the combined sdf-mappings
    ChipLogProgress(DeviceLayer, "Generating the mappers");
    sdf_mapping_matter_file.clear();
    // Load the LwM2M specific mapping
    const char* sdf_mapping_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/sdf/sdf-lwm2m-to-matter-merged";
    LoadSdfMappingMatterFile(sdf_mapping_uri);
    coap_mapping = GenerateMatterIpsoMapping(sdf_mapping_matter_file);
    ChipLogProgress(DeviceLayer, "Generated the mappers!");

    // Load the Matter specific mapping
    const char* sdf_mapping_lwm2m_uri = "coap://[2a02:8109:c40:7cc6:8150:45c1:c796:5026]:5683/sdf/sdf-matter-to-lwm2m-merged";
    LoadSdfMappingLwm2mFile(sdf_mapping_lwm2m_uri);
    matter_mapping = GenerateMatterIpsoMapping(sdf_mapping_lwm2m_file);

    // Create the CoAP Server
    // Note that FreeRTOS task are not allowed to terminate
    // They have to be explicitly terminated with vTaskDelete
    ChipLogProgress(DeviceLayer, "Starting Server");
    xTaskCreate(&InitCoapServer, "coap_server", 4096, NULL, 5, NULL);

    // Check if the Device is reachable
    if (DeviceLayer::Internal::ESP32Utils::IsInterfaceUp("ot1"))
    {
        ChipLogError(DeviceLayer, "Bridge-Handler: Interface is up");
    } else {
        ChipLogError(DeviceLayer, "Bridge-Handler: Interface is down");
    }

    ChipLogProgress(DeviceLayer, "Bridge-Handler: Successfully started server!");
}

extern "C" void app_main()
{
    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default()  failed: %s", esp_err_to_name(err));
        return;
    }

    // bridge will have own database named gDevices.
    // Clear database
    memset(gDevices, 0, sizeof(gDevices));

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (DeviceLayer::Internal::ESP32Utils::InitWiFiStack() != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to initialize the Wi-Fi stack");
        return;
    }
#endif

    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    CHIP_ERROR chip_err;
    chip_err = deviceMgr.Init(&AppCallback);
    if (chip_err != CHIP_NO_ERROR)
    {   
        ChipLogError(DeviceLayer, "Failed to initialize the device manager!");
        return;
    }

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
    SetCommissionableDataProvider(&sFactoryDataProvider);
    SetDeviceAttestationCredentialsProvider(&sFactoryDataProvider);
#if CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER
    SetDeviceInstanceInfoProvider(&sFactoryDataProvider);
#endif
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

    ESPOpenThreadInit();

    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, reinterpret_cast<intptr_t>(nullptr));

    // Start the AppTask used for the button
    chip_err = GetAppTask().StartAppTask();
    if (chip_err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to start AppTask!");
    }
}
