#include "solver_functions.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <random> // For random selection if needed, or shuffling indices
#include <cmath>  // For ceil in sample count calculation (though samples_per_component is decided by main)
#include <filesystem> // For path operations

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
    // contents.num_total_samples_requested = mapping_data.value("num_total_samples_requested", 0); // If stored here

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
    // Sort components by ID to ensure consistent processing order
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

    std::vector<json> component_assignment_lists; // Each element is an assignment_list (array of samples) for a component
    std::vector<size_t> num_samples_per_component;

    for (const auto& comp_map_entry : mapping_info.components) {
        std::filesystem::path comp_result_path = std::filesystem::path(run_dir) / (comp_map_entry.aig_file_stub + "_result.json");
        std::ifstream comp_result_ifs(comp_result_path);
        if (!comp_result_ifs.is_open()) {
            std::cerr << "Error: Cannot open component result file: " << comp_result_path << std::endl;
            // This implies overall UNSAT or an error in the pipeline before merger
            // For now, assume UNSAT and write empty list.
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
        // This case should be caught by individual component checks, but as a safeguard:
        // If there were supposed to be components but we have no assignment lists, something is wrong.
        // If mapping_info.components was also empty, it's a trivial SAT (handled by JVC usually).
        std::cerr << "Warning: No component assignment lists loaded, but components were expected." << std::endl;
    }
    
    if (mapping_info.components.empty()) { // No components to merge, means original problem was trivial SAT
        json final_result_json;
        final_result_json["assignment_list"] = json::array(); // Typically one empty assignment for "always true"
                                                            // Or specific format if needed.
                                                            // For now, an empty list of assignments means SAT if no constraints.
                                                            // If there were variables, one assignment of all zeros might be expected.
                                                            // But this case should be handled by json_v_converter producing a single trivial file.
        std::ofstream ofs(final_output_json_path);
        ofs << final_result_json.dump(4) << std::endl;
        ofs.close();
        return 0;
    }


    json final_assignment_list = json::array();
    std::set<std::string> unique_full_assignment_signatures;
    
    std::vector<size_t> current_indices(component_assignment_lists.size(), 0);
    std::mt19937 rng(random_seed_for_merging); // For shuffling or random picking if needed

    // To get diverse samples, we can iterate through combinations systematically
    // or pick component samples randomly. Systematic iteration is simpler first.

    long long total_possible_combinations = 1;
    for(size_t count : num_samples_per_component) {
        if (count == 0) { // Should have been caught by UNSAT check above
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
         // This means some component had 0 samples, should be caught by UNSAT logic.
         // Writing empty assignment list as a fallback.
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
            
            const json& one_sample_from_comp = samples_for_this_comp[sample_idx_for_comp]; // This is an array of {"value": "hex"}

            // The one_sample_from_comp array corresponds to variables in comp_map_entry.variable_names
            // AND ALSO to variables in that component's mini-JSON variable_list.
            if (one_sample_from_comp.size() != comp_map_entry.variable_names.size()) {
                 std::cerr << "Mismatch in variable count for component " << comp_map_entry.component_id << std::endl;
                 return 1; // Data integrity error
            }

            for (size_t var_k = 0; var_k < comp_map_entry.variable_names.size(); ++var_k) {
                const std::string& original_var_name = comp_map_entry.variable_names[var_k];
                current_full_assignment_map[original_var_name] = one_sample_from_comp[var_k].value("value", "0");
            }
        }

        // Construct the final JSON entry based on original_variable_list order
        json one_final_assignment_entry = json::array();
        for (const auto& orig_var_json : mapping_info.original_variable_list_json) {
            std::string orig_var_name = orig_var_json.value("name", "");
            int bit_width = orig_var_json.value("bit_width",1);
            std::string val_hex = "0"; // Default if var not in any component (should not happen for constrained vars)
            if (current_full_assignment_map.count(orig_var_name)) {
                val_hex = current_full_assignment_map[orig_var_name];
            } else {
                // If a variable from original_variable_list was not in any component,
                // it means it was not part of any constraint. Assign default (e.g., 0).
                // This requires to_hex_string to be available or use a pre-formatted zero.
                // For simplicity, use "0", but proper hex width might be desired.
                // val_hex = to_hex_string(0, bit_width); // if to_hex_string is linkable here
            }
            one_final_assignment_entry.push_back({{"value", val_hex}});
        }
        
        std::string assignment_signature = one_final_assignment_entry.dump();
        if (unique_full_assignment_signatures.insert(assignment_signature).second) {
            final_assignment_list.push_back(one_final_assignment_entry);
            generated_count++;
        }

        // Advance current_indices (like incrementing a mixed-radix number)
        int k_radix = 0;
        while(k_radix < current_indices.size()){
            current_indices[k_radix]++;
            if(current_indices[k_radix] < num_samples_per_component[k_radix]) break; // No carry
            current_indices[k_radix] = 0; // Reset and carry
            k_radix++;
        }
        if(k_radix == current_indices.size()) break; // All combinations exhausted

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