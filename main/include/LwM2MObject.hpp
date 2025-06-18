#ifndef LWM2MOBJECT_H
#define LWM2MOBJECT_H

#include <pugixml.hpp>
#include <string>
#include <vector>
#include <map>

// Structure used to represent a LwM2M-Ressource
struct ResourceDefinition {
    int id;
    std::string name;
    std::string type;
    std::string operations;
    bool instance_mandatory;
    std::string state;
};

// Structure used to represent a LwM2M-Object
struct ObjectDefinition {
    int id;
    std::string name;
    std::vector<ResourceDefinition> resources;
};

/**
 * Function used to parse a LwM2M definition from an xml document into the above structures
 */ 
inline ObjectDefinition ParseObjectDefinition(const pugi::xml_document& xml_document) {
    ObjectDefinition obj_def;

    pugi::xml_node object_node = xml_document.document_element().child("Object");
    obj_def.id = object_node.child("ObjectID").text().as_int();
    obj_def.name = object_node.child("Name").text().as_string();

    for (pugi::xml_node resource_node : object_node.child("Resources").children("Item")) {
        ResourceDefinition res_def;
        res_def.id = resource_node.attribute("ID").as_int();
        res_def.name = resource_node.child("Name").text().as_string();
        res_def.type = resource_node.child("Type").text().as_string();
        res_def.operations = resource_node.child("Operations").text().as_string();
        res_def.instance_mandatory = resource_node.child("InstanceMandatory").text().as_bool();
        obj_def.resources.push_back(res_def);
    }

    return obj_def;
}

#endif //LWM2MOBJECT_H