#include "solver_functions.h"

#include <algorithm>
#include <chrono> // 新增：用于计时
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <quadmath.h> // For __float128
#include <cmath>

namespace std {
inline __float128 nextafter(__float128 __x, __float128 __y) {
    return ::nextafterq(__x, __y); // 调用 quadmath.h 中的 nextafterq
}
} // namespace std

#include <random> // For std::mt19937 and std::uniform_real_distribution
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

// 前向声明PI支持集计算的递归辅助函数
static void get_pi_support_for_node_dfs_recursive(
    int current_literal,
    const std::map<int, std::pair<int, int>> &and_gate_definitions,
    const std::set<int> &all_pi_literals_set, std::set<int> &node_pi_support,
    std::set<int> &visited_for_this_support_dfs,
    std::map<int, std::set<int>> &memoized_supports);

// 获取节点PI支持集的主函数（带记忆化）
static std::set<int>
get_pi_support(int literal,
               const std::map<int, std::pair<int, int>> &and_gate_definitions,
               const std::set<int> &all_pi_literals_set,
               std::map<int, std::set<int>> &memoized_supports) {

    int regular_lit = (literal / 2) * 2;
    // 常量0或1没有PI支持 (AIG文字0和1)
    if (regular_lit == 0 || regular_lit == 1) {
        return {};
    }

    // 检查是否已有记忆化的结果
    auto memo_it = memoized_supports.find(regular_lit);
    if (memo_it != memoized_supports.end()) {
        return memo_it->second;
    }

    std::set<int> support;
    std::set<int>
        visited_dfs_for_this_call; // 为本次顶层调用创建新的visited集合
    // 调用递归辅助函数进行实际计算
    get_pi_support_for_node_dfs_recursive(
        regular_lit, and_gate_definitions, all_pi_literals_set, support,
        visited_dfs_for_this_call, memoized_supports);

    // 存储计算结果到记忆化map中
    memoized_supports[regular_lit] = support;
    return support;
}

// PI支持集计算的递归DFS实现
static void get_pi_support_for_node_dfs_recursive(
    int current_literal, // 初始调用时应传入规整形式 (regular form)
    const std::map<int, std::pair<int, int>> &and_gate_definitions,
    const std::set<int> &all_pi_literals_set,
    std::set<int> &node_pi_support, // 输出: 为此节点找到的PIs
    std::set<int>
        &visited_for_this_support_dfs, // 用于此DFS调用的访问过的节点集合
    std::map<int, std::set<int>> &memoized_supports) { // 全局记忆化map

    int regular_literal = (current_literal / 2) * 2;

    if (regular_literal == 0 || regular_literal == 1)
        return; // 基本常量情况

    // 如果在此DFS路径中已访问过此节点，则返回以避免循环
    if (visited_for_this_support_dfs.count(regular_literal))
        return;
    visited_for_this_support_dfs.insert(regular_literal);

    // 如果当前节点是PI
    if (all_pi_literals_set.count(regular_literal)) {
        node_pi_support.insert(regular_literal);
        // 将PI自身的support（即它自己）存入全局记忆化，如果尚不存在
        if (memoized_supports.find(regular_literal) ==
            memoized_supports.end()) {
            memoized_supports[regular_literal] = {regular_literal};
        }
        return;
    }

    // 在重新计算前，先检查全局记忆化map中是否已有此非PI节点的支持集
    // 这对于处理共享子逻辑非常重要
    auto memo_it = memoized_supports.find(regular_literal);
    if (memo_it != memoized_supports.end()) {
        const auto &cached_support = memo_it->second;
        node_pi_support.insert(cached_support.begin(), cached_support.end());
        return; // 使用缓存结果并返回
    }

    // 如果是AND门
    auto it_and_def = and_gate_definitions.find(regular_literal);
    if (it_and_def != and_gate_definitions.end()) {
        const auto &inputs = it_and_def->second;
        // 对子节点递归调用顶层的 get_pi_support 函数，以利用其对子节点的记忆化查询
        std::set<int> support1 =
            get_pi_support(inputs.first, and_gate_definitions,
                           all_pi_literals_set, memoized_supports);
        std::set<int> support2 =
            get_pi_support(inputs.second, and_gate_definitions,
                           all_pi_literals_set, memoized_supports);

        node_pi_support.insert(support1.begin(), support1.end());
        node_pi_support.insert(support2.begin(), support2.end());
    }
    // 如果节点不是PI，也不是AND门定义（例如，一个未连接的AIG节点或错误），则其支持集为空（或已收集到的）

    // 在此DFS路径探索完成后，将计算得到的支持集存入全局记忆化map
    // 确保不会覆盖由其他路径可能已为该节点计算并存储的（可能更完整的）支持集
    // （get_pi_support的顶层调用会处理最终的存储）
    // 此处的memoized_supports[regular_literal] = node_pi_support; 已移至get_pi_support的顶层调用后。
    // 在这里，我们只是累加到 node_pi_support。
}

// Helper function to perform DFS from a node to collect all PI literals in its support.
// This function is internal to determine_bdd_variable_order's needs or could be a static helper.
static void get_pi_support_for_component_dfs(
    int current_literal,
    const std::map<int, std::pair<int, int>> &and_gate_definitions,
    const std::set<int> &all_pi_literals_set,
    std::set<int> &component_pi_support, // Output: PIs found for this component
    std::set<int>
        &visited_for_support_dfs) { // Tracks visited nodes for THIS DFS call

    int regular_literal = (current_literal / 2) * 2;

    if (regular_literal == 0 ||
        regular_literal == 1) { // Constants 0 or 1 (literal 1)
        return;
    }
    if (visited_for_support_dfs.count(regular_literal)) {
        return;
    }
    visited_for_support_dfs.insert(regular_literal);

    if (all_pi_literals_set.count(regular_literal)) {
        component_pi_support.insert(regular_literal);
        return;
    }

    auto it = and_gate_definitions.find(regular_literal);
    if (it != and_gate_definitions.end()) {
        const auto &inputs = it->second;
        get_pi_support_for_component_dfs(
            inputs.first, and_gate_definitions, all_pi_literals_set,
            component_pi_support, visited_for_support_dfs);
        get_pi_support_for_component_dfs(
            inputs.second, and_gate_definitions, all_pi_literals_set,
            component_pi_support, visited_for_support_dfs);
    }
    // If not a PI and not in and_gate_definitions, it's an isolated node or error (or literal 1 if not handled as constant earlier)
}

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

// --- START OF MODIFICATIONS FOR NEW SAMPLING ---

// Structure to store path counts for dynamic programming
struct PathCounts {
    __float128
        even_cnt; // Paths with an even number of complemented edges to 1-terminal
    __float128
        odd_cnt; // Paths with an odd number of complemented edges to 1-terminal
};

// Global map for memoization in path counting DP
static std::map<DdNode *, PathCounts> path_counts_memo;
// Global random number generator
static std::mt19937 rng;

// Recursive function to compute path counts using dynamic programming
PathCounts compute_path_counts_recursive(DdNode *node_regular,
                                         DdManager *manager) {
    if (node_regular == Cudd_ReadOne(manager)) {
        return {
            1.0Q,
            0.0Q}; // One path (empty) with 0 complemented edges to 1-terminal
    }
    if (node_regular == Cudd_ReadLogicZero(manager)) {
        return {0.0Q, 0.0Q}; // No path to 1-terminal
    }

    auto it = path_counts_memo.find(node_regular);
    if (it != path_counts_memo.end()) {
        return it->second;
    }

    DdNode *E_child = Cudd_E(node_regular); // Else child
    DdNode *T_child = Cudd_T(node_regular); // Then child

    DdNode *E_child_regular = Cudd_Regular(E_child);
    DdNode *T_child_regular = Cudd_Regular(T_child);

    PathCounts counts_E =
        compute_path_counts_recursive(E_child_regular, manager);
    PathCounts counts_T =
        compute_path_counts_recursive(T_child_regular, manager);

    __float128 current_even_cnt = 0.0Q;
    __float128 current_odd_cnt = 0.0Q;

    // Paths via Else child
    if (Cudd_IsComplement(E_child)) { // Edge to Else child is complemented
        current_even_cnt += counts_E.odd_cnt;
        current_odd_cnt += counts_E.even_cnt;
    } else {
        current_even_cnt += counts_E.even_cnt;
        current_odd_cnt += counts_E.odd_cnt;
    }

    // Paths via Then child
    if (Cudd_IsComplement(T_child)) { // Edge to Then child is complemented
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

// DFS function to generate a random solution
// assignment_map: CUDD variable index -> 0 or 1
bool generate_random_solution_dfs(
    DdNode *current_node_regular, bool accumulated_odd_complements_so_far,
    DdManager *manager, std::map<int, int> &assignment_map,
    std::uniform_real_distribution<__float128> &dist) {
    // 基本情况1：成功到达1-terminal。
    if (current_node_regular == Cudd_ReadOne(manager)) {
        return true;
    }

    // 基本情况2：到达0-terminal。此路径无效，不能满足函数。
    if (current_node_regular == Cudd_ReadLogicZero(manager)) {
        return false;
    }

    // 递归步骤：处理内部BDD节点
    DdNode *E_child_ptr = Cudd_E(current_node_regular);
    DdNode *T_child_ptr = Cudd_T(current_node_regular);

    DdNode *E_child_regular = Cudd_Regular(E_child_ptr);
    DdNode *T_child_regular = Cudd_Regular(T_child_ptr);

    bool is_complement_E = Cudd_IsComplement(E_child_ptr);
    bool is_complement_T = Cudd_IsComplement(T_child_ptr);

    // 获取从子节点（正则形式）到1-terminal的路径计数。
    // 这会调用主要的DP函数，该函数能处理終端節點和备忘录。
    PathCounts counts_from_E_child =
        compute_path_counts_recursive(E_child_regular, manager);
    PathCounts counts_from_T_child =
        compute_path_counts_recursive(T_child_regular, manager);

    bool required_odd_parity_for_E_path =
        accumulated_odd_complements_so_far ^ is_complement_E;
    __float128 cnt_paths_via_E;
    if (required_odd_parity_for_E_path) {
        cnt_paths_via_E = counts_from_E_child.odd_cnt;
    } else {
        cnt_paths_via_E = counts_from_E_child.even_cnt;
    }

    bool required_odd_parity_for_T_path =
        accumulated_odd_complements_so_far ^ is_complement_T;
    __float128 cnt_paths_via_T;
    if (required_odd_parity_for_T_path) {
        cnt_paths_via_T = counts_from_T_child.odd_cnt;
    } else {
        cnt_paths_via_T = counts_from_T_child.even_cnt;
    }

    __float128 total_valid_continuing_paths = cnt_paths_via_E + cnt_paths_via_T;
    int var_index = Cudd_NodeReadIndex(current_node_regular);

    // "卡住" 条件：从此节点无法继续找到满足目标奇偶性的路径。
    if (total_valid_continuing_paths <= 0.0Q) {
        return false;
    }

    // 根据概率选择分支
    __float128 prob_take_E_branch;
    if (total_valid_continuing_paths > 0.0Q) {
        prob_take_E_branch = cnt_paths_via_E / total_valid_continuing_paths;
    } else {
        prob_take_E_branch = 0.5Q;
    }

    __float128 random_draw_val = dist(rng);

    if (random_draw_val < prob_take_E_branch) {
        // 选择 Else 分支 (当前变量赋值为 0)
        assignment_map[var_index] = 0;
        return generate_random_solution_dfs(E_child_regular,
                                            required_odd_parity_for_E_path,
                                            manager, assignment_map, dist);
    } else {
        // 选择 Then 分支 (当前变量赋值为 1)
        assignment_map[var_index] = 1;
        return generate_random_solution_dfs(T_child_regular,
                                            required_odd_parity_for_T_path,
                                            manager, assignment_map, dist);
    }
}
// --- END OF MODIFICATIONS FOR NEW SAMPLING ---

// 实现主 BDD 求解器函数
int aig_to_bdd_solver(const string &aig_file_path,
                      const string &original_json_path, int num_samples,
                      const string &result_json_path,
                      unsigned int random_seed) {
    auto function_start_time =
        std::chrono::high_resolution_clock::now(); // 函数总计时开始

    // Initialize random number generator for new sampling
    rng.seed(random_seed);

    DdManager *manager;
    DdNode *bdd_circuit_output = nullptr;
    int nM = 0, nI = 0, nL = 0, nO = 0, nA = 0;

    // 1. 初始化 CUDD 管理器
    auto cudd_init_start_time = std::chrono::high_resolution_clock::now();
    manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (!manager) {
        cerr << "Error: CUDD manager initialization failed." << endl;
        return 1;
    }
    Cudd_AutodynEnable(manager, CUDD_REORDER_SIFT);
    cout << "Debug: Automatic dynamic variable reordering enabled "
            "(CUDD_REORDER_SIFT)."
         << endl;
    // Cudd_Srandom(manager, random_seed); // CUDD's internal random for reordering/picking
    // We use our own rng for path selection.
    auto cudd_init_end_time = std::chrono::high_resolution_clock::now();
    auto cudd_init_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            cudd_init_end_time - cudd_init_start_time);
    cout << "LOG: CUDD manager initialization took "
         << cudd_init_duration.count() << " ms." << endl;

    map<int, DdNode *> literal_to_bdd_map;

    literal_to_bdd_map[0] = Cudd_ReadLogicZero(manager);
    literal_to_bdd_map[1] = Cudd_ReadOne(manager);

    ifstream aig_file_stream(aig_file_path);
    if (!aig_file_stream.is_open()) {
        cerr << "Error: Could not open AIG file: " << aig_file_path << endl;
        Cudd_Quit(manager);
        return 1;
    }

    string line;
    // 1. 解析 AIGER 头信息 (aag M I L O A)
    auto aig_header_parse_start_time =
        std::chrono::high_resolution_clock::now();
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
    auto aig_header_parse_end_time = std::chrono::high_resolution_clock::now();
    auto aig_header_parse_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            aig_header_parse_end_time - aig_header_parse_start_time);
    cout << "LOG: AIGER header parsing took "
         << aig_header_parse_duration.count() << " ms." << endl;

    // --- MODIFICATION START: Pre-read AIG structure for variable ordering ---
    auto aig_preread_start_time = std::chrono::high_resolution_clock::now();
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
    auto aig_preread_end_time = std::chrono::high_resolution_clock::now();
    auto aig_preread_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            aig_preread_end_time - aig_preread_start_time);
    cout << "LOG: Pre-reading AIG structure took "
         << aig_preread_duration.count() << " ms." << endl;
    // --- MODIFICATION END: Pre-read AIG structure ---

    // --- MODIFICATION START: Determine BDD variable order and create variables
    auto var_order_start_time = std::chrono::high_resolution_clock::now();
    vector<DdNode *> input_vars_bdd(
        nI); // This will store DdNode* for PIs, indexed by their original AIG
             // file order (0 to nI-1). nI is from AIG header.

    // 调用修改后的 determine_bdd_variable_order 函数
    // 参数 nI 是从AIG头中读取的输入数量，这应该与 aig_primary_input_literals.size() 相符
    std::vector<int> ordered_aig_pi_literals_for_bdd_creation =
        determine_bdd_variable_order(nI, aig_primary_input_literals,
                                     circuit_output_literals_from_aig,
                                     and_gate_definitions);
    // 注意：这里传递的 nI 是来自 AIG header 的 MILOA 中的 'I'。
    // determine_bdd_variable_order 内部逻辑需要确保处理的 PI 数量与此一致或基于 aig_primary_input_literals.size()

    auto var_order_end_time = std::chrono::high_resolution_clock::now();
    auto var_order_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            var_order_end_time - var_order_start_time);
    cout << "LOG: Determining BDD variable order took "
         << var_order_duration.count() << " ms." << endl;

    auto bdd_var_creation_start_time =
        std::chrono::high_resolution_clock::now();
    cout << "Debug: Creating "
         << ordered_aig_pi_literals_for_bdd_creation
                .size() // 使用实际得到的排序后PI数量
         << " BDD input variables based on determined order (AIG header nI: "
         << nI << ")." << endl;

    if (ordered_aig_pi_literals_for_bdd_creation.size() > (size_t)nI) {
        cout << "Warning: Number of PIs in determined order ("
             << ordered_aig_pi_literals_for_bdd_creation.size()
             << ") is greater than nI from header (" << nI
             << "). Using determined order size." << endl;
    }

    for (size_t i = 0; i < ordered_aig_pi_literals_for_bdd_creation.size();
         ++i) {
        int current_aig_pi_literal_to_create =
            ordered_aig_pi_literals_for_bdd_creation[i];

        // 检查这个PI是否已经在map中（理论上不应该，因为是按顺序第一次创建）
        if (literal_to_bdd_map.count(current_aig_pi_literal_to_create)) {
            cerr << "Error: PI literal " << current_aig_pi_literal_to_create
                 << " already has a BDD node before creation in ordered list. "
                    "Logic error."
                 << endl;
            // ... 错误处理 ...
            return 1;
        }

        DdNode *var_node =
            Cudd_bddNewVar(manager); // Creates BDD var at CUDD level 'i'
        if (!var_node) {             /* ... Error handling ... */
            cerr << "Cudd_bddNewVar failed for PI "
                 << current_aig_pi_literal_to_create << " at CUDD index " << i
                 << endl;
            Cudd_Quit(manager);
            return 1;
        }

        literal_to_bdd_map[current_aig_pi_literal_to_create] = var_node;
        literal_to_bdd_map[current_aig_pi_literal_to_create + 1] =
            Cudd_Not(var_node);
        Cudd_Ref(var_node);
        Cudd_Ref(Cudd_Not(var_node));

        // 更新 input_vars_bdd：通过AIG literal找到它在原始AIG输入列表中的索引
        auto it_orig_idx = aig_literal_to_original_pi_index.find(
            current_aig_pi_literal_to_create);
        if (it_orig_idx != aig_literal_to_original_pi_index.end()) {
            int original_pi_file_order_idx = it_orig_idx->second;
            if (original_pi_file_order_idx <
                nI) { // 确保索引在 input_vars_bdd 的界限内
                input_vars_bdd[original_pi_file_order_idx] = var_node;
            } else {
                cerr << "Warning: Original PI index "
                     << original_pi_file_order_idx << " for literal "
                     << current_aig_pi_literal_to_create
                     << " is out of bounds for input_vars_bdd (size " << nI
                     << ")." << endl;
            }
        } else {
            cerr << "Warning: AIG PI literal "
                 << current_aig_pi_literal_to_create
                 << " from ordering not found in "
                    "aig_literal_to_original_pi_index map."
                 << endl;
        }
    }
    cout << "Debug: Finished creating "
         << Cudd_ReadSize(manager) /*实际创建的BDD变量数*/
         << " BDD input variables." << endl;

    auto bdd_var_creation_end_time = std::chrono::high_resolution_clock::now();
    auto bdd_var_creation_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            bdd_var_creation_end_time - bdd_var_creation_start_time);
    cout << "LOG: Creating BDD input variables took "
         << bdd_var_creation_duration.count() << " ms." << endl;

    // Create a map from CUDD variable index to original AIG PI index (0 to nI-1 in AIG file)
    // This is useful for interpreting the assignment from DFS.
    std::map<int, int> cudd_idx_to_original_aig_pi_file_idx;
    for (int aig_file_idx = 0; aig_file_idx < nI; ++aig_file_idx) {
        if (input_vars_bdd[aig_file_idx]) { // Should be populated
            int cudd_var_idx = Cudd_NodeReadIndex(input_vars_bdd[aig_file_idx]);
            if (Cudd_bddIsVar(
                    manager,
                    input_vars_bdd
                        [aig_file_idx])) { // Ensure it's an actual BDD var
                cudd_idx_to_original_aig_pi_file_idx[cudd_var_idx] =
                    aig_file_idx;
            } else {
                cerr << "Warning: input_vars_bdd[" << aig_file_idx
                     << "] with CUDD index " << cudd_var_idx
                     << " is not a simple BDD variable node." << endl;
                // This might happen if a PI is constant or somehow optimized away before this stage, though unlikely with current setup.
            }
        } else {
            cerr << "Error: input_vars_bdd[" << aig_file_idx
                 << "] was not populated." << endl;
            // Handle error
        }
    }
    // --- MODIFICATION END: Determine BDD variable order and create variables ---

    // 5. 处理 AND 门 (A 行) - Now using pre-read and_gate_lines_for_processing
    auto and_gate_processing_start_time =
        std::chrono::high_resolution_clock::now();
    cout << "Debug: Building BDDs for " << nA << " AND gates." << endl;
    for (const auto &gate_def : and_gate_lines_for_processing) {
        int output_lit = std::get<0>(gate_def);
        int input1_lit = std::get<1>(gate_def);
        int input2_lit = std::get<2>(gate_def);

        auto it1 = literal_to_bdd_map.find(input1_lit);
        auto it2 = literal_to_bdd_map.find(input2_lit);

        if (it1 == literal_to_bdd_map.end() ||
            it2 == literal_to_bdd_map.end()) {
            cerr << "Error: AIGER parsing failed for AND gate (" << output_lit
                 << " = " << input1_lit << " & " << input2_lit;
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
    auto and_gate_processing_end_time =
        std::chrono::high_resolution_clock::now();
    auto and_gate_processing_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            and_gate_processing_end_time - and_gate_processing_start_time);
    cout << "LOG: Processing AND gates took "
         << and_gate_processing_duration.count() << " ms." << endl;

    // 6. 获取最终输出的 BDD 节点 (第一个输出)
    auto get_output_bdd_start_time = std::chrono::high_resolution_clock::now();
    if (nO > 0) { // circuit_output_literals_from_aig was populated earlier
        int first_output_lit = circuit_output_literals_from_aig[0];
        if (!literal_to_bdd_map.count(first_output_lit)) {
            cerr << "Error: Final output BDD node for literal "
                 << first_output_lit << " not found." << endl;
            Cudd_Quit(manager);
            return 1;
        }
        bdd_circuit_output = literal_to_bdd_map[first_output_lit];
        Cudd_Ref(bdd_circuit_output);
    } else { /* Should have been caught by nO == 0 check */
    }
    auto get_output_bdd_end_time = std::chrono::high_resolution_clock::now();
    auto get_output_bdd_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            get_output_bdd_end_time - get_output_bdd_start_time);
    cout << "LOG: Getting final output BDD node took "
         << get_output_bdd_duration.count() << " ms." << endl;

    cout << "AIG 文件解析成功，BDD 构建初步完成。" << endl;

    // --- NEW SAMPLING LOGIC ---
    auto sampling_logic_start_time = std::chrono::high_resolution_clock::now();
    json result_json;
    json assignment_list = json::array();

    auto original_json_read_start_time =
        std::chrono::high_resolution_clock::now();
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
    auto original_json_read_end_time =
        std::chrono::high_resolution_clock::now();
    auto original_json_read_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            original_json_read_end_time - original_json_read_start_time);
    cout << "LOG: Reading original JSON took "
         << original_json_read_duration.count() << " ms." << endl;

    if (!bdd_circuit_output) {
        cerr << "Error: bdd_circuit_output is null before sampling." << endl;
        Cudd_Quit(manager);
        return 1;
    }

    // --- Perform Dynamic Programming to count paths ---
    path_counts_memo.clear(); // Clear memoization table for this BDD
    DdNode *regular_root_bdd = Cudd_Regular(bdd_circuit_output);
    cout << "LOG: Starting DP for path counting." << endl;
    auto dp_start_time = std::chrono::high_resolution_clock::now();
    PathCounts root_counts =
        compute_path_counts_recursive(regular_root_bdd, manager);
    auto dp_end_time = std::chrono::high_resolution_clock::now();
    auto dp_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        dp_end_time - dp_start_time);
    cout << "LOG: DP for path counting took " << dp_duration.count() << " ms. "
         << "Memoized nodes: " << path_counts_memo.size() << endl;

    __float128 total_target_paths;
    bool initial_accumulated_odd_complements =
        Cudd_IsComplement(bdd_circuit_output);
    if (initial_accumulated_odd_complements) {
        total_target_paths = root_counts.odd_cnt;
    } else {
        total_target_paths = root_counts.even_cnt;
    }

    // Convert __float128 to string for printing, as direct cout might not work well.
    char buf[128];
    quadmath_snprintf(buf, sizeof(buf), "%.0Qf", total_target_paths);
    cout << "LOG: Total paths satisfying the 'overall even complement edges' "
            "condition: "
         << buf << endl;

    if (total_target_paths <= 0.0Q) {
        cout << "LOG: No satisfying assignments that result in an even number "
                "of complement edges for the function."
             << endl;
        // assignment_list 将为空，这是正确的行为
    } else {
        cout << "Debug: Starting to pick " << num_samples
             << " samples using new DFS method." << endl;

        // 声明和初始化 dist_float128
        std::uniform_real_distribution<__float128> dist_float128(0.0Q, 1.0Q);

        int samples_successfully_generated = 0;
        int total_dfs_attempts = 0;
        const int MAX_TOTAL_DFS_ATTEMPTS = num_samples * 200;

        // 新增：用于存储已生成的唯一解的签名，以避免重复
        std::set<std::string> unique_assignment_signatures;

        while (samples_successfully_generated < num_samples &&
               total_dfs_attempts < MAX_TOTAL_DFS_ATTEMPTS) {
            total_dfs_attempts++; // 每次循环都增加尝试次数
            std::map<int, int>
                current_assignment_cudd_indices; // DFS的结果会放在这里
            bool current_sample_generation_successful = false;

            // 特殊情况处理: nI == 0 (无输入变量)
            if (nI == 0) {
                if (bdd_circuit_output == Cudd_ReadOne(manager) &&
                    !initial_accumulated_odd_complements) {
                    current_sample_generation_successful = true;
                } else {
                    cout << "LOG: Cannot generate sample for nI=0 case "
                            "(unexpected, or target path count was non-zero "
                            "for unsat case). Stopping sampling."
                         << endl;
                    break;
                }
            }
            // 特殊情况处理: nI > 0 但BDD根是常数
            else if (regular_root_bdd == Cudd_ReadOne(manager)) {
                if (!initial_accumulated_odd_complements) {
                    current_sample_generation_successful = true;
                } else {
                    cout << "LOG: Function is constant 1, but parity "
                            "requirement not met. Stopping sampling."
                         << endl;
                    break;
                }
            } else if (regular_root_bdd == Cudd_ReadLogicZero(manager)) {
                cout << "LOG: Function is constant 0. No solutions. Stopping "
                        "sampling (should have been caught by "
                        "total_target_paths <= 0)."
                     << endl;
                break;
            }
            // 一般情况: nI > 0 且函数不是常数，执行DFS
            else {
                // 在调用DFS前，确保 current_assignment_cudd_indices 是空的
                current_assignment_cudd_indices.clear();
                current_sample_generation_successful =
                    generate_random_solution_dfs(
                        regular_root_bdd, initial_accumulated_odd_complements,
                        manager, current_assignment_cudd_indices,
                        dist_float128);
            }

            if (current_sample_generation_successful) {
                // --- 开始JSON格式化一个成功生成的样本 ---
                json assignment_entry = json::array();
                int current_bit_idx_overall = 0;

                for (const auto &var_info : original_variable_list) {
                    int bit_width = var_info.value("bit_width", 1);
                    unsigned long long variable_combined_value = 0;

                    for (int bit_k = 0; bit_k < bit_width; ++bit_k) {
                        if (current_bit_idx_overall >= nI) {
                            cerr << "FATAL ERROR: JSON variable structure "
                                    "inconsistent with AIG nI ("
                                 << current_bit_idx_overall << " >= " << nI
                                 << "). Cannot proceed." << endl;
                            if (bdd_circuit_output)
                                Cudd_RecursiveDeref(manager,
                                                    bdd_circuit_output);
                            for (auto const &[key, val_node] :
                                 literal_to_bdd_map)
                                if (val_node)
                                    Cudd_RecursiveDeref(manager, val_node);
                            literal_to_bdd_map.clear();
                            path_counts_memo.clear();
                            if (manager)
                                Cudd_Quit(manager);
                            return 1;
                        }

                        unsigned long long bit_assignment = 0;
                        if (nI > 0) {
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
                                    current_assignment_cudd_indices.end()) {
                                    bit_assignment = it_assign->second;
                                } else {
                                    bit_assignment =
                                        0; // Don't care, default to 0
                                }
                            } else {
                                bit_assignment =
                                    0; // PI not in BDD support or error, default to 0
                            }
                        }
                        variable_combined_value |= (bit_assignment << bit_k);
                        current_bit_idx_overall++;
                    } // end bit_k loop
                    assignment_entry.push_back(
                        {{"value",
                          to_hex_string(variable_combined_value, bit_width)}});
                } // end var_info loop
                // --- 结束JSON格式化 ---

                // --- 新增：检查解的唯一性 ---
                std::string assignment_signature =
                    assignment_entry
                        .dump(); // 使用json对象的dump()方法生成紧凑的字符串表示作为签名

                //尝试将签名插入集合中，.second 会在插入成功时为 true (即这是一个新的唯一解)
                if (unique_assignment_signatures.insert(assignment_signature)
                        .second) {
                    assignment_list.push_back(assignment_entry);
                    samples_successfully_generated++;
                }
                // --- 唯一性检查结束 ---
            }
        } // end while loop for sampling
    } // end if (total_target_paths > 0.0Q)
    // --- END NEW SAMPLING LOGIC ---

    auto sampling_logic_end_time = std::chrono::high_resolution_clock::now();
    auto sampling_logic_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            sampling_logic_end_time - sampling_logic_start_time);
    cout << "LOG: Entire sampling logic (including JSON read, DP, and sample "
            "picking) took "
         << sampling_logic_duration.count() << " ms." << endl;

    auto json_write_start_time = std::chrono::high_resolution_clock::now();
    result_json["assignment_list"] = assignment_list;
    std::ofstream output_json_stream(result_json_path);
    if (!output_json_stream.is_open()) { /* ... error ... */
        cerr << "Error: Could not open result JSON file for writing: "
             << result_json_path << endl;
        if (bdd_circuit_output)
            Cudd_RecursiveDeref(manager, bdd_circuit_output);
        Cudd_Quit(manager);
        return 1;
    }
    output_json_stream << result_json.dump(4) << std::endl;
    output_json_stream.close();
    auto json_write_end_time = std::chrono::high_resolution_clock::now();
    auto json_write_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            json_write_end_time - json_write_start_time);
    cout << "LOG: Writing result JSON took " << json_write_duration.count()
         << " ms." << endl;

    auto cudd_cleanup_start_time = std::chrono::high_resolution_clock::now();
    if (bdd_circuit_output)
        Cudd_RecursiveDeref(manager, bdd_circuit_output);

    // Dereference BDDs stored in literal_to_bdd_map
    // This map stores BDDs for PIs and intermediate AND gates.
    // They were Ref'd when put into the map.
    for (auto const &[key, val_node] : literal_to_bdd_map) {
        if (val_node) {
            // Check if this node is the bdd_circuit_output that was already dereferenced.
            // However, bdd_circuit_output might be different if it was Cudd_Not'd from map.
            // Safe approach is to just deref all. If bdd_circuit_output was one of them,
            // its ref count was incremented again when assigned to bdd_circuit_output.
            // The Cudd_Ref for bdd_circuit_output itself handles one reference.
            // The Cudd_Ref for map storage handles another. So, separate derefs are needed.
            Cudd_RecursiveDeref(manager, val_node);
        }
    }
    literal_to_bdd_map.clear();
    path_counts_memo.clear(); // Clear DP memoization map

    Cudd_Quit(manager);
    auto cudd_cleanup_end_time = std::chrono::high_resolution_clock::now();
    auto cudd_cleanup_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            cudd_cleanup_end_time - cudd_cleanup_start_time);
    cout << "LOG: CUDD cleanup took " << cudd_cleanup_duration.count() << " ms."
         << endl;

    std::cout << "BDD 求解器完成。结果已写入到 " << result_json_path
              << std::endl;

    auto function_end_time = std::chrono::high_resolution_clock::now();
    auto function_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            function_end_time - function_start_time);
    cout << "LOG: Total execution time for aig_to_bdd_solver: "
         << function_duration.count() << " ms." << endl;

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

// 主变量排序函数
static std::vector<int> determine_bdd_variable_order(
    int nI_total_from_header,
    const std::vector<int> &aig_primary_input_literals,
    const std::vector<int> &circuit_output_literals_from_aig,
    const std::map<int, std::pair<int, int>> &and_gate_definitions) {
    std::cout << "Debug: Determining BDD variable order (DFS with PI support "
                 "heuristic for AND children)..."
              << std::endl;
    std::vector<int> ordered_pis_for_bdd_creation;
    std::set<int> added_pis_to_final_order_set;
    std::set<int> all_pi_literals_set(aig_primary_input_literals.begin(),
                                      aig_primary_input_literals.end());
    std::map<int, std::set<int>> memoized_pi_supports;
    std::set<int> visited_nodes_for_ordering_dfs;
    std::function<void(int)> order_dfs_main = [&](int current_node_literal) {
        int regular_node_lit = (current_node_literal / 2) * 2;

        // 基本情况：常量0或1没有变量需要排序
        if (regular_node_lit == 0 || regular_node_lit == 1)
            return;

        // 如果此节点在主排序DFS中已访问过，则返回
        if (visited_nodes_for_ordering_dfs.count(regular_node_lit)) {
            return;
        }
        visited_nodes_for_ordering_dfs.insert(regular_node_lit);

        // 如果当前节点是PI
        if (all_pi_literals_set.count(regular_node_lit)) {
            // 如果此PI尚未添加到最终排序列表中
            if (added_pis_to_final_order_set.find(regular_node_lit) ==
                added_pis_to_final_order_set.end()) {
                ordered_pis_for_bdd_creation.push_back(regular_node_lit);
                added_pis_to_final_order_set.insert(regular_node_lit);
            }
            return; // DFS在此路径到达PI后终止
        }

        // 如果当前节点是AND门
        auto it_and = and_gate_definitions.find(regular_node_lit);
        if (it_and != and_gate_definitions.end()) {
            const auto &inputs = it_and->second;

            // 启发式：根据AND门子节点的PI支持集大小来决定递归顺序
            std::set<int> support1 =
                get_pi_support(inputs.first, and_gate_definitions,
                               all_pi_literals_set, memoized_pi_supports);
            std::set<int> support2 =
                get_pi_support(inputs.second, and_gate_definitions,
                               all_pi_literals_set, memoized_pi_supports);

            // 优先处理PI支持集较小（或相等）的子节点
            if (support1.size() <= support2.size()) {
                order_dfs_main(inputs.first);
                order_dfs_main(inputs.second);
            } else {
                order_dfs_main(inputs.second);
                order_dfs_main(inputs.first);
            }
        }
        // 如果节点不是PI，不是0/1，也不在and_gate_definitions中，
        // 可能是AIG结构问题或未处理的叶节点类型。
    };

    // 从主输出开始DFS进行变量排序
    // 为简单起见，这里只处理第一个主输出。
    // 更通用的方案可能需要处理所有PO或构建一个组合PO。
    if (!circuit_output_literals_from_aig.empty()) {
        int main_po_lit = circuit_output_literals_from_aig[0];
        std::cout
            << "Debug: Starting variable ordering DFS from Main PO Literal: "
            << main_po_lit << std::endl;
        order_dfs_main(main_po_lit);
    } else {
        std::cout << "Warning: No primary outputs found in AIG for variable "
                     "ordering strategy."
                  << std::endl;
    }

    // 回退步骤：添加任何未从PO通过DFS访问到的PI
    // （确保所有在AIG文件中定义的PI都在排序列表中，即使它们是未使用的或仅与其他PO相关）
    // 按照它们在AIG文件中的原始顺序迭代，以保证这部分添加的稳定性
    for (int pi_lit_from_file : aig_primary_input_literals) {
        int regular_pi_lit = (pi_lit_from_file / 2) * 2;
        if (all_pi_literals_set.count(regular_pi_lit)) { // 再次确认是有效的PI
            if (added_pis_to_final_order_set.find(regular_pi_lit) ==
                added_pis_to_final_order_set.end()) {
                ordered_pis_for_bdd_creation.push_back(regular_pi_lit);
                added_pis_to_final_order_set.insert(regular_pi_lit);
                // std::cout << "Debug: Adding fallback PI to order (not reached from PO DFS): " << regular_pi_lit << std::endl;
            }
        }
    }

    // 合理性检查和日志输出
    if (ordered_pis_for_bdd_creation.size() != all_pi_literals_set.size() &&
        !aig_primary_input_literals.empty()) {
        std::cout << "Warning: Final ordered PI count ("
                  << ordered_pis_for_bdd_creation.size()
                  << ") does not match unique PI count from AIG file ("
                  << all_pi_literals_set.size() << ")." << std::endl;
    }
    // 与AIG头中的nI进行比较
    if (all_pi_literals_set.size() != (size_t)nI_total_from_header) {
        std::cout << "Warning: Count of unique PIs from AIG file ("
                  << all_pi_literals_set.size()
                  << ") does not match nI from AIG header ("
                  << nI_total_from_header << ")." << std::endl;
    }

    std::cout
        << "Debug: BDD variable order determined. Total variables in order: "
        << ordered_pis_for_bdd_creation.size() << ". Order: ";
    size_t print_limit = 20; // 限制打印的PI数量，避免过长的日志
    for (size_t i = 0; i < ordered_pis_for_bdd_creation.size(); ++i) {
        if (ordered_pis_for_bdd_creation.size() <= print_limit * 2 ||
            i < print_limit ||
            i >= ordered_pis_for_bdd_creation.size() - print_limit) {
            std::cout << ordered_pis_for_bdd_creation[i]
                      << (i == ordered_pis_for_bdd_creation.size() - 1 ? ""
                                                                       : ", ");
        } else if (i == print_limit) {
            std::cout << "... ("
                      << (ordered_pis_for_bdd_creation.size() - 2 * print_limit)
                      << " more PIs) ..., ";
        }
    }
    std::cout << std::endl;

    return ordered_pis_for_bdd_creation;
}