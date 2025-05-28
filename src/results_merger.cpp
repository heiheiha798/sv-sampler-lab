#include "solver_functions.h" // For to_hex_string, and potentially json type
#include "nlohmann/json.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <random>
#include <climits>
#include <algorithm> // For std::shuffle if needed
#include <filesystem> // For path manipulation

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

// (Optional, if to_hex_string is not in solver_functions.h or needs specific include)
// string to_hex_string(unsigned long long value, int bit_width); 

// This mt19937 can be seeded from main if deterministic merging is needed for testing
static std::mt19937 merge_rng(std::random_device{}()); 

int merge_bdd_results(const string &manifest_path_str, 
                      const string &final_output_json_path_str, 
                      int total_samples_required,
                      unsigned int random_seed) {
    merge_rng.seed(random_seed);
    fs::path manifest_fs_path(manifest_path_str);
    fs::path manifest_dir = manifest_fs_path.parent_path();

    json manifest_data;
    ifstream manifest_stream(manifest_fs_path);
    if (!manifest_stream.is_open()) {
        cerr << "Error: [Merger] Cannot open manifest file: " << manifest_path_str << endl;
        return 1;
    }
    try {
        manifest_stream >> manifest_data;
    } catch (const json::parse_error& e) {
        cerr << "Error: [Merger] Parsing manifest JSON failed: " << e.what() << endl;
        manifest_stream.close();
        return 1;
    }
    manifest_stream.close();

    if (manifest_data.value("unsat_by_preprocessing", false)) {
        cout << "Info: [Merger] Problem was marked UNSAT by preprocessing. Generating empty result." << endl;
        json empty_result;
        empty_result["assignment_list"] = json::array();
        empty_result["notes"] = "Problem determined UNSAT during preprocessing by json_v_converter.";
        
        ofstream final_out_stream(final_output_json_path_str);
        if (!final_out_stream.is_open()) {
            cerr << "Error: [Merger] Cannot write final empty result JSON: " << final_output_json_path_str << endl;
            return 1;
        }
        final_out_stream << empty_result.dump(4) << endl;
        final_out_stream.close();
        return 0; // Success in writing empty result for pre-UNSAT
    }

    int num_components = manifest_data.value("num_components", 0);
    json components_array = manifest_data.value("components", json::array());
    json original_variable_list_full; // To get structure for final output

    // Load the original variable list structure from the original JSON path
    // This is needed to structure the final combined assignment correctly.
    string original_json_full_path_str = manifest_data.value("original_json_path", "");
    if (original_json_full_path_str.empty()) {
        cerr << "Error: [Merger] 'original_json_path' not found in manifest." << endl;
        return 1;
    }
    ifstream original_json_full_stream(original_json_full_path_str);
    if (!original_json_full_stream.is_open()) {
        cerr << "Error: [Merger] Cannot open original JSON file for variable structure: " << original_json_full_path_str << endl;
        return 1;
    }
    json original_json_data_full;
    try {
        original_json_full_stream >> original_json_data_full;
        original_variable_list_full = original_json_data_full.value("variable_list", json::array());
    } catch (const json::parse_error& e) {
        cerr << "Error: [Merger] Parsing original JSON for variable structure failed: " << e.what() << endl;
        original_json_full_stream.close();
        return 1;
    }
    original_json_full_stream.close();
    if (original_variable_list_full.empty()) {
        cerr << "Error: [Merger] Original variable_list is empty in " << original_json_full_path_str << endl;
        return 1;
    }
    
    // Map original_json_id to its full info for easy lookup
    std::map<int, json> original_pi_id_to_full_info; // Key is the original "id"
    for(const auto& var_entry : original_variable_list_full) {
        // Use "id" from the original variable_list structure
        original_pi_id_to_full_info[var_entry["id"].get<int>()] = var_entry;
    }


    std::vector<std::vector<json>> component_sample_lists(num_components);
    std::vector<const json*> component_pi_info_ptrs(num_components);


    for (const auto& comp_entry : components_array) {
        int comp_id = comp_entry["id"].get<int>();
        if (comp_id >= num_components) {
            cerr << "Error: [Merger] Component ID " << comp_id << " out of bounds." << endl;
            return 1;
        }

        component_pi_info_ptrs[comp_id] = &comp_entry["primary_inputs"];

        if (comp_entry.value("is_trivial_unsat", false)) {
            cout << "Info: [Merger] Component " << comp_id << " is trivially UNSAT. Overall problem is UNSAT." << endl;
            json empty_result;
            empty_result["assignment_list"] = json::array();
            empty_result["notes"] = "Problem determined UNSAT due to component " + to_string(comp_id) + ".";
            ofstream final_out_stream(final_output_json_path_str);
            final_out_stream << empty_result.dump(4) << endl;
            final_out_stream.close();
            return 0; // Successfully wrote UNSAT result
        }

        if (comp_entry.value("is_trivial_sat", false)) {
            cout << "Info: [Merger] Component " << comp_id << " is trivially SAT. It contributes no constraints on its PIs for sampling." << endl;
            // It has no samples file, or an empty one. Treat as having one "empty" sample.
            // The PIs for this component will be "don't cares" effectively from this component's perspective.
            // When combining, we can assign them randomly or 0s.
            // For simplicity, we'll make component_sample_lists[comp_id] have one empty json object
            // to signify it's satisfied.
            component_sample_lists[comp_id].push_back(json::array()); // An empty assignment for its (zero) vars
        }
        
        fs::path comp_result_path = manifest_dir / comp_entry["result_json_file"].get<string>();
        ifstream comp_result_stream(comp_result_path);
        if (!comp_result_stream.is_open()) {
            cerr << "Warning: [Merger] Cannot open result file for component " << comp_id << ": " << comp_result_path 
                 << ". Assuming it has no solutions if not trivial_sat." << endl;
            // If it's not trivial SAT and file doesn't exist/empty, it means no solutions for this component.
            component_sample_lists[comp_id].clear(); // Ensure it's empty
        } else {
            json comp_result_data;
            try {
                comp_result_stream >> comp_result_data;
                component_sample_lists[comp_id] = comp_result_data.value("assignment_list", json::array());
            } catch (const json::parse_error& e) {
                cerr << "Warning: [Merger] Parsing result JSON for component " << comp_id << " failed: " << e.what() 
                     << ". Assuming no solutions." << endl;
                component_sample_lists[comp_id].clear();
            }
            comp_result_stream.close();
        }

        if (component_sample_lists[comp_id].empty() && !comp_entry.value("is_trivial_sat", false)) {
             cout << "Info: [Merger] Component " << comp_id << " has no solutions. Overall problem is UNSAT." << endl;
            json empty_result;
            empty_result["assignment_list"] = json::array();
            empty_result["notes"] = "Problem determined UNSAT because component " + to_string(comp_id) + " has no solutions.";
            ofstream final_out_stream(final_output_json_path_str);
            final_out_stream << empty_result.dump(4) << endl;
            final_out_stream.close();
            return 0; // Successfully wrote UNSAT result
        }
    }

    json final_assignment_list = json::array();
    std::set<string> unique_final_assignments_signatures;
    
    // Helper lambda to build final assignment and add if unique
    auto build_and_add_final_assignment = 
        [&](const json& current_full_assignment_map_param, // original_json_id (string) -> hex_value_string
            json& target_assignment_list, 
            std::set<string>& unique_signatures_set) // merge_rng and original_variable_list_full are captured
    {
        json final_assignment_entry = json::array();
        string current_signature_str = "";

        for (const auto& var_spec : original_variable_list_full) { // original_variable_list_full captured
            int current_var_original_id = var_spec["id"].get<int>(); 
            string hex_value_str;

            if (current_full_assignment_map_param.contains(to_string(current_var_original_id))) {
                hex_value_str = current_full_assignment_map_param[to_string(current_var_original_id)].get<string>();
            } else {
                int bit_width = var_spec["bit_width"].get<int>();
                unsigned long long random_val = 0;
                if (bit_width > 0) {
                    std::uniform_int_distribution<unsigned long long> dist_val;
                    if (bit_width < 64) {
                        dist_val = std::uniform_int_distribution<unsigned long long>(0, (1ULL << bit_width) - 1);
                    } else { // bit_width == 64
                        dist_val = std::uniform_int_distribution<unsigned long long>(0, ULLONG_MAX);
                    }
                    random_val = dist_val(merge_rng); // Use captured merge_rng
                }
                hex_value_str = to_hex_string(random_val, bit_width);
            }
            final_assignment_entry.push_back({{"value", hex_value_str}});
            current_signature_str += hex_value_str + ";";
        }
        
        if (unique_signatures_set.find(current_signature_str) == unique_signatures_set.end()) {
            unique_signatures_set.insert(current_signature_str);
            target_assignment_list.push_back(final_assignment_entry);
            return true;
        }
        return false;
    };

    bool single_normal_component_case = false;
    if (num_components == 1) {
        const auto& comp_entry_manifest = manifest_data["components"][0];
        if (!comp_entry_manifest.value("is_trivial_unsat", false) &&
            !comp_entry_manifest.value("is_trivial_sat", false) &&
            !component_sample_lists[0].empty()) { // Has actual solutions
            single_normal_component_case = true;
        }
    }

    if (single_normal_component_case) {
        const auto& samples_from_single_component = component_sample_lists[0];
        const json& comp_pis_info = *component_pi_info_ptrs[0]; // comp_idx is 0

        for (const auto& comp_sample : samples_from_single_component) {
            if (final_assignment_list.size() >= static_cast<size_t>(total_samples_required)) {
                break;
            }

            json current_full_assignment_map; // original_json_id (string) -> hex_value_string
            if (comp_pis_info.size() != comp_sample.size()) {
                 cerr << "Warning: [Merger] Mismatch in PI count (" << comp_pis_info.size() 
                      << ") and sample value count (" << comp_sample.size() 
                      << ") for single component. Skipping this sample." << endl;
                 continue; 
            }
            for (size_t pi_local_idx = 0; pi_local_idx < comp_pis_info.size(); ++pi_local_idx) {
                int original_json_id = comp_pis_info[pi_local_idx]["original_json_id"].get<int>();
                string hex_val = comp_sample[pi_local_idx]["value"].get<string>();
                current_full_assignment_map[to_string(original_json_id)] = hex_val;
            }
            build_and_add_final_assignment(current_full_assignment_map, final_assignment_list, unique_final_assignments_signatures);
        }
    } else { // Multiple components, or single trivial component, or no components (num_components == 0)
        std::vector<std::uniform_int_distribution<int>> dists;
        bool possible_to_generate_more_combinations = true; // Renamed for clarity
        for(int i=0; i<num_components; ++i) {
            if (component_sample_lists[i].empty()) { 
                // This implies a non-trivial_sat component has no solutions,
                // which should have been caught as UNSAT.
                // If trivial_sat, list has one `[]` sample, so not empty.
                cerr << "Error: [Merger] Component " << i << " has empty sample list unexpectedly." << endl;
                possible_to_generate_more_combinations = false;
                break;
            }
            dists.emplace_back(0, component_sample_lists[i].size() - 1);
        }
        
        if (!possible_to_generate_more_combinations && num_components > 0) {
             // Problem already indicated, or will result in no samples.
        }


        long long max_possible_combinations = 1;
        if (num_components > 0) { // Only calculate if there are components to combine
            for(int i=0; i<num_components; ++i) {
                if (component_sample_lists[i].empty()) { // Should not happen if possible_to_generate_more_combinations is true
                    max_possible_combinations = 0; break;
                }
                if ((double)max_possible_combinations * component_sample_lists[i].size() > 2e9 ) { 
                    max_possible_combinations = 2000000000; 
                    break;
                }
                max_possible_combinations *= component_sample_lists[i].size();
            }
        } else { // num_components == 0
            max_possible_combinations = 1; // Can generate 1 type of sample (all random)
        }

        long long attempts_for_uniqueness = std::min(max_possible_combinations, (long long)total_samples_required * 200); 
        if (num_components == 0 && total_samples_required > 0) { 
            attempts_for_uniqueness = std::max(1LL, (long long)total_samples_required); // Ensure enough attempts for all-random case
        } else if (total_samples_required == 0) {
            attempts_for_uniqueness = 0;
        }


        for (long long attempt = 0; 
            possible_to_generate_more_combinations && final_assignment_list.size() < static_cast<size_t>(total_samples_required) && attempt < attempts_for_uniqueness; 
            ++attempt) {
            
            json current_full_assignment_map; 
            bool current_combination_valid = true;

            for (int comp_idx = 0; comp_idx < num_components; ++comp_idx) {
                if (component_sample_lists[comp_idx].empty()) { // Should be caught by possible_to_generate_more_combinations
                    current_combination_valid = false; break;
                }
                
                int sample_idx_for_comp = dists[comp_idx](merge_rng);
                const json& comp_sample = component_sample_lists[comp_idx][sample_idx_for_comp]; 
                const json& comp_pis_info = *component_pi_info_ptrs[comp_idx]; 
                bool is_current_comp_trivial_sat = manifest_data["components"][comp_idx].value("is_trivial_sat", false);

                if (comp_pis_info.size() != comp_sample.size() && !is_current_comp_trivial_sat) {
                    cerr << "Error: [Merger] Mismatch in PI count and sample value count for component " << comp_idx << endl;
                    current_combination_valid = false; break;
                }

                if (!is_current_comp_trivial_sat) {
                    for (size_t pi_local_idx = 0; pi_local_idx < comp_pis_info.size(); ++pi_local_idx) {
                        if (pi_local_idx < comp_sample.size()) {
                            int original_json_id = comp_pis_info[pi_local_idx]["original_json_id"].get<int>();
                            string hex_val = comp_sample[pi_local_idx]["value"].get<string>(); 
                            current_full_assignment_map[to_string(original_json_id)] = hex_val; 
                        } else {
                            cerr << "Warning: [Merger] Sample array too short for PIs in non-trivial component " << comp_idx << endl;
                            current_combination_valid = false; break;
                        }
                    }
                }
                if (!current_combination_valid) break;
            }
            if (!current_combination_valid) continue; // Try next random combination

            build_and_add_final_assignment(current_full_assignment_map, final_assignment_list, unique_final_assignments_signatures);
        }
    }
    
    if (final_assignment_list.size() < static_cast<size_t>(total_samples_required) && total_samples_required > 0) {
        cout << "Warning: [Merger] Could only generate " << final_assignment_list.size() 
             << " unique samples out of " << total_samples_required << " requested." << endl;
    }

    json final_result_json_output;
    final_result_json_output["assignment_list"] = final_assignment_list;
    
    ofstream final_out_stream(final_output_json_path_str);
    if (!final_out_stream.is_open()) {
        cerr << "Error: [Merger] Cannot write final merged result JSON: " << final_output_json_path_str << endl;
        return 1;
    }
    final_out_stream << final_result_json_output.dump(4) << endl;
    final_out_stream.close();
    
    cout << "Info: [Merger] Successfully merged results into " << final_output_json_path_str << endl;
    return 0;
}