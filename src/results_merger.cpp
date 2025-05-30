#include "solver_functions.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <random> 
#include <cmath>  
#include <filesystem> 

using json = nlohmann::json;
using namespace std;


bool load_mapping_file(const std::string& mapping_json_path, MappingFileContents& contents) {
    std::ifstream ifs(mapping_json_path);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open mapping file: " << mapping_json_path << std::endl;
        return false;
    }
    json mapping_data;
    try {
        ifs >> mapping_data;
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Cannot parse mapping file: " << mapping_json_path << " (" << e.what() << ")" << std::endl;
        ifs.close();
        return false;
    }
    ifs.close();

    if (!mapping_data.contains("original_variable_list") || !mapping_data.contains("components")) {
        std::cerr << "Error: Mapping file missing required fields." << std::endl;
        return false;
    }
    contents.original_variable_list_json = mapping_data["original_variable_list"];

    for (const auto& comp_json : mapping_data["components"]) {
        ComponentMappingInfo info;
        info.component_id = comp_json.value("component_id", -1);
        info.verilog_file = comp_json.value("verilog_file", "");
        info.input_json_file = comp_json.value("input_json_file", "");
        info.aig_file_stub = comp_json.value("aig_file_stub", "");
        if (comp_json.contains("variable_names") && comp_json["variable_names"].is_array()) {
            for (const auto& name_json : comp_json["variable_names"]) {
                info.variable_names.push_back(name_json.get<std::string>());
            }
        }
        if (info.component_id == -1 || info.aig_file_stub.empty()) {
             std::cerr << "Error: Invalid component entry in mapping file." << std::endl;
             return false;
        }
        contents.components.push_back(info);
    }
    std::sort(contents.components.begin(), contents.components.end(), 
        [](const ComponentMappingInfo& a, const ComponentMappingInfo& b){
            return a.component_id < b.component_id;
        });
    return true;
}


int merge_results(const std::string& mapping_json_path,
                  const std::string& run_dir, 
                  int total_samples_to_generate,
                  const std::string& final_output_json_path,
                  unsigned int random_seed_for_merging)
{
    MappingFileContents mapping_info;
    if (!load_mapping_file(mapping_json_path, mapping_info)) {
        return 1; // Error loading/parsing mapping file
    }

    std::vector<json> component_assignment_lists;
    std::vector<size_t> num_samples_per_component;

    for (const auto& comp_map_entry : mapping_info.components) {
        std::filesystem::path comp_result_path = std::filesystem::path(run_dir) / (comp_map_entry.aig_file_stub + "_result.json");
        std::ifstream comp_result_ifs(comp_result_path);
        if (!comp_result_ifs.is_open()) {
            std::cerr << "Error: Cannot open component result file: " << comp_result_path << std::endl;
            json final_result_json;
            final_result_json["assignment_list"] = json::array();
            std::ofstream ofs(final_output_json_path);
            if (!ofs.is_open()) return 1; // Cannot write final result
            ofs << final_result_json.dump(4) << std::endl;
            ofs.close();
            return 0; // Indicate successful UNSAT output
        }
        json comp_result_data;
        try {
            comp_result_ifs >> comp_result_data;
        } catch (const json::parse_error& e) {
            std::cerr << "Error: Cannot parse component result file: " << comp_result_path << " (" << e.what() << ")" << std::endl;
            comp_result_ifs.close();
            return 1; // Parse error
        }
        comp_result_ifs.close();

        if (!comp_result_data.contains("assignment_list") || !comp_result_data["assignment_list"].is_array()) {
            std::cerr << "Error: Component result file " << comp_result_path << " has invalid format." << std::endl;
            return 1;
        }
        
        json current_assignments = comp_result_data["assignment_list"];
        if (current_assignments.empty()) { // This component is UNSAT
            std::cout << "Component " << comp_map_entry.component_id << " is UNSAT. Overall problem is UNSAT." << std::endl;
            json final_result_json;
            final_result_json["assignment_list"] = json::array();
            std::ofstream ofs(final_output_json_path);
            if (!ofs.is_open()) return 1;
            ofs << final_result_json.dump(4) << std::endl;
            ofs.close();
            return 0; // Successful UNSAT output
        }
        component_assignment_lists.push_back(current_assignments);
        num_samples_per_component.push_back(current_assignments.size());
    }

    if (component_assignment_lists.empty() && !mapping_info.components.empty()) {
        std::cerr << "Warning: No component assignment lists loaded, but components were expected." << std::endl;
    }
    
    if (mapping_info.components.empty()) {
        json final_result_json;
        final_result_json["assignment_list"] = json::array(); 


        std::ofstream ofs(final_output_json_path);
        ofs << final_result_json.dump(4) << std::endl;
        ofs.close();
        return 0;
    }


    json final_assignment_list = json::array();
    std::set<std::string> unique_full_assignment_signatures;
    
    std::vector<size_t> current_indices(component_assignment_lists.size(), 0);
    std::mt19937 rng(random_seed_for_merging); 

    long long total_possible_combinations = 1;
    for(size_t count : num_samples_per_component) {
        if (count == 0) { 
            total_possible_combinations = 0;
            break;
        }
        if ((double)total_possible_combinations * count > std::numeric_limits<long long>::max()) {
            total_possible_combinations = std::numeric_limits<long long>::max(); // Cap to avoid overflow
        } else {
            total_possible_combinations *= count;
        }
    }
    
    if (total_possible_combinations == 0 && !component_assignment_lists.empty()) {
        json final_result_json;
        final_result_json["assignment_list"] = json::array();
        std::ofstream ofs(final_output_json_path);
        ofs << final_result_json.dump(4) << std::endl;
        ofs.close();
        return 0; 
    }


    int generated_count = 0;
    long long combinations_tried = 0;

    while(generated_count < total_samples_to_generate && combinations_tried < total_possible_combinations) {
        std::map<std::string, std::string> current_full_assignment_map; // Original_var_name -> hex_value

        for (size_t comp_i = 0; comp_i < component_assignment_lists.size(); ++comp_i) {
            const auto& comp_map_entry = mapping_info.components[comp_i]; // Assumes sorted by ID
            const json& samples_for_this_comp = component_assignment_lists[comp_i];
            size_t sample_idx_for_comp = current_indices[comp_i];
            
            const json& one_sample_from_comp = samples_for_this_comp[sample_idx_for_comp]; 


            if (one_sample_from_comp.size() != comp_map_entry.variable_names.size()) {
                 std::cerr << "Mismatch in variable count for component " << comp_map_entry.component_id << std::endl;
                 return 1; // Data integrity error
            }

            for (size_t var_k = 0; var_k < comp_map_entry.variable_names.size(); ++var_k) {
                const std::string& original_var_name = comp_map_entry.variable_names[var_k];
                current_full_assignment_map[original_var_name] = one_sample_from_comp[var_k].value("value", "0");
            }
        }

        json one_final_assignment_entry = json::array();
        for (const auto& orig_var_json : mapping_info.original_variable_list_json) {
            std::string orig_var_name = orig_var_json.value("name", "");
            int bit_width = orig_var_json.value("bit_width",1);
            std::string val_hex = "0"; 
            if (current_full_assignment_map.count(orig_var_name)) {
                val_hex = current_full_assignment_map[orig_var_name];
            } else {
                // NoNeed
            }
            one_final_assignment_entry.push_back({{"value", val_hex}});
        }
        
        std::string assignment_signature = one_final_assignment_entry.dump();
        if (unique_full_assignment_signatures.insert(assignment_signature).second) {
            final_assignment_list.push_back(one_final_assignment_entry);
            generated_count++;
        }

        int k_radix = 0;
        while(k_radix < current_indices.size()){
            current_indices[k_radix]++;
            if(current_indices[k_radix] < num_samples_per_component[k_radix]) break; 
            current_indices[k_radix] = 0; 
            k_radix++;
        }
        if(k_radix == current_indices.size()) break; 

        combinations_tried++;
    }
    
    json final_result_json;
    final_result_json["assignment_list"] = final_assignment_list;
    std::ofstream ofs(final_output_json_path);
    if (!ofs.is_open()) return 1;
    ofs << final_result_json.dump(4) << std::endl;
    ofs.close();

    return 0; // Success
}