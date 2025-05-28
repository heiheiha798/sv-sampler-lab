// File: main.cpp
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>      // For reading manifest
#include <cstdlib>      // For system()
#include "solver_functions.h"
#include "nlohmann/json.hpp" // For parsing manifest

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

// Forward declarations for functions that will be in this file or linked
int json_v_converter(const std::string &input_json_path, const std::string &output_v_dir);
int aig_to_bdd_solver(const string &aig_file_path, const string &original_json_path, int num_samples, const string &result_json_path, unsigned int random_seed);
int merge_bdd_results(const string &manifest_path_str, const string &final_output_json_path_str, int total_samples_required, unsigned int random_seed);


// Helper to create component-specific variable list JSON
// The order of variables in this list MUST match PIs in AIG (derived from Verilog)
bool create_component_vars_json(const json& component_pi_info, const fs::path& output_path) {
    json vars_json;
    vars_json["variable_list"] = json::array();

    // The component_pi_info is already ordered as it appeared in the Verilog's input list
    // which Yosys should preserve for AIGER PIs.
    for (const auto& pi_entry : component_pi_info) {
        json var_entry;
        // These names/IDs are for aig_to_bdd_solver's internal JSON representation.
        // The important part is that `aig_to_bdd_solver` will output values
        // in this same order. The merger will then use `original_json_id` for mapping.
        var_entry["name"] = pi_entry["original_name"]; 
        var_entry["bit_width"] = pi_entry["bit_width"];
        // Add a reference to original ID if merger needs it directly from component results,
        // but current merger gets it from manifest.
        // var_entry["original_json_id"] = pi_entry["original_json_id"]; 
        vars_json["variable_list"].push_back(var_entry);
    }

    ofstream out_stream(output_path);
    if (!out_stream.is_open()) {
        cerr << "Error: Failed to create component vars json: " << output_path << endl;
        return false;
    }
    out_stream << vars_json.dump(4) << endl;
    out_stream.close();
    return true;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        cerr << "Usage: MySolver <mode> [options...]" << endl;
        cerr << "Modes:" << endl;
        cerr << "  json-to-v <input.json> <output_verilog_dir>" << endl;
        cerr << "  aig-to-bdd <input.aag> <component_vars.json> <num_samples> <output_result.json> <seed>" << endl;
        cerr << "  merge-results <manifest.json> <final_output.json> <num_samples> <seed>" << endl;
        cerr << "  solve-decomposed <input_constraint.json> <num_samples> <run_dir> <seed> [yosys_path]" << endl;
        return 1;
    }

    string mode = argv[1];

    if (mode == "json-to-v" && argc == 4) {
        return json_v_converter(argv[2], argv[3]);
    } else if (mode == "aig-to-bdd" && argc == 7) {
        return aig_to_bdd_solver(argv[2], argv[3], stoi(argv[4]), argv[5], stoul(argv[6]));
    } else if (mode == "merge-results" && argc == 6) {
        return merge_bdd_results(argv[2], argv[3], stoi(argv[4]), stoul(argv[5]));
    } else if (mode == "solve-decomposed" && (argc == 6 || argc == 7)) {
        string constraint_json_path = argv[2];
        int num_samples = stoi(argv[3]);
        string run_dir_str = argv[4];
        unsigned int random_seed = stoul(argv[5]);
        string yosys_executable = (argc == 7) ? argv[6] : "yosys"; // Default to "yosys" if not provided

        fs::path run_dir(run_dir_str);
        fs::create_directories(run_dir); // Ensure run_dir exists

        // 1. Convert main JSON to component Verilog files and manifest.json
        cout << "Step 1: Converting JSON to component Verilog files..." << endl;
        int conv_ret = json_v_converter(constraint_json_path, run_dir_str);
        if (conv_ret == 1) { // General error
            cerr << "Error in json_v_converter." << endl;
            return 1;
        }
        
        fs::path manifest_path = run_dir / "manifest.json";
        ifstream manifest_stream(manifest_path);
        if (!manifest_stream.is_open()) {
            cerr << "Error: manifest.json not found after json_v_converter call in " << run_dir_str << endl;
            return 1;
        }
        json manifest_data;
        manifest_stream >> manifest_data;
        manifest_stream.close();

        if (conv_ret == 2 || manifest_data.value("unsat_by_preprocessing", false)) {
             cout << "Problem determined to be UNSAT by json_v_converter. Skipping BDD steps." << endl;
             // Merger will handle this based on manifest flag
        } else {
            // 2. For each component: Verilog -> AIG -> BDD solve
            json components_array = manifest_data.value("components", json::array());
            cout << "Step 2: Processing " << components_array.size() << " components..." << endl;

            for (const auto& comp_entry : components_array) {
                string comp_v_file_name = comp_entry["verilog_file"].get<string>();
                string comp_aig_file_name = comp_entry["aig_file"].get<string>(); // e.g., component_0.aag
                string comp_result_json_name = comp_entry["result_json_file"].get<string>();
                int comp_id = comp_entry["id"].get<int>();

                fs::path comp_v_path = run_dir / comp_v_file_name;
                fs::path comp_aig_path = run_dir / comp_aig_file_name;
                fs::path comp_result_path = run_dir / comp_result_json_name;
                fs::path temp_comp_vars_json_path = run_dir / ("temp_vars_component_" + to_string(comp_id) + ".json");

                cout << "  Processing component " << comp_id << ": " << comp_v_file_name << endl;

                if (comp_entry.value("is_trivial_sat", false) || comp_entry.value("is_trivial_unsat", false)) {
                    cout << "    Component " << comp_id << " is trivial. Skipping Yosys and BDD solver." << endl;
                    // If UNSAT, merger will catch it. If SAT, merger knows it has no specific variable samples.
                    // Create an empty assignment list file for trivial sat if merger expects one
                    if (comp_entry.value("is_trivial_sat", false)) {
                        json trivial_sat_result;
                        trivial_sat_result["assignment_list"] = json::array(); // Empty, as no specific assignment needed from solver
                        ofstream ts_out(comp_result_path);
                        ts_out << trivial_sat_result.dump(4) << endl;
                        ts_out.close();
                    }
                    continue;
                }

                // 2a. Verilog to AIG (using Yosys)
                cout << "    Converting " << comp_v_path.filename() << " to AIG..." << endl;
                string yosys_script = "read_verilog " + comp_v_path.string() +
                                      "; synth -top " + comp_entry["verilog_file"].get<string>().substr(0, comp_entry["verilog_file"].get<string>().find_last_of('.')) +
                                      "; abc; aigmap; opt; clean; write_aiger -ascii " + comp_aig_path.string() + ";";
                string yosys_command = yosys_executable + " -q -p \"" + yosys_script + "\"";
                
                // cout << "    Yosys command: " << yosys_command << endl;
                int yosys_ret = system(yosys_command.c_str());
                if (yosys_ret != 0) {
                    cerr << "Error: Yosys conversion failed for " << comp_v_path << endl;
                    // Decide if this is fatal for the whole run or just this component
                    // For now, let's assume it means this component is unsolvable or problematic
                    // The merger should detect a missing result file.
                    continue; // Skip to next component
                }

                // 2b. Create temporary JSON for component's variables for aig_to_bdd_solver
                if (!create_component_vars_json(comp_entry["primary_inputs"], temp_comp_vars_json_path)) {
                    cerr << "Error: Failed to create temp vars JSON for component " << comp_id << endl;
                    continue; // Skip to next component
                }

                // 2c. AIG to BDD solve
                cout << "    Solving AIG for component " << comp_id << "..." << endl;
                int samples_for_this_component;
                
                // Calculate samples based on N^(1/k) + 100 strategy
                // Only exclude trivial_unsat components from k calculation
                int total_components = components_array.size();
                int effective_components_for_sampling = 0;
                
                // Count components that can contribute to sampling (exclude only trivial_unsat)
                for (const auto& comp : components_array) {
                    if (!comp.value("is_trivial_unsat", false)) {
                        effective_components_for_sampling++;
                    }
                }
                
                if (effective_components_for_sampling > 0 && num_samples > 0) {
                    // Calculate N^(1/k) where N = num_samples, k = effective_components_for_sampling
                    double base_samples = std::pow(static_cast<double>(num_samples), 1.0 / static_cast<double>(effective_components_for_sampling));
                    samples_for_this_component = static_cast<int>(std::ceil(base_samples)) + 50;
                    
                    // Ensure minimum samples for edge cases
                    if (samples_for_this_component < 1) {
                        samples_for_this_component = 1;
                    }
                } else if (num_samples == 0) {
                    samples_for_this_component = 0;
                } else {
                    // Fallback: should not happen if we have valid components
                    samples_for_this_component = 100;
                }
                
                int bdd_ret = aig_to_bdd_solver(comp_aig_path.string(), 
                                                temp_comp_vars_json_path.string(), 
                                                samples_for_this_component, 
                                                comp_result_path.string(), 
                                                random_seed + comp_id); // Vary seed per component
                if (bdd_ret != 0) {
                    cerr << "Warning: aig_to_bdd_solver failed for component " << comp_id 
                         << ". Result file might be empty or indicate no solution." << endl;
                    // Merger will check if component_X_results.json is empty
                }
                 fs::remove(temp_comp_vars_json_path); // Clean up temp file
            }
        }

        // 3. Merge results
        cout << "Step 3: Merging component results..." << endl;
        fs::path final_result_json_path = run_dir / "result.json";
        int merge_ret = merge_bdd_results(manifest_path.string(), final_result_json_path.string(), num_samples, random_seed);
        if (merge_ret != 0) {
            cerr << "Error in results_merger." << endl;
            return 1;
        }
        
        cout << "Solve-decomposed finished. Final results in " << final_result_json_path << endl;
        return 0;

    } else {
        cerr << "Invalid mode or incorrect number of arguments for mode '" << mode << "'." << endl;
        // Print usage again
        cerr << "Usage: MySolver <mode> [options...]" << endl;
        cerr << "Modes:" << endl;
        cerr << "  json-to-v <input.json> <output_verilog_dir>" << endl;
        cerr << "  aig-to-bdd <input.aag> <component_vars.json> <num_samples> <output_result.json> <seed>" << endl;
        cerr << "  merge-results <manifest.json> <final_output.json> <num_samples> <seed>" << endl;
        cerr << "  solve-decomposed <input_constraint.json> <num_samples> <run_dir> <seed> [yosys_path]" << endl;
        return 1;
    }
    return 0;
}