#ifndef SOLVER_FUNCTIONS_H
#define SOLVER_FUNCTIONS_H

#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map> // For results_merger
#include <set> // For results_merger

std::string to_hex_string(unsigned long long value, int bit_width);

int aig_to_bdd_solver(const std::string &aig_file_path,
                      const std::string &original_json_path, int num_samples,
                      const std::string &result_json_path,
                      unsigned int random_seed);

// Return codes for json_v_converter are standard 0 for success, 1 for file/parse error.
// The status_or_num_components output parameter carries detailed status:
// JVC_INTERNAL_ERROR (-1): Error during conversion logic.
// JVC_UNSAT_PREPROCESSED (-2): Problem is UNSAT based on constant propagation.
// JVC_SUCCESS_SINGLE_FILE (1): Generated a single Verilog file.
// >1 : Number of components generated (k files).
extern const int JVC_INTERNAL_ERROR;
extern const int JVC_UNSAT_PREPROCESSED;
extern const int JVC_SUCCESS_SINGLE_FILE;


int json_v_converter(const std::string &input_json_path,
                     const std::string &output_dir_for_files,
                     std::string &base_file_name_for_outputs_no_ext, // Out param
                     int *status_or_num_components);                 // Out param


struct ComponentMappingInfo {
    int component_id;
    std::string verilog_file;
    std::string input_json_file; // Path to the mini-JSON for this component
    std::string aig_file_stub;   // Base for component's .aig and _result.json
    std::vector<std::string> variable_names; // Original names of variables in this component
};

struct MappingFileContents {
    nlohmann::json original_variable_list_json;
    std::vector<ComponentMappingInfo> components;
    int num_total_samples_requested;
};

// Reads the .mapping.json file
bool load_mapping_file(const std::string& mapping_json_path, MappingFileContents& contents);


int merge_results(const std::string& mapping_json_path,
                  const std::string& run_dir, // To construct full paths to component results
                  int total_samples_to_generate,
                  const std::string& final_output_json_path,
                  unsigned int random_seed_for_merging);


#endif