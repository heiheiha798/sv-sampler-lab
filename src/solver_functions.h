#ifndef SOLVER_FUNCTIONS_H
#define SOLVER_FUNCTIONS_H

#include <string>
#include <utility> // For std::pair

// Structure for detailed timings from aig_to_bdd_solver
struct SolverDetailedTimings {
    long long aig_parsing_ms = 0;          // Time to parse AIG file content (header and structure)
    long long bdd_construction_ms = 0;     // Time for CUDD var setup (including ordering), AND gates, final BDD
    long long sampling_ms = 0;             // Time for path counting and DFS sampling
};

// Forward declaration for aig_to_bdd_solver to be used in main.cpp
// The actual definition is in aig_bdd_solver.cpp
// Note: If main.cpp includes this header, its local forward declaration should match or be removed.
std::pair<int, SolverDetailedTimings> aig_to_bdd_solver(
    const std::string &aig_file_path, 
    const std::string &original_json_path, 
    int num_samples, 
    const std::string &result_json_path, 
    unsigned int random_seed);

// Declaration for to_hex_string, implementation can be in a .cpp file
// (e.g., solver_functions.cpp or kept in aig_bdd_solver.cpp if linked appropriately)
std::string to_hex_string(unsigned long long value, int bit_width);

int json_v_converter(const std::string &input_json_path,
                     const std::string &output_v_dir);

int merge_bdd_results(const std::string &manifest_path_str, 
                      const std::string &final_output_json_path_str, 
                      int total_samples_required,
                      unsigned int random_seed);

#endif // SOLVER_FUNCTIONS_H