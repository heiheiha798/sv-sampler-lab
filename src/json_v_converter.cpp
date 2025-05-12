#include "nlohmann/json.hpp"
#include "solver_functions.h" // 假设 solver_functions.h 包含必要的声明
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <numeric>   // For std::iota
#include <algorithm> // For std::sort, std::unique
#include <optional>  // For std::optional
#include <climits>

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

struct DSUComponentForVerilogOrder {
    size_t size_metric; // 用于排序的度量，例如约束数量或PI数量
    std::vector<std::string> wire_names_in_component;
    // 该组件包含的有效线网名称（已按原始顺序）
    int representative_dsu_root_idx; // 用于稳定排序的DSU根索引

    // 注意：这里的 < 和 > 操作符需要根据具体是从小到大还是从大到小来定义
};

// 用于表示常量求值结果
struct EvaluatedConstant {
    long long value;
    int bit_width;
    bool is_zero;
    bool is_all_ones;

    static std::optional<EvaluatedConstant>
    from_verilog_string(const std::string &s) {
        EvaluatedConstant ec;
        ec.is_zero = false;
        ec.is_all_ones = false;
        ec.bit_width = -1;
        try {
            size_t prime_pos = s.find('\'');
            if (prime_pos != std::string::npos) {
                if (prime_pos == 0)
                    return std::nullopt;
                ec.bit_width = std::stoi(s.substr(0, prime_pos));
                if (ec.bit_width <= 0)
                    return std::nullopt;
                char type_char = s[prime_pos + 1];
                std::string val_str = s.substr(prime_pos + 2);
                if (val_str.empty())
                    return std::nullopt;
                unsigned long long num_val = 0;
                if (type_char == 'b') {
                    if (val_str.find_first_not_of("01xzX"
                                                  "Z") != std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 2);

                } else if (type_char == 'h' || type_char == 'H') {
                    if (val_str.find_first_not_of("0123456789abcdefABCDEFxzX"
                                                  "Z") != std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 16);

                } else if (type_char == 'd' || type_char == 'D') {
                    if (val_str.find_first_not_of("0123456789xzX"
                                                  "Z") != std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 10);

                } else if (type_char == 'o' || type_char == 'O') {
                    if (val_str.find_first_not_of("01234567xzX"
                                                  "Z") != std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 8);

                } else {
                    return std::nullopt;
                }
                ec.value = static_cast<long long>(num_val);
                ec.is_zero = (num_val == 0);
                if (ec.bit_width > 0 && ec.bit_width <= 64) {
                    unsigned long long all_ones_mask =
                        (ec.bit_width == 64) ? ULLONG_MAX
                                             : (1ULL << ec.bit_width) - 1;
                    ec.is_all_ones = (num_val == all_ones_mask);

                } else if (ec.bit_width == 0) {
                    return std::nullopt;
                }
                if (ec.bit_width == 1 && num_val == 1)
                    ec.is_all_ones = true;
                if (ec.bit_width == 1 && num_val == 0)
                    ec.is_zero = true;
                return ec;

            } else {
                if (s.find_first_not_of("0123456789") != std::string::npos ||
                    s.empty())
                    return std::nullopt;
                ec.value = std::stoll(s);
                ec.is_zero = (ec.value == 0);
                if (ec.value == 1) {
                    ec.bit_width = 1;
                    ec.is_all_ones = true;

                } else if (ec.value == 0) {
                    ec.bit_width = 1;

                } else {
                    ec.bit_width = 32;
                }
                return ec;
            }

        } catch (const std::exception &e) {
            return std::nullopt;
        }
        return std::nullopt;
    }
};

// Structure to hold details about a parsed expression
struct ExpressionDetail {
    string verilog_expr_str;
    std::set<int> variable_ids;
    std::optional<bool> const_expr_evaluates_to_nonzero;
};

// Structure to hold internal information about each constraint
struct ConstraintInternalInfo {
    string verilog_expression_body;
    std::set<int> variable_ids;
    string assigned_wire_name;
    int original_json_constraint_index; // For DSU, refers to index in all_constraints_info for effective constraints
    int original_overall_idx; // Index in the initial all_constraints_info before filtering
    std::optional<bool> determined_wire_value;
};

// Forward declaration
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &
        all_divisors_map); // Unchanged, but its return will be used differently

// Recursive function to parse JSON expression tree and collect details
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map) {
    ExpressionDetail current_detail;
    string type = node["op"];
    current_detail.const_expr_evaluates_to_nonzero = std::nullopt;

    if (type == "VAR") {
        int id = node.value("id", 0);
        current_detail.verilog_expr_str = "var_" + to_string(id);
        current_detail.variable_ids.insert(id);

    } else if (type == "CONST") {
        current_detail.verilog_expr_str = node.value("value", "1'b0");
        std::optional<EvaluatedConstant> ec =
            EvaluatedConstant::from_verilog_string(
                current_detail.verilog_expr_str);
        if (ec.has_value()) {
            current_detail.const_expr_evaluates_to_nonzero = !ec->is_zero;
        }

    } else if (type == "BIT_NEG" || type == "LOG_NEG" || type == "MINUS") {
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map);
        current_detail.variable_ids = lhs_detail.variable_ids;
        string op_symbol_default;
        if (type == "BIT_NEG")
            op_symbol_default = "~";
        else if (type == "LOG_NEG")
            op_symbol_default = "!";
        else if (type == "MINUS")
            op_symbol_default = "-";

        bool evaluated = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
            std::optional<EvaluatedConstant> ec_lhs =
                EvaluatedConstant::from_verilog_string(
                    lhs_detail.verilog_expr_str);
            if (ec_lhs.has_value()) {
                if (type == "LOG_NEG") {
                    current_detail.const_expr_evaluates_to_nonzero =
                        ec_lhs->is_zero;
                    current_detail.verilog_expr_str =
                        (ec_lhs->is_zero) ? "1'b1" : "1'b0";
                    evaluated = true;
                }
            }
        }
        if (!evaluated) {
            current_detail.verilog_expr_str =
                op_symbol_default + "(" + lhs_detail.verilog_expr_str + ")";
            current_detail.const_expr_evaluates_to_nonzero = std::nullopt;
        }

    } else { // Binary operators
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map);
        ExpressionDetail rhs_detail =
            get_expression_details(node["rhs_expression"], all_divisors_map);
        current_detail.variable_ids = lhs_detail.variable_ids;
        current_detail.variable_ids.insert(rhs_detail.variable_ids.begin(),
                                           rhs_detail.variable_ids.end());

        if (type == "DIV" || type == "MOD") {
            if (all_divisors_map.find(rhs_detail.verilog_expr_str) ==
                all_divisors_map.end()) {
                bool rhs_is_const_nonzero =
                    rhs_detail.const_expr_evaluates_to_nonzero.has_value() &&
                    rhs_detail.const_expr_evaluates_to_nonzero.value();
                if (!rhs_is_const_nonzero) {
                    all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
                }
            }
        }

        bool evaluated_binary_const = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value() &&
            rhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
            std::optional<EvaluatedConstant> ec_lhs =
                EvaluatedConstant::from_verilog_string(
                    lhs_detail.verilog_expr_str);
            std::optional<EvaluatedConstant> ec_rhs =
                EvaluatedConstant::from_verilog_string(
                    rhs_detail.verilog_expr_str);
            if (ec_lhs.has_value() && ec_rhs.has_value()) {
                bool result_is_nonzero = false;
                bool evaluable = true;
                if (type == "E"
                            "Q")
                    result_is_nonzero = (ec_lhs->value == ec_rhs->value);
                else if (type == "NE"
                                 "Q")
                    result_is_nonzero = (ec_lhs->value != ec_rhs->value);
                else if (type == "LOG_AND")
                    result_is_nonzero = (!ec_lhs->is_zero && !ec_rhs->is_zero);
                else if (type == "LOG_OR")
                    result_is_nonzero = (!ec_lhs->is_zero || !ec_rhs->is_zero);
                else {
                    evaluable = false;
                }
                if (evaluable) {
                    current_detail.const_expr_evaluates_to_nonzero =
                        result_is_nonzero;
                    current_detail.verilog_expr_str =
                        result_is_nonzero ? "1'b1" : "1'b0";
                    evaluated_binary_const = true;
                }
            }
        }
        if (!evaluated_binary_const) {
            current_detail.const_expr_evaluates_to_nonzero = std::nullopt;
            string op_symbol;
            if (type == "IMPLY") {
                current_detail.verilog_expr_str =
                    "( (!(" + lhs_detail.verilog_expr_str + ")) || (" +
                    rhs_detail.verilog_expr_str + ") )";

            } else {
                if (type == "ADD")
                    op_symbol = "+";
                else if (type == "SUB")
                    op_symbol = "-";
                else if (type == "MUL")
                    op_symbol = "*";
                else if (type == "DIV")
                    op_symbol = "/";
                else if (type == "MOD")
                    op_symbol = "%";
                else if (type == "LSHIF"
                                 "T")
                    op_symbol = "<<";
                else if (type == "RSHIF"
                                 "T")
                    op_symbol = ">>";
                else if (type == "BIT_AND")
                    op_symbol = "&";
                else if (type == "BIT_OR")
                    op_symbol = "|";
                else if (type == "BIT_XOR")
                    op_symbol = "^";
                else if (type == "LOG_AND")
                    op_symbol = "&&";
                else if (type == "LOG_OR")
                    op_symbol = "||";
                else if (type == "EQ")
                    op_symbol = "==";
                else if (type == "NEQ")
                    op_symbol = "!=";
                else {
                    cerr << "Warning: Unhandled binary 'op' in string "
                            "gen: "

                         << type << endl;
                    current_detail.verilog_expr_str =
                        "/* UNHANDLED_OP_STR: " + type + " */";
                    return current_detail;
                }
                current_detail.verilog_expr_str =
                    "(" + lhs_detail.verilog_expr_str + " " + op_symbol + " " +
                    rhs_detail.verilog_expr_str + ")";
            }
        }
    }
    return current_detail;
}

struct DSUComponentForOrdering {
    int root_representative_idx; // DSU根在 effective_constraints_for_result 中的索引
    size_t num_constraints;
    // 该组件包含的有效约束数量
    size_t total_pi_support_size; // 该组件所有约束的PI支持集并集的大小
    size_t size_metric;           // 添加 size_metric 成员
    std::vector<int>
        effective_constraint_indices_in_component; // 该组件包含的约束在 effective_constraints_for_result 中的索引列表（保持原始相对顺序）

    // 为了稳定排序，如果主要指标相同，可以使用 root_representative_idx
};

// sort_strategy: 0=从小到大(按约束数), 1=从大到小(按约束数), 2=大的在中间, 3=大的在两边
static std::vector<std::string> get_ordered_wires_by_dsu_strategy(
    const std::vector<ConstraintInternalInfo> &effective_constraints,
    int sort_strategy) {

    std::vector<std::string> final_ordered_wires;
    if (effective_constraints.empty()) {
        return final_ordered_wires;
    }

    int num_effective = effective_constraints.size();
    std::vector<int> dsu_parent(num_effective);
    std::iota(dsu_parent.begin(), dsu_parent.end(), 0);

    std::function<int(int)> find_set = [&](int i) -> int {
        if (dsu_parent[i] == i)
            return i;
        return dsu_parent[i] = find_set(dsu_parent[i]);
    };
    std::function<void(int, int)> unite_sets = [&](int i, int j) {
        i = find_set(i);
        j = find_set(j);
        if (i != j)
            dsu_parent[j] = i;
    };

    std::map<int, std::vector<int>> var_to_effective_idx_map;
    for (int i = 0; i < num_effective; ++i) {
        for (int var_id : effective_constraints[i].variable_ids) {
            var_to_effective_idx_map[var_id].push_back(i);
        }
    }
    for (const auto &pair_var_indices : var_to_effective_idx_map) {
        const std::vector<int> &indices = pair_var_indices.second;
        if (indices.size() > 1) {
            for (size_t i = 0; i < indices.size() - 1; ++i) {
                unite_sets(indices[i], indices[i + 1]);
            }
        }
    }

    std::map<int, std::vector<int>> dsu_root_to_effective_indices;
    for (int i = 0; i < num_effective; ++i) {
        dsu_root_to_effective_indices[find_set(i)].push_back(i);
    }

    std::vector<DSUComponentForOrdering> component_list;
    for (const auto &pair_entry : dsu_root_to_effective_indices) {
        DSUComponentForOrdering comp;
        comp.root_representative_idx =
            pair_entry.first; // Root's own index in effective_constraints
        comp.effective_constraint_indices_in_component =
            pair_entry.second; // Indices within effective_constraints
        comp.num_constraints =
            comp.effective_constraint_indices_in_component.size();

        std::set<int> component_pis;
        for (int eff_idx : comp.effective_constraint_indices_in_component) {
            component_pis.insert(
                effective_constraints[eff_idx].variable_ids.begin(),
                effective_constraints[eff_idx].variable_ids.end());
        }
        comp.total_pi_support_size = component_pis.size();
        // 使用 num_constraints 作为主要的 size_metric
        // 您也可以改为 comp.total_pi_support_size
        comp.size_metric = comp.num_constraints;

        component_list.push_back(comp);
    }

    // 根据 sort_strategy 对 component_list 排序
    if (sort_strategy == 0) { // 从小到大 (by num_constraints)
        std::sort(component_list.begin(), component_list.end(),
                  [](const DSUComponentForOrdering &a,
                     const DSUComponentForOrdering &b) {
                      if (a.size_metric != b.size_metric)
                          return a.size_metric < b.size_metric;
                      return a.root_representative_idx <
                             b.root_representative_idx; // Stable sort
                  });

    } else if (sort_strategy == 1) { // 从大到小 (by num_constraints)
        std::sort(component_list.begin(), component_list.end(),
                  [](const DSUComponentForOrdering &a,
                     const DSUComponentForOrdering &b) {
                      if (a.size_metric != b.size_metric)
                          return a.size_metric > b.size_metric;
                      return a.root_representative_idx <
                             b.root_representative_idx; // Stable sort
                  });

    } else if (sort_strategy == 2 ||
               sort_strategy == 3) { // 大的在中间 或 大的在两边
                                     // 先按大小升序排序，作为基础
        std::sort(component_list.begin(), component_list.end(),
                  [](const DSUComponentForOrdering &a,
                     const DSUComponentForOrdering &b) {
                      if (a.size_metric != b.size_metric)
                          return a.size_metric < b.size_metric;
                      return a.root_representative_idx <
                             b.root_representative_idx;
                  });

        if (component_list.size() >
            2) { // 需要至少3个组件才能有明确的中间和两边
            std::vector<DSUComponentForOrdering> reordered_component_list;
            size_t n_comps = component_list.size();
            size_t count1 = n_comps / 3;
            size_t count3 = n_comps / 3;
            size_t count2 = n_comps - count1 - count3;
            if (n_comps % 3 == 1 && count2 > 0) {
                count2++;

            } else if (n_comps % 3 == 2 && count2 > 0) {
                count2++;
                if (count1 > 0)
                    count1++;
                else
                    count3++;
            }

            std::vector<DSUComponentForOrdering> group_small, group_medium,
                group_large;
            size_t current_idx = 0;
            for (size_t i = 0; i < count1 && current_idx < n_comps; ++i)
                group_small.push_back(component_list[current_idx++]);
            for (size_t i = 0; i < count2 && current_idx < n_comps; ++i)
                group_medium.push_back(component_list[current_idx++]);
            for (size_t i = 0; i < count3 && current_idx < n_comps; ++i)
                group_large.push_back(component_list[current_idx++]);

            if (sort_strategy == 2) { // 大的在中间: Small + Large + Medium
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_small.begin(),
                                                group_small.end());
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_large.begin(),
                                                group_large.end());
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_medium.begin(),
                                                group_medium.end());

            } else { // sort_strategy == 3, 大的在两边: Large + Small + Medium
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_large.begin(),
                                                group_large.end());
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_small.begin(),
                                                group_small.end());
                reordered_component_list.insert(reordered_component_list.end(),
                                                group_medium.begin(),
                                                group_medium.end());
            }
            if (reordered_component_list.size() ==
                n_comps) { // Ensure all components were re-added
                component_list = reordered_component_list;

            } else {
                cerr << "Warning: Component reordering for "
                        "strategy "
                     << sort_strategy
                     << " resulted in mismatched component count. Using "
                        "ascending size order."
                     << endl;
                // Fallback to simple ascending order already done by initial sort
            }
        }
        // If n_comps <= 2, the initial ascending sort is fine, or could be specifically handled.
    }
    // Default: if no specific sort_strategy matched or list too small, it might use map iteration order or simple sort.
    // For strategies 0 and 1, component_list is already sorted.

    for (const auto &comp_info : component_list) {
        for (int effective_idx :
             comp_info.effective_constraint_indices_in_component) {
            final_ordered_wires.push_back(
                effective_constraints[effective_idx].assigned_wire_name);
        }
    }
    return final_ordered_wires;
}

// Core module: JSON 到 Verilog 转换
int json_v_converter(const string &input_json_path,
                     const string &output_v_dir) {
    json data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open()) { /* ... */
        return 1;
    }
    try {
        input_json_stream >> data;

    } catch (const json::parse_error &e) { /* ... */
        input_json_stream.close();
        return 1;
    }
    input_json_stream.close();
    if (!data.contains("variable_list") || !data["variable_list"].is_array() ||
        !data.contains("constraint_list") ||
        !data["constraint_list"].is_array()) {
        /* ... */ return 1;
    }

    vector<string> v_lines;
    json variable_list = data["variable_list"];
    json constraint_list_json = data["constraint_list"];

    v_lines.push_back("module from_json(");
    for (const auto &var : variable_list) {
        if (!var.contains("name") || !var["name"].is_string() ||
            !var.contains("bit_width") || !var["bit_width"].is_number()) {
            cerr << "Warning: Skipping malformed variable entry." << endl;
            continue;
        }
        string name = var.value("name", "default_name");
        int bit_width = var.value("bit_width", 1);
        v_lines.push_back("    input wire [" + to_string(bit_width - 1) +
                          ":0] " + name + ",");
    }
    v_lines.push_back("    output wire result");
    v_lines.push_back(");");

    std::vector<ConstraintInternalInfo>
        all_constraints_info_orig; // Keep original full list
    std::map<std::string, ExpressionDetail> all_found_divisors_map;
    int current_idx_counter = 0;

    for (size_t i = 0; i < constraint_list_json.size(); ++i) {
        const auto &cnstr_json_node = constraint_list_json[i];
        ExpressionDetail detail =
            get_expression_details(cnstr_json_node, all_found_divisors_map);
        ConstraintInternalInfo info;
        info.verilog_expression_body = detail.verilog_expr_str;
        info.variable_ids = detail.variable_ids;
        info.assigned_wire_name =
            "cnstr" + to_string(current_idx_counter) + "_redor";
        info.original_json_constraint_index = static_cast<int>(
            i); // This relative to original constraint_list_json
        info.original_overall_idx = current_idx_counter;

        if (detail.const_expr_evaluates_to_nonzero.has_value()) {
            info.determined_wire_value =
                detail.const_expr_evaluates_to_nonzero.value();

        } else {
            info.determined_wire_value = std::nullopt;
        }
        all_constraints_info_orig.push_back(info);
        current_idx_counter++;
    }

    for (const auto &pair_str_detail : all_found_divisors_map) {
        const ExpressionDetail &div_detail = pair_str_detail.second;
        ConstraintInternalInfo info;
        string divisor_expr_str = div_detail.verilog_expr_str;
        info.verilog_expression_body = "(" + divisor_expr_str + " != 0)";
        info.variable_ids = div_detail.variable_ids;
        info.assigned_wire_name =
            "cnstr" + to_string(current_idx_counter) + "_redor";
        info.original_json_constraint_index = -1;
        info.original_overall_idx = current_idx_counter;

        if (div_detail.const_expr_evaluates_to_nonzero.has_value()) {
            info.determined_wire_value =
                div_detail.const_expr_evaluates_to_nonzero.value();

        } else {
            info.determined_wire_value = std::nullopt;
        }
        all_constraints_info_orig.push_back(info);
        current_idx_counter++;
    }

    bool result_is_const_zero = false;
    for (const auto &info : all_constraints_info_orig) {
        if (info.determined_wire_value.has_value() &&
            !info.determined_wire_value.value()) {
            result_is_const_zero = true;
            cout << "Optimization: Constraint wire '" << info.assigned_wire_name
                 << "' evaluates to 1'b0. Overall result is 1'b0." << endl;
            break;
        }
    }

    for (const auto &info : all_constraints_info_orig) {
        v_lines.push_back("    wire " + info.assigned_wire_name + ";");
        if (info.determined_wire_value.has_value()) {
            v_lines.push_back(
                "    assign " + info.assigned_wire_name + " = " +
                (info.determined_wire_value.value() ? "1'b1;" : "1'b0;"));

        } else {
            v_lines.push_back("    assign " + info.assigned_wire_name +
                              " = |(" + info.verilog_expression_body + ");");
        }
    }

    std::vector<ConstraintInternalInfo>
        effective_constraints_for_result; // Only non-const-true constraints
    if (!result_is_const_zero) {
        for (const auto &info : all_constraints_info_orig) {
            if (!(info.determined_wire_value.has_value() &&
                  info.determined_wire_value.value())) {
                effective_constraints_for_result.push_back(info);

            } else {
                cout << "Optimization: Constraint wire '"
                     << info.assigned_wire_name
                     << "' evaluates to 1'b1, omitted from result AND chain."
                     << endl;
            }
        }
    }

    std::vector<std::string> final_ordered_result_wire_names;
    if (!result_is_const_zero && !effective_constraints_for_result.empty()) {
        // CHOOSE YOUR SORTING STRATEGY HERE by changing the integer argument:
        // 0: DSU components sorted by size (num constraints) ascending
        // 1: DSU components sorted by size (num constraints) descending
        // 2: DSU components: Smallest 1/3rd, then Largest 1/3rd, then Medium 1/3rd (Large in Middle)
        // 3: DSU components: Largest 1/3rd, then Smallest 1/3rd, then Medium 1/3rd (Large at Ends)
        // Default (e.g., if an invalid strategy number is given, or for simple DSU natural order):
        //    Iterate dsu_root_to_effective_indices from its map directly.
        //    For now, let's implement one specific strategy, e.g., 0 (smallest DSU component first).
        //    You can change the '0' to test other strategies.
        int chosen_strategy =
            3; // <<< --- CHANGE THIS VALUE TO TEST DIFFERENT STRATEGIES (0, 1, 2, or 3)
        cout << "LOG: Using DSU component ordering strategy for "
                "'result' line: "
             << chosen_strategy << endl;
        final_ordered_result_wire_names = get_ordered_wires_by_dsu_strategy(
            effective_constraints_for_result, chosen_strategy);
    }

    string result_assign = "    assign result = ";
    if (result_is_const_zero) {
        result_assign += "1'b0;";

    } else if (final_ordered_result_wire_names.empty()) {
        result_assign +=
            "1'b1;"; // All effective constraints were const 1'b1 or no effective constraints

    } else {
        for (size_t i = 0; i < final_ordered_result_wire_names.size(); ++i) {
            result_assign += final_ordered_result_wire_names[i];
            result_assign +=
                (i == final_ordered_result_wire_names.size() - 1) ? ";" : " & ";
        }
    }
    v_lines.push_back(result_assign);
    v_lines.push_back("endmodule");

    // --- File Naming and Writing (Verilog only) ---
    filesystem::path input_json_path_obj(input_json_path);
    string filename_no_ext = input_json_path_obj.stem().string();
    string parent_dir_name =
        input_json_path_obj.parent_path().filename().string();
    string test_id;
    if (!parent_dir_name.empty() && parent_dir_name != "." &&
        parent_dir_name != "/" && parent_dir_name != "..") {
        test_id = parent_dir_name + "_" + filename_no_ext;

    } else {
        test_id = filename_no_ext;
    }
    filesystem::path output_v_path =
        filesystem::path(output_v_dir) / (test_id + ".v");
    ofstream output_v_stream(output_v_path);
    if (!output_v_stream.is_open()) {
        cerr << "Error: Could not open output Verilog file: " << output_v_path
             << endl;
        return 1;
    }
    for (const string &line : v_lines) {
        output_v_stream << line << endl;
    }
    output_v_stream.close();
    cout << "Verilog file generated successfully: " << output_v_path << endl;

    return 0;
}
