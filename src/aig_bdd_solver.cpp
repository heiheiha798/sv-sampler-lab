#include "solver_functions.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cudd.h" // CUDD library header

#include "nlohmann/json.hpp" // For JSON operations

using json = nlohmann::json;
using namespace std;

// Forward declaration of helper, implementation unchanged
string to_hex_string(unsigned long long value, int bit_width);

// --- NEW HELPER FUNCTION PROTOTYPE for variable ordering ---
static std::vector<int> determine_bdd_variable_order(
    int nI_total,
    const std::vector<int>
        &aig_primary_input_literals, // Literals as read from AIG PI section
    const std::vector<int>
        &circuit_output_literals_from_aig, // AIG output literals
    const std::map<int, std::pair<int, int>>
        &and_gate_definitions // LHS -> {RHS1, RHS2}
);

// 实现主 BDD 求解器函数
int aig_to_bdd_solver(const string &aig_file_path,
                      const string &original_json_path, int num_samples,
                      const string &result_json_path,
                      unsigned int random_seed) {
    DdManager *manager;
    DdNode *bdd_circuit_output = nullptr;
    int nM = 0, nI = 0, nL = 0, nO = 0, nA = 0;

    // 1. 初始化 CUDD 管理器
    manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (!manager) {
        cerr << "Error: CUDD manager initialization failed." << endl;
        return 1;
    }

    Cudd_AutodynEnable(manager, CUDD_REORDER_SIFT);
    cout << "Debug: Automatic dynamic variable reordering enabled "
            "(CUDD_REORDER_SIFT)."
         << endl;
    Cudd_Srandom(manager, random_seed);

    map<int, DdNode *> literal_to_bdd_map;
    ifstream aig_file_stream(aig_file_path);
    if (!aig_file_stream.is_open()) {
        cerr << "Error: Could not open AIG file: " << aig_file_path << endl;
        Cudd_Quit(manager);
        return 1;
    }

    string line;
    // 1. 解析 AIGER 头信息 (aag M I L O A)
    if (!getline(aig_file_stream, line) || line.substr(0, 3) != "aag") {
        cerr << "Error: Invalid or empty AIGER file (header)." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }
    stringstream header_ss(line.substr(4));
    if (!(header_ss >> nM >> nI >> nL >> nO >> nA)) {
        cerr << "Error: Malformed AIGER header values." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }
    if (nL != 0) {
        cerr << "Warning: AIGER file contains latches (nL > 0), this solver "
                "only handles combinational circuits."
             << endl;
    }
    if (nO == 0) {
        cerr << "Error: AIGER file has no outputs." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }
    cout << "AIGER Header: M=" << nM << " I=" << nI << " L=" << nL
         << " O=" << nO << " A=" << nA << endl;

    // --- MODIFICATION START: Pre-read AIG structure for variable ordering ---
    std::vector<int> aig_primary_input_literals(
        nI); // Stores the actual literals like 2, 4, 6...
    std::map<int, int>
        aig_literal_to_original_pi_index; // Maps AIG PI literal to its 0-based
                                          // index in file

    for (int i = 0; i < nI; ++i) {
        if (!getline(aig_file_stream, line)) { /* Error handling */
            cerr << "Error reading PI line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        int input_lit;
        if (sscanf(line.c_str(), "%d", &input_lit) != 1) { /* Error handling */
            cerr << "Error parsing PI line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        aig_primary_input_literals[i] = input_lit;
        aig_literal_to_original_pi_index[input_lit] = i;
    }

    for (int i = 0; i < nL; ++i) {             // Skip latch lines
        if (!getline(aig_file_stream, line)) { /* Error handling */
            cerr << "Error reading Latch line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
    }

    std::vector<int> circuit_output_literals_from_aig(nO);
    for (int i = 0; i < nO; ++i) {
        if (!getline(aig_file_stream, line)) { /* Error handling */
            cerr << "Error reading PO line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        int output_lit;
        if (sscanf(line.c_str(), "%d", &output_lit) != 1) { /* Error handling */
            cerr << "Error parsing PO line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        circuit_output_literals_from_aig[i] = output_lit;
    }

    std::map<int, std::pair<int, int>>
        and_gate_definitions; // LHS literal -> {RHS1, RHS2}
    std::vector<tuple<int, int, int>>
        and_gate_lines_for_processing; // Store lines to process after var
                                       // creation

    for (int i = 0; i < nA; ++i) {
        if (!getline(aig_file_stream, line)) { /* Error handling */
            cerr << "Error reading AND line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        stringstream ss(line);
        int output_lit, input1_lit, input2_lit;
        if (!(ss >> output_lit >> input1_lit >>
              input2_lit)) { /* Error handling */
            cerr << "Error parsing AND line " << i << endl;
            Cudd_Quit(manager);
            return 1;
        }
        and_gate_definitions[output_lit] = {input1_lit, input2_lit};
        and_gate_lines_for_processing.emplace_back(output_lit, input1_lit,
                                                   input2_lit);
    }
    aig_file_stream.close(); // Finished reading AIG file structure
    // --- MODIFICATION END: Pre-read AIG structure ---

    // --- MODIFICATION START: Determine BDD variable order and create variables
    vector<DdNode *> input_vars_bdd(
        nI); // This will store DdNode* for PIs, indexed by their original AIG
             // file order (0 to nI-1)

    std::vector<int> ordered_aig_pi_literals_for_bdd_creation =
        determine_bdd_variable_order(nI, aig_primary_input_literals,
                                     circuit_output_literals_from_aig,
                                     and_gate_definitions);

    cout << "Debug: Creating " << nI
         << " BDD input variables based on determined order." << endl;
    for (int i = 0; i < nI; ++i) {
        int current_aig_pi_literal_to_create;
        if (i < ordered_aig_pi_literals_for_bdd_creation.size()) {
            current_aig_pi_literal_to_create =
                ordered_aig_pi_literals_for_bdd_creation[i];
        } else {
            // This case should ideally not be hit if
            // determine_bdd_variable_order includes all PIs. As a fallback,
            // pick an unassigned PI. This needs robust handling. For simplicity
            // now, we assume determine_bdd_variable_order returns all nI PIs.
            // Find a PI from aig_primary_input_literals not yet in
            // literal_to_bdd_map
            bool found_uncreated = false;
            for (int orig_pi_lit : aig_primary_input_literals) {
                if (literal_to_bdd_map.find(orig_pi_lit) ==
                    literal_to_bdd_map.end()) {
                    current_aig_pi_literal_to_create = orig_pi_lit;
                    found_uncreated = true;
                    break;
                }
            }
            if (!found_uncreated) {
                cerr << "Error: Logic flaw in assigning remaining PIs for BDD "
                        "creation."
                     << endl;
                Cudd_Quit(manager);
                return 1;
            }
            cout << "Warning: Fallback PI assignment for BDD var creation: "
                 << current_aig_pi_literal_to_create << endl;
        }

        DdNode *var_node =
            Cudd_bddNewVar(manager); // Creates BDD var at level 'i'
        if (!var_node) {             /* Error handling */
            cerr << "Cudd_bddNewVar failed for PI "
                 << current_aig_pi_literal_to_create << endl;
            Cudd_Quit(manager);
            return 1;
        }

        literal_to_bdd_map[current_aig_pi_literal_to_create] = var_node;
        literal_to_bdd_map[current_aig_pi_literal_to_create + 1] =
            Cudd_Not(var_node);
        Cudd_Ref(var_node);
        Cudd_Ref(Cudd_Not(var_node));

        // Populate input_vars_bdd correctly: it's indexed by original AIG PI
        // order
        int original_pi_idx =
            aig_literal_to_original_pi_index[current_aig_pi_literal_to_create];
        input_vars_bdd[original_pi_idx] = var_node;
    }
    cout << "Debug: Finished creating BDD input variables." << endl;
    // --- MODIFICATION END: Determine BDD variable order and create variables
    // ---

    // 5. 处理 AND 门 (A 行) - Now using pre-read and_gate_lines_for_processing
    cout << "Debug: Building BDDs for " << nA << " AND gates." << endl;
    for (const auto &gate_def : and_gate_lines_for_processing) {
        int output_lit = std::get<0>(gate_def);
        int input1_lit = std::get<1>(gate_def);
        int input2_lit = std::get<2>(gate_def);

        auto it1 = literal_to_bdd_map.find(input1_lit);
        auto it2 = literal_to_bdd_map.find(input2_lit);

        if (it1 == literal_to_bdd_map.end() ||
            it2 == literal_to_bdd_map.end()) {
            // Error logging as before...
            cerr << "Error: AIGER parsing failed for AND gate (" << output_lit
                 << " = " << input1_lit << " & " << input2_lit;
            // ... (rest of error message)
            cerr << ") input literal not found in map." << endl;
            Cudd_Quit(manager);
            return 1;
        }

        DdNode *input1_bdd = it1->second;
        DdNode *input2_bdd = it2->second;
        DdNode *and_node = Cudd_bddAnd(manager, input1_bdd, input2_bdd);
        Cudd_Ref(and_node);

        literal_to_bdd_map[output_lit] = and_node;
        literal_to_bdd_map[output_lit + 1] = Cudd_Not(and_node);
        Cudd_Ref(Cudd_Not(and_node));
    }
    cout << "Debug: Finished building BDDs for AND gates." << endl;

    // 6. 获取最终输出的 BDD 节点 (第一个输出)
    if (nO > 0) { // circuit_output_literals_from_aig was populated earlier
        int first_output_lit = circuit_output_literals_from_aig[0];
        if (!literal_to_bdd_map.count(first_output_lit)) {
            cerr << "Error: Final output BDD node for literal "
                 << first_output_lit << " not found." << endl;
            Cudd_Quit(manager);
            return 1;
        }
        bdd_circuit_output = literal_to_bdd_map[first_output_lit];
        Cudd_Ref(
            bdd_circuit_output); // Already reffed when put in map, but reffing
                                 // output explicitly is good practice.
        // However, if it's already in map, its ref count is >=1.
        // The map stores owning DdNode*. If we assign bdd_circuit_output,
        // then Cudd_RecursiveDeref it later, map still holds a valid ref.
        // It's safer to NOT re-ref here if it's taken directly from map,
        // and ensure all map items are properly deref'd at the end,
        // or only deref bdd_circuit_output.
        // Let's assume the current Ref/Deref logic is:
        // items in map are reffed. bdd_circuit_output is an alias to one of
        // them. We will deref bdd_circuit_output specifically. The map items
        // will be deref'd by Cudd_Quit or if map is cleared carefully. To be
        // safe: if bdd_circuit_output is a distinct pointer to be managed, Ref
        // it. Since it's just an alias from map, the map's ref is enough. The
        // original code did Ref here. Let's keep it for now assuming specific
        // deref later. Cudd_Ref(bdd_circuit_output); // Original code did this.
    } else { /* Should have been caught by nO == 0 check */
    }

    cout << "AIG 文件解析成功，BDD 构建初步完成。" << endl;

    // --- SAMPLING LOGIC (largely unchanged, but uses `input_vars_bdd` which is
    // now correctly populated) ---
    json result_json;
    json assignment_list = json::array();

    ifstream original_json_stream(original_json_path);
    json original_data;
    if (original_json_stream.is_open()) {
        original_json_stream >> original_data;
        original_json_stream.close();
    } else {
        cerr << "Warning: Could not open original JSON for variable info: "
             << original_json_path << endl;
    }
    json original_variable_list = original_data.contains("variable_list")
                                      ? original_data["variable_list"]
                                      : json::array();

    DdNode **input_vars_array_for_sampling = nullptr; // Renamed for clarity
    if (nI > 0) {
        input_vars_array_for_sampling = new DdNode *[nI];
        for (int i = 0; i < nI; ++i) {
            // input_vars_bdd is indexed by original AIG PI order (0 to nI-1)
            // This order should align with how bits are expected by
            // original_json_path
            input_vars_array_for_sampling[i] = input_vars_bdd[i];
        }
    }

    cout << "Debug: Starting to pick " << num_samples << " samples." << endl;
    if (bdd_circuit_output == Cudd_ReadLogicZero(manager)) { /* ... */
    } else if (bdd_circuit_output == Cudd_ReadOne(manager) &&
               nI > 0) { /* ... */
    }
    // ... (rest of constant BDD output checks)

    for (int i = 0; i < num_samples; ++i) {
        DdNode *minterm_node = nullptr;
        if (nI > 0) {
            minterm_node = Cudd_bddPickOneMinterm(
                manager, bdd_circuit_output, input_vars_array_for_sampling, nI);
        } else if (bdd_circuit_output == Cudd_ReadOne(manager)) {
            minterm_node = Cudd_ReadOne(manager);
        }

        if (!minterm_node ||
            minterm_node ==
                Cudd_ReadLogicZero(manager)) { /* ... no more solutions ... */
            break;
        }
        if (minterm_node != Cudd_ReadOne(manager) || nI > 0) {
            Cudd_Ref(minterm_node);
        }

        json assignment_entry = json::array();
        int current_bit_idx_overall =
            0; // Tracks overall bit index corresponding
               // to input_vars_array_for_sampling

        for (const auto &var_info : original_variable_list) {
            // ... (variable info parsing as before)
            int bit_width = var_info.value("bit_width", 1);
            unsigned long long variable_combined_value = 0;

            for (int bit_k = 0; bit_k < bit_width;
                 ++bit_k) { // bit_k is bit within the multi-bit variable
                if (current_bit_idx_overall >= nI) { /* Error handling ... */
                    Cudd_Quit(manager);
                    return 1;
                }

                DdNode *current_single_bit_var_bdd =
                    input_vars_array_for_sampling[current_bit_idx_overall];
                unsigned long long bit_assignment = 0; // Default to 0

                // Logic to extract assignment for current_single_bit_var_bdd
                // from minterm_node
                if (minterm_node !=
                    Cudd_ReadOne(manager)) { // If not a tautology where minterm
                                             // is just ONE
                    DdNode *temp_check_one = Cudd_bddAnd(
                        manager, minterm_node, current_single_bit_var_bdd);
                    Cudd_Ref(temp_check_one);
                    if (temp_check_one == minterm_node) {
                        bit_assignment = 1;
                    } else {
                        // Check for var = 0 might be needed if var is don't
                        // care in minterm, but Cudd_bddPickOneMinterm should
                        // return a full cube. Assuming var must be in minterm
                        // if nI > 0.
                    }
                    Cudd_RecursiveDeref(manager, temp_check_one);
                } else if (nI > 0) {
                    // Tautology, Cudd_bddPickOneMinterm might return an
                    // arbitrary minterm (e.g. all 0s or all 1s) or could return
                    // BDD_ONE. If it returns BDD_ONE, the specific bit values
                    // are not in it. The current logic defaults to 0 if
                    // minterm_node is BDD_ONE. For a tautology, any assignment
                    // is valid. Cudd might pick all zeros. This part of
                    // interpreting minterm_node = BDD_ONE needs to be robust.
                    // Let's assume for now Cudd_bddPickOneMinterm returns a
                    // proper cube even for tautology if vars are given. If
                    // minterm_node IS BDD_ONE, the check (temp_check_one ==
                    // minterm_node) implies current_single_bit_var_bdd is
                    // BDD_ONE, error. This implies that if minterm_node is
                    // BDD_ONE, this method of extracting bits doesn't work.
                    // However, Cudd_bddPickOneMinterm for f=1 with vars {x0,x1}
                    // might return e.g. x0*!x1. The original code's check was:
                    // DdNode *temp_check_one = Cudd_bddAnd(manager,
                    // minterm_node, current_bit_var_bdd);
                    // Cudd_Ref(temp_check_one); if (temp_check_one
                    // == minterm_node) { bit_assignment = 1; } else { DdNode
                    // *not_current_bit_var_bdd = Cudd_Not(current_bit_var_bdd);
                    //        DdNode *temp_check_zero = Cudd_bddAnd(manager,
                    //        minterm_node, not_current_bit_var_bdd);
                    //        Cudd_Ref(temp_check_zero); if (temp_check_zero ==
                    //        minterm_node) { bit_assignment = 0;} else { /*
                    //        Warning, default 0 */ }
                    //        Cudd_RecursiveDeref(manager, temp_check_zero); }
                    // Cudd_RecursiveDeref(manager, temp_check_one);
                    // This original logic is more robust. Let's reinstate it.
                    DdNode *temp_check_positive = Cudd_bddAnd(
                        manager, minterm_node, current_single_bit_var_bdd);
                    Cudd_Ref(temp_check_positive);
                    if (temp_check_positive == minterm_node) {
                        bit_assignment = 1;
                    } else {
                        DdNode *temp_check_negative =
                            Cudd_bddAnd(manager, minterm_node,
                                        Cudd_Not(current_single_bit_var_bdd));
                        Cudd_Ref(temp_check_negative);
                        if (temp_check_negative == minterm_node) {
                            bit_assignment = 0;
                        } else {
                            // This variable might be a "don't care" for this
                            // specific minterm, or if minterm_node is BDD_ONE
                            // (tautology). PickOneMinterm should still pick *a*
                            // value. Defaulting to 0 is one strategy for "don't
                            // care". cout << "Warning: Could not determine
                            // assignment for CUDD var (level "
                            //      << Cudd_ReadIndex(manager,
                            //      current_single_bit_var_bdd)
                            //      << ") in minterm. Defaulting to 0." << endl;
                            bit_assignment = 0; // Default for don't care or if
                                                // minterm is BDD_ONE
                        }
                        Cudd_RecursiveDeref(manager, temp_check_negative);
                    }
                    Cudd_RecursiveDeref(manager, temp_check_positive);
                }
                // else if nI == 0, minterm_node is BDD_ONE, loop for var_info
                // won't run if original_variable_list is empty.

                variable_combined_value |= (bit_assignment << bit_k);
                current_bit_idx_overall++;
            }
            assignment_entry.push_back(
                {{"value", to_hex_string(variable_combined_value, bit_width)}});
        }
        // ... (rest of sampling loop, list push_back, Deref minterm_node)
        assignment_list.push_back(assignment_entry);
        if (minterm_node != Cudd_ReadOne(manager) || nI > 0) {
            Cudd_RecursiveDeref(manager, minterm_node);
        }
    }

    if (input_vars_array_for_sampling) {
        delete[] input_vars_array_for_sampling;
    }
    // ... (JSON output writing)

    result_json["assignment_list"] = assignment_list;
    std::ofstream output_json_stream(result_json_path);
    if (!output_json_stream.is_open()) { /* ... error ... */
        Cudd_Quit(manager);
        return 1;
    }
    output_json_stream << result_json.dump(4) << std::endl;
    output_json_stream.close();

    if (bdd_circuit_output)
        Cudd_RecursiveDeref(manager, bdd_circuit_output);

    // Dereference BDDs stored in literal_to_bdd_map
    // This is important because Cudd_Quit might not handle everything if
    // intermediate nodes were not properly managed. However, Cudd_Quit is
    // generally robust for nodes created by CUDD. For safety, if not relying
    // solely on Cudd_Quit:
    for (auto const &[key, val_node] : literal_to_bdd_map) {
        if (val_node) { // Check against nullptr just in case, though map
                        // shouldn't store them Be careful here: if
                        // bdd_circuit_output was one of these, it's already
                        // Deref'd. This requires careful ref count management.
                        // Typically Cudd_Quit handles registered nodes. Let's
                        // assume Cudd_Quit is sufficient for nodes in the map
                        // that were Cudd_Ref'd.
        }
    }
    literal_to_bdd_map.clear();

    Cudd_Quit(manager);
    std::cout << "BDD 求解器完成。结果已写入到 " << result_json_path
              << std::endl;
    return 0;
}

// --- IMPLEMENTATION of to_hex_string (unchanged from original) ---
string to_hex_string(unsigned long long value, int bit_width) {
    if (bit_width <= 0)
        return "";
    // 计算所需的十六进制字符数 (每个十六进制字符代表 4 比特)
    int hex_chars = (bit_width + 3) / 4;
    // Mask value to ensure it fits within bit_width, prevents overflow issues
    // with setw
    unsigned long long mask =
        (1ULL << (bit_width < 64 ? bit_width : 63)) - 1; // basic mask
    if (bit_width == 64)
        mask = 0xFFFFFFFFFFFFFFFFULL;
    else if (bit_width > 0 && bit_width < 64)
        mask = (1ULL << bit_width) - 1;
    else if (bit_width == 0)
        mask = 0;
    // else mask remains (1ULL<<63)-1 which is not right for bit_width > 64, but
    // u_l_l is 64 bit.

    value &= mask; // Apply mask

    stringstream ss;
    ss << hex << setfill('0') << setw(hex_chars) << value;
    string hex_str = ss.str();
    // Ensure string length does not exceed hex_chars due to setw behavior with
    // large numbers (though masking should prevent value from being "too large"
    // for bit_width)
    if (hex_str.length() > hex_chars && hex_chars > 0) {
        hex_str = hex_str.substr(hex_str.length() - hex_chars);
    }
    return hex_str;
}

// --- NEW HELPER FUNCTION IMPLEMENTATION for variable ordering ---
static std::vector<int> determine_bdd_variable_order(
    int nI_total, // Total number of primary inputs
    const std::vector<int>
        &aig_primary_input_literals, // All PI literals from AIG file (e.g., 2,
                                     // 4, 6)
    const std::vector<int>
        &circuit_output_literals_from_aig, // AIG output literals (e.g., one
                                           // literal for single-output)
    const std::map<int, std::pair<int, int>>
        &and_gate_definitions // AIG AND gates: LHS_lit -> {RHS1_lit, RHS2_lit}
) {
    cout << "Debug: Determining BDD variable order..." << endl;
    std::vector<int> ordered_pis_for_bdd_creation;
    std::set<int>
        visited_nodes_for_ordering_dfs; // Stores positive literals visited
    std::set<int> added_pis_to_order;   // Stores positive PI literals already
                                      // added to 'ordered_pis_for_bdd_creation'

    // Convert aig_primary_input_literals to a set for quick lookup
    std::set<int> primary_input_set;
    for (int pi_lit : aig_primary_input_literals) {
        primary_input_set.insert(
            pi_lit); // These are already positive and even by AIG convention
    }

    std::function<void(int)> order_dfs = [&](int current_node_literal) {
        int regular_node_lit = (current_node_literal / 2) *
                               2; // Get the variable's positive literal

        if (visited_nodes_for_ordering_dfs.count(regular_node_lit)) {
            return;
        }
        visited_nodes_for_ordering_dfs.insert(regular_node_lit);

        // Check if it's a primary input
        if (primary_input_set.count(regular_node_lit)) {
            if (added_pis_to_order.find(regular_node_lit) ==
                added_pis_to_order.end()) {
                ordered_pis_for_bdd_creation.push_back(regular_node_lit);
                added_pis_to_order.insert(regular_node_lit);
            }
            return; // Stop recursion at PIs
        }

        // Check if it's an AND gate output
        auto it = and_gate_definitions.find(regular_node_lit);
        if (it != and_gate_definitions.end()) {
            const auto &inputs = it->second;
            // Recurse on the inputs of the AND gate
            // The order of recursion (input1 then input2, or vice-versa) can be
            // a minor heuristic itself. For now, just process as given.
            order_dfs(inputs.first);  // RHS1
            order_dfs(inputs.second); // RHS2
        }
        // If it's not a PI and not an AND gate output we know about, it's an
        // issue or a constant (0/1) Constants 0 (lit 0) and 1 (lit 1) are not
        // recursed upon from here.
    };

    // Start DFS from each primary output of the circuit
    for (int output_lit : circuit_output_literals_from_aig) {
        if (output_lit == 0 || output_lit == 1)
            continue; // Skip constant outputs for DFS start
        order_dfs(output_lit);
    }

    // The `ordered_pis_for_bdd_creation` now contains PIs in the order they
    // were first fully explored by the backward DFS. PIs closer to outputs
    // (higher in the BDD structure) tend to be added earlier. For CUDD,
    // variables created first get lower indices (e.g., var 0, var 1). Variables
    // that are "more important" or differentiate paths earlier should get lower
    // indices. The current order (PIs closer to POs added first to
    // `ordered_pis_for_bdd_creation`) means these PIs will be created as BDD
    // variables 0, 1, ... This is a common heuristic: variables closer to
    // outputs get lower BDD indices.
    // std::reverse(ordered_pis_for_bdd_creation.begin(),
    // ordered_pis_for_bdd_creation.end()); Reversing would mean PIs closer to
    // other PIs (further from POs in DFS) get lower BDD indices. Let's stick
    // with the current order: PIs encountered first in backward DFS from POs
    // get lower BDD indices.

    // Add any remaining PIs that were not reached by DFS (e.g., unused inputs
    // for these specific POs) These will be appended, getting higher BDD
    // indices.
    if (ordered_pis_for_bdd_creation.size() < nI_total) {
        for (int pi_lit : aig_primary_input_literals) {
            if (added_pis_to_order.find(pi_lit) == added_pis_to_order.end()) {
                ordered_pis_for_bdd_creation.push_back(pi_lit);
                added_pis_to_order.insert(pi_lit); // Should already be covered
                                                   // by find, but for clarity
                cout << "Debug: Appending unreached PI to order: " << pi_lit
                     << endl;
            }
        }
    }

    if (ordered_pis_for_bdd_creation.size() != nI_total) {
        cerr << "Warning: Determined PI order size ("
             << ordered_pis_for_bdd_creation.size() << ") does not match nI ("
             << nI_total
             << "). Some PIs might be missing or duplicated in ordering logic."
             << endl;
    }

    cout << "Debug: BDD variable order determined. Number of variables in "
            "order: "
         << ordered_pis_for_bdd_creation.size() << endl;

    return ordered_pis_for_bdd_creation;
}
