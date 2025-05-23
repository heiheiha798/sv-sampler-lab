#ifndef SOLVER_FUNCTIONS_H
#define SOLVER_FUNCTIONS_H

#include "nlohmann/json.hpp"
#include <string>
#include <vector>

std::string to_hex_string(unsigned long long value, int bit_width);

int aig_to_bdd_solver(const std::string &aig_file_path,
                      const std::string &original_json_path, int num_samples,
                      const std::string &result_json_path,
                      unsigned int random_seed);

int json_v_converter(const std::string &input_json_path,
                     const std::string &output_v_dir);

#endif