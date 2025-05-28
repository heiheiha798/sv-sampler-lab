#include "solver_functions.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <quadmath.h>
#include <cmath>

namespace std {
inline __float128 nextafter(__float128 __x, __float128 __y) {
    return ::nextafterq(__x, __y);
}
} // namespace std

#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "cudd.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

string to_hex_string(unsigned long long value, int bit_width);

static std::vector<int> determine_bdd_variable_order(
    int nI_total, const std::vector<int> &aig_primary_input_literals,
    const std::vector<int> &circuit_output_literals_from_aig,
    const std::map<int, std::pair<int, int>> &and_gate_definitions);

struct PathCounts {
    __float128 even_cnt;
    __float128 odd_cnt;
};

static std::map<DdNode *, PathCounts> path_counts_memo;

static std::mt19937 rng;

PathCounts compute_path_counts_recursive(DdNode *node_regular,
                                         DdManager *manager) {
    if (node_regular == Cudd_ReadOne(manager))
        return {1.0Q, 0.0Q};
    if (node_regular == Cudd_ReadLogicZero(manager))
        return {0.0Q, 0.0Q};
    auto it = path_counts_memo.find(node_regular);
    if (it != path_counts_memo.end())
        return it->second;
    DdNode *E_child = Cudd_E(node_regular);
    DdNode *T_child = Cudd_T(node_regular);
    DdNode *E_child_regular = Cudd_Regular(E_child);
    DdNode *T_child_regular = Cudd_Regular(T_child);
    PathCounts counts_E =
        compute_path_counts_recursive(E_child_regular, manager);
    PathCounts counts_T =
        compute_path_counts_recursive(T_child_regular, manager);
    __float128 current_even_cnt = 0.0Q;
    __float128 current_odd_cnt = 0.0Q;
    if (Cudd_IsComplement(E_child)) {
        current_even_cnt += counts_E.odd_cnt;
        current_odd_cnt += counts_E.even_cnt;
    } else {
        current_even_cnt += counts_E.even_cnt;
        current_odd_cnt += counts_E.odd_cnt;
    }
    if (Cudd_IsComplement(T_child)) {
        current_even_cnt += counts_T.odd_cnt;
        current_odd_cnt += counts_T.even_cnt;
    } else {
        current_even_cnt += counts_T.even_cnt;
        current_odd_cnt += counts_T.odd_cnt;
    }
    PathCounts result = {current_even_cnt, current_odd_cnt};
    path_counts_memo[node_regular] = result;
    return result;
}

bool generate_random_solution_dfs(
    DdNode *current_node_regular, bool accumulated_odd_complements_so_far,
    DdManager *manager, std::map<int, int> &assignment_map,
    std::uniform_real_distribution<__float128> &dist) {
    if (current_node_regular == Cudd_ReadOne(manager))
        return true;
    if (current_node_regular == Cudd_ReadLogicZero(manager))
        return false;
    DdNode *E_child_ptr = Cudd_E(current_node_regular);
    DdNode *T_child_ptr = Cudd_T(current_node_regular);
    DdNode *E_child_regular = Cudd_Regular(E_child_ptr);
    DdNode *T_child_regular = Cudd_Regular(T_child_ptr);
    bool is_complement_E = Cudd_IsComplement(E_child_ptr);
    bool is_complement_T = Cudd_IsComplement(T_child_ptr);
    PathCounts counts_from_E_child =
        compute_path_counts_recursive(E_child_regular, manager);
    PathCounts counts_from_T_child =
        compute_path_counts_recursive(T_child_regular, manager);
    bool required_odd_parity_for_E_path =
        accumulated_odd_complements_so_far ^ is_complement_E;
    __float128 cnt_paths_via_E;
    if (required_odd_parity_for_E_path)
        cnt_paths_via_E = counts_from_E_child.odd_cnt;
    else
        cnt_paths_via_E = counts_from_E_child.even_cnt;
    bool required_odd_parity_for_T_path =
        accumulated_odd_complements_so_far ^ is_complement_T;
    __float128 cnt_paths_via_T;
    if (required_odd_parity_for_T_path)
        cnt_paths_via_T = counts_from_T_child.odd_cnt;
    else
        cnt_paths_via_T = counts_from_T_child.even_cnt;
    __float128 total_valid_continuing_paths = cnt_paths_via_E + cnt_paths_via_T;
    int var_index = Cudd_NodeReadIndex(current_node_regular);
    if (total_valid_continuing_paths <= 0.0Q)
        return false;
    __float128 prob_take_E_branch;
    if (total_valid_continuing_paths > 0.0Q)
        prob_take_E_branch = cnt_paths_via_E / total_valid_continuing_paths;
    else
        prob_take_E_branch = 0.5Q;
    __float128 random_draw_val = dist(rng);
    if (random_draw_val < prob_take_E_branch) {
        assignment_map[var_index] = 0;
        return generate_random_solution_dfs(E_child_regular,
                                            required_odd_parity_for_E_path,
                                            manager, assignment_map, dist);
    } else {
        assignment_map[var_index] = 1;
        return generate_random_solution_dfs(T_child_regular,
                                            required_odd_parity_for_T_path,
                                            manager, assignment_map, dist);
    }
}

struct AigData {
    int nM, nI, nL, nO, nA;
    std::vector<int> primary_input_literals;
    std::map<int, int> literal_to_original_pi_index;
    std::vector<int> circuit_output_literals;
    std::map<int, std::pair<int, int>> and_gate_definitions;
    std::vector<std::tuple<int, int, int>> and_gate_lines_for_processing;
};

static DdManager *initialize_cudd_manager() {
    auto cudd_init_start_time = std::chrono::high_resolution_clock::now();
    DdManager *manager =
        Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (!manager)
        return nullptr;
    Cudd_AutodynEnable(manager, CUDD_REORDER_SIFT);
    auto cudd_init_end_time = std::chrono::high_resolution_clock::now();
    auto cudd_init_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            cudd_init_end_time - cudd_init_start_time);
    return manager;
}

static bool parse_aig_header(std::ifstream &aig_file_stream, AigData &data) {
    auto aig_header_parse_start_time =
        std::chrono::high_resolution_clock::now();
    std::string line;
    if (!getline(aig_file_stream, line) || line.substr(0, 3) != "aag")
        return false;
    stringstream header_ss(line.substr(4));
    if (!(header_ss >> data.nM >> data.nI >> data.nL >> data.nO >> data.nA))
        return false;
    if (data.nO == 0)
        return false;
    auto aig_header_parse_end_time = std::chrono::high_resolution_clock::now();
    auto aig_header_parse_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            aig_header_parse_end_time - aig_header_parse_start_time);
    return true;
}

static bool read_aig_structure(std::ifstream &aig_file_stream, AigData &data) {
    auto aig_preread_start_time = std::chrono::high_resolution_clock::now();
    data.primary_input_literals.resize(data.nI);
    std::string line;
    for (int i = 0; i < data.nI; ++i) {
        if (!getline(aig_file_stream, line))
            return false;
        int input_lit;
        if (sscanf(line.c_str(), "%d", &input_lit) != 1)
            return false;
        data.primary_input_literals[i] = input_lit;
        data.literal_to_original_pi_index[input_lit] = i;
    }
    for (int i = 0; i < data.nL; ++i)
        if (!getline(aig_file_stream, line))
            return false;
    data.circuit_output_literals.resize(data.nO);
    for (int i = 0; i < data.nO; ++i) {
        if (!getline(aig_file_stream, line))
            return false;
        int output_lit;
        if (sscanf(line.c_str(), "%d", &output_lit) != 1)
            return false;
        data.circuit_output_literals[i] = output_lit;
    }
    for (int i = 0; i < data.nA; ++i) {
        if (!getline(aig_file_stream, line))
            return false;
        stringstream ss(line);
        int output_lit, input1_lit, input2_lit;
        if (!(ss >> output_lit >> input1_lit >> input2_lit))
            return false;
        data.and_gate_definitions[output_lit] = {input1_lit, input2_lit};
        data.and_gate_lines_for_processing.emplace_back(output_lit, input1_lit,
                                                        input2_lit);
    }
    auto aig_preread_end_time = std::chrono::high_resolution_clock::now();
    auto aig_preread_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            aig_preread_end_time - aig_preread_start_time);
    return true;
}

static bool
create_bdd_variables(DdManager *manager, const AigData &data,
                     std::map<int, DdNode *> &literal_to_bdd_map,
                     std::vector<DdNode *> &input_vars_bdd,
                     std::map<int, int> &cudd_idx_to_original_aig_pi_file_idx) {
    auto var_order_start_time = std::chrono::high_resolution_clock::now();
    input_vars_bdd.resize(data.nI);
    std::vector<int> ordered_aig_pi_literals_for_bdd_creation =
        determine_bdd_variable_order(data.nI, data.primary_input_literals,
                                     data.circuit_output_literals,
                                     data.and_gate_definitions);
    auto var_order_end_time = std::chrono::high_resolution_clock::now();
    auto var_order_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            var_order_end_time - var_order_start_time);
    auto bdd_var_creation_start_time =
        std::chrono::high_resolution_clock::now();
    for (int i = 0; i < data.nI; ++i) {
        int current_aig_pi_literal_to_create;
        if (i < ordered_aig_pi_literals_for_bdd_creation.size())
            current_aig_pi_literal_to_create =
                ordered_aig_pi_literals_for_bdd_creation[i];
        else {
            bool found_uncreated = false;
            for (int orig_pi_lit : data.primary_input_literals)
                if (literal_to_bdd_map.find(orig_pi_lit) ==
                    literal_to_bdd_map.end()) {
                    current_aig_pi_literal_to_create = orig_pi_lit;
                    found_uncreated = true;
                    break;
                }
            if (!found_uncreated)
                return false;
        }
        DdNode *var_node = Cudd_bddNewVar(manager);
        if (!var_node)
            return false;
        literal_to_bdd_map[current_aig_pi_literal_to_create] = var_node;
        literal_to_bdd_map[current_aig_pi_literal_to_create + 1] =
            Cudd_Not(var_node);
        Cudd_Ref(var_node);
        Cudd_Ref(Cudd_Not(var_node));
        int original_pi_idx = data.literal_to_original_pi_index.at(
            current_aig_pi_literal_to_create);
        input_vars_bdd[original_pi_idx] = var_node;
        int cudd_var_idx = Cudd_NodeReadIndex(var_node);
        if (Cudd_bddIsVar(manager, var_node))
            cudd_idx_to_original_aig_pi_file_idx[cudd_var_idx] =
                original_pi_idx;
    }
    auto bdd_var_creation_end_time = std::chrono::high_resolution_clock::now();
    auto bdd_var_creation_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            bdd_var_creation_end_time - bdd_var_creation_start_time);
    return true;
}

static bool
build_bdd_for_and_gates(DdManager *manager, const AigData &data,
                        std::map<int, DdNode *> &literal_to_bdd_map) {
    auto and_gate_processing_start_time =
        std::chrono::high_resolution_clock::now();
    for (const auto &gate_def : data.and_gate_lines_for_processing) {
        int output_lit = std::get<0>(gate_def);
        int input1_lit = std::get<1>(gate_def);
        int input2_lit = std::get<2>(gate_def);
        auto it1 = literal_to_bdd_map.find(input1_lit);
        auto it2 = literal_to_bdd_map.find(input2_lit);
        if (it1 == literal_to_bdd_map.end() || it2 == literal_to_bdd_map.end())
            return false;
        DdNode *input1_bdd = it1->second;
        DdNode *input2_bdd = it2->second;
        DdNode *and_node = Cudd_bddAnd(manager, input1_bdd, input2_bdd);
        Cudd_Ref(and_node);
        literal_to_bdd_map[output_lit] = and_node;
        literal_to_bdd_map[output_lit + 1] = Cudd_Not(and_node);
        Cudd_Ref(Cudd_Not(and_node));
    }
    auto and_gate_processing_end_time =
        std::chrono::high_resolution_clock::now();
    auto and_gate_processing_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            and_gate_processing_end_time - and_gate_processing_start_time);
    return true;
}

static DdNode *
get_final_bdd_output(DdManager *manager, const AigData &data,
                     const std::map<int, DdNode *> &literal_to_bdd_map) {
    auto get_output_bdd_start_time = std::chrono::high_resolution_clock::now();
    DdNode *bdd_circuit_output = nullptr;
    if (data.nO > 0) {
        int first_output_lit = data.circuit_output_literals[0];
        auto it = literal_to_bdd_map.find(first_output_lit);
        if (it == literal_to_bdd_map.end())
            return nullptr;
        bdd_circuit_output = it->second;
        Cudd_Ref(bdd_circuit_output);
    } else
        return nullptr;
    auto get_output_bdd_end_time = std::chrono::high_resolution_clock::now();
    auto get_output_bdd_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            get_output_bdd_end_time - get_output_bdd_start_time);
    return bdd_circuit_output;
}

static json perform_bdd_sampling(
    DdManager *manager, DdNode *bdd_circuit_output, int num_samples, int nI,
    const json &original_variable_list,
    const std::vector<DdNode *> &input_vars_bdd,
    const std::map<int, int> &cudd_idx_to_original_aig_pi_file_idx
    [[maybe_unused]]) {
    auto sampling_logic_start_time = std::chrono::high_resolution_clock::now();
    json assignment_list = json::array();
    if (!bdd_circuit_output)
        return assignment_list;
    path_counts_memo.clear();
    DdNode *regular_root_bdd = Cudd_Regular(bdd_circuit_output);
    auto dp_start_time = std::chrono::high_resolution_clock::now();
    PathCounts root_counts =
        compute_path_counts_recursive(regular_root_bdd, manager);
    auto dp_end_time = std::chrono::high_resolution_clock::now();
    auto dp_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        dp_end_time - dp_start_time);
    __float128 total_target_paths;
    bool initial_accumulated_odd_complements =
        Cudd_IsComplement(bdd_circuit_output);
    if (initial_accumulated_odd_complements)
        total_target_paths = root_counts.odd_cnt;
    else
        total_target_paths = root_counts.even_cnt;
    char buf[128];
    quadmath_snprintf(buf, sizeof(buf), "%.0Qf", total_target_paths);
    if (total_target_paths > 0.0Q) {
        std::uniform_real_distribution<__float128> dist_float128(0.0Q, 1.0Q);
        int samples_successfully_generated = 0;
        int total_dfs_attempts = 0;
        const int MAX_TOTAL_DFS_ATTEMPTS = num_samples * 200;
        std::set<std::string> unique_assignment_signatures;
        while (samples_successfully_generated < num_samples &&
               total_dfs_attempts < MAX_TOTAL_DFS_ATTEMPTS) {
            total_dfs_attempts++;
            std::map<int, int> current_assignment_cudd_indices;
            bool current_sample_generation_successful = false;
            if (nI == 0) {
                if (bdd_circuit_output == Cudd_ReadOne(manager) &&
                    !initial_accumulated_odd_complements)
                    current_sample_generation_successful = true;
                else
                    break;
            } else if (regular_root_bdd == Cudd_ReadOne(manager)) {
                if (!initial_accumulated_odd_complements)
                    current_sample_generation_successful = true;
                else
                    break;
            } else if (regular_root_bdd == Cudd_ReadLogicZero(manager))
                break;
            else {
                current_assignment_cudd_indices.clear();
                current_sample_generation_successful =
                    generate_random_solution_dfs(
                        regular_root_bdd, initial_accumulated_odd_complements,
                        manager, current_assignment_cudd_indices,
                        dist_float128);
            }
            if (current_sample_generation_successful) {
                json assignment_entry = json::array();
                int current_bit_idx_overall = 0;
                for (const auto &var_info : original_variable_list) {
                    int bit_width = var_info.value("bit_width", 1);
                    unsigned long long variable_combined_value = 0;
                    for (int bit_k = 0; bit_k < bit_width; ++bit_k) {
                        if (current_bit_idx_overall >= nI) {
                            auto sampling_logic_end_time_fatal =
                                std::chrono::high_resolution_clock::now();
                            auto sampling_logic_duration_fatal =
                                std::chrono::duration_cast<
                                    std::chrono::milliseconds>(
                                    sampling_logic_end_time_fatal -
                                    sampling_logic_start_time);
                            throw std::runtime_error(
                                "JSON variable structure inconsistent with AIG "
                                "nI during sampling.");
                        }
                        unsigned long long bit_assignment = 0;
                        if (nI > 0 &&
                            current_bit_idx_overall < input_vars_bdd.size()) {
                            DdNode *pi_bdd_node =
                                input_vars_bdd[current_bit_idx_overall];
                            if (pi_bdd_node &&
                                Cudd_bddIsVar(manager, pi_bdd_node)) {
                                int cudd_var_idx =
                                    Cudd_NodeReadIndex(pi_bdd_node);
                                auto it_assign =
                                    current_assignment_cudd_indices.find(
                                        cudd_var_idx);
                                if (it_assign !=
                                    current_assignment_cudd_indices.end())
                                    bit_assignment = it_assign->second;
                                else
                                {
                                    std::uniform_int_distribution<int> dist_bit(0, 1);
                                    bit_assignment = dist_bit(rng);
                                }
                            } else
                                bit_assignment = 0;
                        }
                        variable_combined_value |= (bit_assignment << bit_k);
                        current_bit_idx_overall++;
                    }
                    assignment_entry.push_back(
                        {{"value",
                          to_hex_string(variable_combined_value, bit_width)}});
                }
                std::string assignment_signature = assignment_entry.dump();
                if (unique_assignment_signatures.insert(assignment_signature)
                        .second) {
                    assignment_list.push_back(assignment_entry);
                    samples_successfully_generated++;
                }
            }
        }
    }
    auto sampling_logic_end_time = std::chrono::high_resolution_clock::now();
    auto sampling_logic_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            sampling_logic_end_time - sampling_logic_start_time);
    return assignment_list;
}

static bool format_and_write_results(const std::string &result_json_path,
                                     const json &assignment_list) {
    auto json_write_start_time = std::chrono::high_resolution_clock::now();
    json result_json;
    result_json["assignment_list"] = assignment_list;
    std::ofstream output_json_stream(result_json_path);
    if (!output_json_stream.is_open())
        return false;
    output_json_stream << result_json.dump(4) << std::endl;
    output_json_stream.close();
    auto json_write_end_time = std::chrono::high_resolution_clock::now();
    auto json_write_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            json_write_end_time - json_write_start_time);
    return true;
}

static void
cleanup_cudd_resources(DdManager *manager, DdNode *bdd_circuit_output,
                       std::map<int, DdNode *> &literal_to_bdd_map) {
    auto cudd_cleanup_start_time = std::chrono::high_resolution_clock::now();
    if (bdd_circuit_output)
        Cudd_RecursiveDeref(manager, bdd_circuit_output);
    for (auto const &[key, val_node] : literal_to_bdd_map)
        if (val_node)
            Cudd_RecursiveDeref(manager, val_node);
    literal_to_bdd_map.clear();
    path_counts_memo.clear();
    if (manager)
        Cudd_Quit(manager);
    auto cudd_cleanup_end_time = std::chrono::high_resolution_clock::now();
    auto cudd_cleanup_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            cudd_cleanup_end_time - cudd_cleanup_start_time);
}

int aig_to_bdd_solver(const string &aig_file_path,
                      const string &original_json_path, int num_samples,
                      const string &result_json_path,
                      unsigned int random_seed) {
    auto function_start_time = std::chrono::high_resolution_clock::now();
    rng.seed(random_seed);
    DdManager *manager = nullptr;
    DdNode *bdd_circuit_output = nullptr;
    AigData aig_data;
    std::map<int, DdNode *> literal_to_bdd_map;
    std::vector<DdNode *> input_vars_bdd;
    std::map<int, int> cudd_idx_to_original_aig_pi_file_idx;
    manager = initialize_cudd_manager();
    if (!manager)
        return 1;

    literal_to_bdd_map[0] = Cudd_ReadLogicZero(manager);
    literal_to_bdd_map[1] = Cudd_ReadOne(manager);

    std::ifstream aig_file_stream(aig_file_path);
    if (!aig_file_stream.is_open()) {
        cleanup_cudd_resources(manager, nullptr, literal_to_bdd_map);
        return 1;
    }
    if (!parse_aig_header(aig_file_stream, aig_data)) {
        aig_file_stream.close();
        cleanup_cudd_resources(manager, nullptr, literal_to_bdd_map);
        return 1;
    }
    if (!read_aig_structure(aig_file_stream, aig_data)) {
        aig_file_stream.close();
        cleanup_cudd_resources(manager, nullptr, literal_to_bdd_map);
        return 1;
    }
    aig_file_stream.close();
    if (!create_bdd_variables(manager, aig_data, literal_to_bdd_map,
                              input_vars_bdd,
                              cudd_idx_to_original_aig_pi_file_idx)) {
        cleanup_cudd_resources(manager, nullptr, literal_to_bdd_map);
        return 1;
    }
    if (!build_bdd_for_and_gates(manager, aig_data, literal_to_bdd_map)) {
        cleanup_cudd_resources(manager, nullptr, literal_to_bdd_map);
        return 1;
    }
    bdd_circuit_output =
        get_final_bdd_output(manager, aig_data, literal_to_bdd_map);
    if (!bdd_circuit_output) {
        cleanup_cudd_resources(manager, bdd_circuit_output, literal_to_bdd_map);
        return 1;
    }
    auto original_json_read_start_time =
        std::chrono::high_resolution_clock::now();
    ifstream original_json_stream(original_json_path);
    json original_data;
    if (original_json_stream.is_open()) {
        original_json_stream >> original_data;
        original_json_stream.close();
    }
    json original_variable_list = original_data.contains("variable_list")
                                      ? original_data["variable_list"]
                                      : json::array();
    auto original_json_read_end_time =
        std::chrono::high_resolution_clock::now();
    auto original_json_read_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            original_json_read_end_time - original_json_read_start_time);
    json assignment_list;
    try {
        assignment_list = perform_bdd_sampling(
            manager, bdd_circuit_output, num_samples, aig_data.nI,
            original_variable_list, input_vars_bdd,
            cudd_idx_to_original_aig_pi_file_idx);
    } catch (const std::runtime_error &e) {
        cleanup_cudd_resources(manager, bdd_circuit_output, literal_to_bdd_map);
        return 1;
    }
    if (!format_and_write_results(result_json_path, assignment_list)) {
        cleanup_cudd_resources(manager, bdd_circuit_output, literal_to_bdd_map);
        return 1;
    }
    cleanup_cudd_resources(manager, bdd_circuit_output, literal_to_bdd_map);
    manager = nullptr;
    bdd_circuit_output = nullptr;
    auto function_end_time = std::chrono::high_resolution_clock::now();
    auto function_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            function_end_time - function_start_time);
    return 0;
}

string to_hex_string(unsigned long long value, int bit_width) {
    if (bit_width <= 0)
        return "";
    int hex_chars = (bit_width + 3) / 4;
    unsigned long long mask = (1ULL << (bit_width < 64 ? bit_width : 63)) - 1;
    if (bit_width == 64)
        mask = 0xFFFFFFFFFFFFFFFFULL;
    else if (bit_width > 0 && bit_width < 64)
        mask = (1ULL << bit_width) - 1;
    else if (bit_width == 0)
        mask = 0;
    value &= mask;
    stringstream ss;
    ss << hex << setfill('0') << setw(hex_chars) << value;
    string hex_str = ss.str();
    if (hex_str.length() > hex_chars && hex_chars > 0)
        hex_str = hex_str.substr(hex_str.length() - hex_chars);
    return hex_str;
}

static void get_pi_support_for_node_dfs_recursive(
    int current_literal,
    const std::map<int, std::pair<int, int>> &and_gate_definitions,
    const std::set<int> &all_pi_literals_set, std::set<int> &node_pi_support,
    std::set<int> &visited_for_this_support_dfs,
    std::map<int, std::set<int>> &memoized_supports);

static std::set<int>
get_pi_support(int literal,
               const std::map<int, std::pair<int, int>> &and_gate_definitions,
               const std::set<int> &all_pi_literals_set,
               std::map<int, std::set<int>> &memoized_supports) {
    int regular_lit = (literal / 2) * 2;
    if (regular_lit == 0 || regular_lit == 1)
        return {};
    auto memo_it = memoized_supports.find(regular_lit);
    if (memo_it != memoized_supports.end())
        return memo_it->second;
    std::set<int> support;
    std::set<int> visited_dfs_for_this_call;
    get_pi_support_for_node_dfs_recursive(
        regular_lit, and_gate_definitions, all_pi_literals_set, support,
        visited_dfs_for_this_call, memoized_supports);
    memoized_supports[regular_lit] = support;
    return support;
}

static void get_pi_support_for_node_dfs_recursive(
    int current_literal,
    const std::map<int, std::pair<int, int>> &and_gate_definitions,
    const std::set<int> &all_pi_literals_set, std::set<int> &node_pi_support,
    std::set<int> &visited_for_this_support_dfs,
    std::map<int, std::set<int>> &memoized_supports) {
    int regular_literal = (current_literal / 2) * 2;
    if (regular_literal == 0 || regular_literal == 1)
        return;
    if (visited_for_this_support_dfs.count(regular_literal))
        return;
    visited_for_this_support_dfs.insert(regular_literal);
    if (all_pi_literals_set.count(regular_literal)) {
        node_pi_support.insert(regular_literal);
        if (memoized_supports.find(regular_literal) == memoized_supports.end())
            memoized_supports[regular_literal] = {regular_literal};
        return;
    }
    auto memo_it = memoized_supports.find(regular_literal);
    if (memo_it != memoized_supports.end()) {
        const auto &cached_support = memo_it->second;
        node_pi_support.insert(cached_support.begin(), cached_support.end());
        return;
    }
    auto it_and_def = and_gate_definitions.find(regular_literal);
    if (it_and_def != and_gate_definitions.end()) {
        const auto &inputs = it_and_def->second;
        std::set<int> support1 =
            get_pi_support(inputs.first, and_gate_definitions,
                           all_pi_literals_set, memoized_supports);
        std::set<int> support2 =
            get_pi_support(inputs.second, and_gate_definitions,
                           all_pi_literals_set, memoized_supports);
        node_pi_support.insert(support1.begin(), support1.end());
        node_pi_support.insert(support2.begin(), support2.end());
    }
}

static std::vector<int> determine_bdd_variable_order(
    int nI_total_from_header,
    const std::vector<int> &aig_primary_input_literals,
    const std::vector<int> &circuit_output_literals_from_aig,
    const std::map<int, std::pair<int, int>> &and_gate_definitions) {
    std::vector<int> ordered_pis_for_bdd_creation;
    std::set<int> added_pis_to_final_order_set;
    std::set<int> all_pi_literals_set;
    for (int pi_lit : aig_primary_input_literals)
        all_pi_literals_set.insert((pi_lit / 2) * 2);
    std::map<int, std::set<int>> memoized_pi_supports;
    std::set<int> visited_nodes_for_ordering_dfs;
    std::function<void(int)> order_dfs_main = [&](int current_node_literal) {
        int regular_node_lit = (current_node_literal / 2) * 2;
        if (regular_node_lit == 0 || regular_node_lit == 1)
            return;
        if (visited_nodes_for_ordering_dfs.count(regular_node_lit))
            return;
        visited_nodes_for_ordering_dfs.insert(regular_node_lit);
        if (all_pi_literals_set.count(regular_node_lit)) {
            if (added_pis_to_final_order_set.find(regular_node_lit) ==
                added_pis_to_final_order_set.end()) {
                ordered_pis_for_bdd_creation.push_back(regular_node_lit);
                added_pis_to_final_order_set.insert(regular_node_lit);
            }
            return;
        }
        auto it_and = and_gate_definitions.find(regular_node_lit);
        if (it_and != and_gate_definitions.end()) {
            const auto &inputs = it_and->second;
            std::set<int> support1 =
                get_pi_support(inputs.first, and_gate_definitions,
                               all_pi_literals_set, memoized_pi_supports);
            std::set<int> support2 =
                get_pi_support(inputs.second, and_gate_definitions,
                               all_pi_literals_set, memoized_pi_supports);
            if (support1.size() <= support2.size()) {
                order_dfs_main(inputs.first);
                order_dfs_main(inputs.second);
            } else {
                order_dfs_main(inputs.second);
                order_dfs_main(inputs.first);
            }
        }
    };
    if (!circuit_output_literals_from_aig.empty())
        for (int po_lit : circuit_output_literals_from_aig) {
            if (po_lit == 0 || po_lit == 1)
                continue;
            order_dfs_main(po_lit);
        }
    for (int pi_lit_from_file : aig_primary_input_literals) {
        int regular_pi_lit = (pi_lit_from_file / 2) * 2;
        if (all_pi_literals_set.count(regular_pi_lit))
            if (added_pis_to_final_order_set.find(regular_pi_lit) ==
                added_pis_to_final_order_set.end()) {
                ordered_pis_for_bdd_creation.push_back(regular_pi_lit);
                added_pis_to_final_order_set.insert(regular_pi_lit);
            }
    }
    return ordered_pis_for_bdd_creation;
}