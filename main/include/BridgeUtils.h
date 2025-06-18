#ifndef BRIDGE_UTILS_H
#define BRIDGE_UTILS_H

#include "esp_netif.h"
#include <string>
#include <pugixml.hpp>
#include <nlohmann/json.hpp>
#include "sdf_to_matter.h"
#include "matter_to_sdf.h"
#include "matter.h"
#include "sdf.h"
#include <list>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/ZclString.h>

std::string Ip6ToStr(esp_ip6_addr_t &ip6addr);

// Implementation of a bidirectional map
class BiMap {
public:
    // Insert a pair into the bimap
    void insert(int matter_id, int ipso_id) {
        if (left_map.find(matter_id) != left_map.end() || right_map.find(ipso_id) != right_map.end()) {
            ChipLogError(DeviceLayer, "Tried to insert duplicate key!");
        } else {
            left_map[matter_id] = ipso_id;
            right_map[ipso_id] = matter_id;
        }
    }

    // Get LwM2M ID from Matter ID
    int get_ipso_id(int matter_id) const {
        auto it = left_map.find(matter_id);
        if (it != left_map.end()) {
            return it->second;
        }
        ChipLogError(DeviceLayer, "Couldn't find LwM2M ID");
        return -1;
    }

    // Get Matter ID from LwM2M ID
    int get_matter_id(int ipso_id) const {
        auto it = right_map.find(ipso_id);
        if (it != right_map.end()) {
            return it->second;
        }
        ChipLogError(DeviceLayer, "Couldn't find Matter ID");
        return -1;
    }

private:
    std::unordered_map<int, int> left_map;
    std::unordered_map<int, int> right_map;
};

// Struct to use for the Matter <-> LwM2M ID mapping
// Contains multiple of the above structures
struct MatterIpsoMapping {
    BiMap cluster_object_map;
    BiMap attribute_resource_map;
    BiMap command_resource_map;
    BiMap event_resource_map;
};

/**
 * Custom implementation of ConvertSdfToMatter that returns objects instead of the serialized files 
 */
static inline int ConvertSdfToMatter(nlohmann::ordered_json& sdf_model_json,
                                    nlohmann::ordered_json& sdf_mapping_json, 
                                    matter::Device& device, 
                                    std::list<matter::Cluster>& clusters)
{
    sdf::SdfModel sdf_model = sdf::ParseSdfModel(sdf_model_json);
    sdf::SdfMapping sdf_mapping = sdf::ParseSdfMapping(sdf_mapping_json);
    std::optional<matter::Device> optional_device;
    MapSdfToMatter(sdf_model, sdf_mapping, optional_device, clusters);
    if (optional_device.has_value()) {
        device = optional_device.value();
    }

    return 0;
}

/**
 * Custom implementation of ConvertMatterToSdf that returns objects instead of the serialized files 
 */
static inline int ConvertMatterToSdf(pugi::xml_document& device_xml, const std::list<pugi::xml_document>& cluster_xml_list, sdf::SdfModel& sdf_model, sdf::SdfMapping& sdf_mapping)
{
    std::list<matter::Cluster> cluster_list;
    for (auto const& cluster_xml : cluster_xml_list) {
        matter::Cluster cluster =  matter::ParseCluster(cluster_xml.document_element());
        cluster_list.push_back(cluster);
    }

    matter::Device device = matter::ParseDevice(device_xml.document_element());
    MapMatterToSdf(device, cluster_list, sdf_model, sdf_mapping);

    return 0;
}

// Makros based on the Makros defined in attribute-storage.h
// These are altered to support dynamic arrays that can be used at runtime
#define DECLARE_DYNAMIC_CUSTOM_CLUSTER_LIST_BEGIN(clusterListName) EmberAfCluster clusterListName[] = {

// Makro requires the following informations
// - Cluster ID
// - Pointer to a vector containing the cluster attributes
// - Number of elements in the vector of cluster attributes
// - Pointer to a vector containing the cluster client commands
// - Pointer to a vector containing the cluster server commands
#define DECLARE_DYNAMIC_CUSTOM_CLUSTER(clusterId, clusterAttrs, clusterAttrsSize, incomingCommands, outgoingCommands)                                       \
    {                                                                                                                              \
        clusterId, clusterAttrs, clusterAttrsSize, 0, ZAP_CLUSTER_MASK(SERVER), NULL, incomingCommands, outgoingCommands    \
    }

#define DECLARE_DYNAMIC_CUSTOM_CLIENT_CLUSTER(clusterId, clusterAttrs, clusterAttrsSize, incomingCommands, outgoingCommands)                                       \
    {                                                                                                                              \
        clusterId, clusterAttrs, clusterAttrsSize, 0, ZAP_CLUSTER_MASK(CLIENT), NULL, incomingCommands, outgoingCommands    \
    }

#define DECLARE_DYNAMIC_CUSTOM_CLUSTER_LIST_END }

/**
 * Function used to extract the last string thats contained between two slashes
 */ 
inline std::string ExtractBetweenSlashes(const std::string& str) 
{
    // Find the position of the last backslash
    std::size_t lastBackslashPos = str.rfind('/');
    if (lastBackslashPos == std::string::npos) {
        // No backslash found, return an empty string
        return "";
    }

    // Find the position of the previous backslash before the last one
    std::size_t prevBackslashPos = str.rfind('/', lastBackslashPos - 1);
    if (prevBackslashPos == std::string::npos) {
        // No previous backslash found, return an empty string
        return "";
    }

    // Extract the substring between the two backslashes
    return str.substr(prevBackslashPos + 1, lastBackslashPos - prevBackslashPos - 1);
}

#endif //BRIDGE_UTILS_H