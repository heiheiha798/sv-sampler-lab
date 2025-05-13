#include "nlohmann/json.hpp"
#include "solver_functions.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <numeric>
#include <algorithm>
#include <optional>
#include <climits>

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

struct DSUComponentForVerilogOrder {
    size_t size_metric;
    std::vector<std::string> wire_names_in_component;
    int representative_dsu_root_idx;
};

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
                    if (val_str.find_first_not_of("01xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 2);
                } else if (type_char == 'h' || type_char == 'H') {
                    if (val_str.find_first_not_of(
                            "0123456789abcdefABCDEFxzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 16);
                } else if (type_char == 'd' || type_char == 'D') {
                    if (val_str.find_first_not_of("0123456789xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 10);
                } else if (type_char == 'o' || type_char == 'O') {
                    if (val_str.find_first_not_of("01234567xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 8);
                } else
                    return std::nullopt;
                ec.value = static_cast<long long>(num_val);
                ec.is_zero = (num_val == 0);
                if (ec.bit_width > 0 && ec.bit_width <= 64) {
                    unsigned long long all_ones_mask =
                        (ec.bit_width == 64) ? ULLONG_MAX
                                             : (1ULL << ec.bit_width) - 1;
                    ec.is_all_ones = (num_val == all_ones_mask);
                } else if (ec.bit_width == 0)
                    return std::nullopt;
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
                } else
                    ec.bit_width = 32;
                return ec;
            }
        } catch (const std::exception &e) {
            return std::nullopt;
        }
        return std::nullopt;
    }
};

struct ExpressionDetail {
    string verilog_expr_str;
    std::set<int> variable_ids;
    std::optional<bool> const_expr_evaluates_to_nonzero;
};

struct ConstraintInternalInfo {
    string verilog_expression_body;
    std::set<int> variable_ids;
    string assigned_wire_name;
    int original_json_constraint_index;
    int original_overall_idx;
    std::optional<bool> determined_wire_value;
};

static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map);

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
        if (ec.has_value())
            current_detail.const_expr_evaluates_to_nonzero = !ec->is_zero;
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
    } else {
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
                if (!rhs_is_const_nonzero)
                    all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
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
                if (type == "EQ")
                    result_is_nonzero = (ec_lhs->value == ec_rhs->value);
                else if (type == "NEQ")
                    result_is_nonzero = (ec_lhs->value != ec_rhs->value);
                else if (type == "LOG_AND")
                    result_is_nonzero = (!ec_lhs->is_zero && !ec_rhs->is_zero);
                else if (type == "LOG_OR")
                    result_is_nonzero = (!ec_lhs->is_zero || !ec_rhs->is_zero);
                else
                    evaluable = false;
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
                else if (type == "LSHIFT")
                    op_symbol = "<<";
                else if (type == "RSHIFT")
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
    int root_representative_idx;
    size_t num_constraints;
    size_t total_pi_support_size;
    size_t size_metric;
    std::vector<int> effective_constraint_indices_in_component;
};

static std::vector<std::string> get_ordered_wires_by_dsu_strategy(
    const std::vector<ConstraintInternalInfo> &effective_constraints) {
    std::vector<std::string> final_ordered_wires;
    if (effective_constraints.empty())
        return final_ordered_wires;
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
    for (int i = 0; i < num_effective; ++i)
        for (int var_id : effective_constraints[i].variable_ids)
            var_to_effective_idx_map[var_id].push_back(i);
    for (const auto &pair_var_indices : var_to_effective_idx_map) {
        const std::vector<int> &indices = pair_var_indices.second;
        if (indices.size() > 1)
            for (size_t i = 0; i < indices.size() - 1; ++i)
                unite_sets(indices[i], indices[i + 1]);
    }
    std::map<int, std::vector<int>> dsu_root_to_effective_indices;
    for (int i = 0; i < num_effective; ++i)
        dsu_root_to_effective_indices[find_set(i)].push_back(i);
    std::vector<DSUComponentForOrdering> component_list;
    for (const auto &pair_entry : dsu_root_to_effective_indices) {
        DSUComponentForOrdering comp;
        comp.root_representative_idx = pair_entry.first;
        comp.effective_constraint_indices_in_component = pair_entry.second;
        comp.num_constraints =
            comp.effective_constraint_indices_in_component.size();
        std::set<int> component_pis;
        for (int eff_idx : comp.effective_constraint_indices_in_component) {
            component_pis.insert(
                effective_constraints[eff_idx].variable_ids.begin(),
                effective_constraints[eff_idx].variable_ids.end());
        }
        comp.total_pi_support_size = component_pis.size();
        comp.size_metric = comp.num_constraints;
        component_list.push_back(comp);
    }
    std::sort(
        component_list.begin(), component_list.end(),
        [](const DSUComponentForOrdering &a, const DSUComponentForOrdering &b) {
            if (a.size_metric != b.size_metric)
                return a.size_metric < b.size_metric;
            return a.root_representative_idx < b.root_representative_idx;
        });
    for (const auto &comp_info : component_list)
        for (int effective_idx :
             comp_info.effective_constraint_indices_in_component)
            final_ordered_wires.push_back(
                effective_constraints[effective_idx].assigned_wire_name);
    return final_ordered_wires;
}

int json_v_converter(const string &input_json_path,
                     const string &output_v_dir) {
    json data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open())
        return 1;
    try {
        input_json_stream >> data;
    } catch (const json::parse_error &e) {
        input_json_stream.close();
        return 1;
    }
    input_json_stream.close();
    if (!data.contains("variable_list") || !data["variable_list"].is_array() ||
        !data.contains("constraint_list") ||
        !data["constraint_list"].is_array())
        return 1;
    vector<string> v_lines;
    json variable_list = data["variable_list"];
    json constraint_list_json = data["constraint_list"];
    v_lines.push_back("module from_json(");
    for (const auto &var : variable_list) {
        if (!var.contains("name") || !var["name"].is_string() ||
            !var.contains("bit_width") || !var["bit_width"].is_number())
            continue;
        string name = var.value("name", "default_name");
        int bit_width = var.value("bit_width", 1);
        v_lines.push_back("     input wire [" + to_string(bit_width - 1) +
                          ":0] " + name + ",");
    }
    v_lines.push_back("     output wire result");
    v_lines.push_back(");");
    std::vector<ConstraintInternalInfo> all_constraints_info_orig;
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
        info.original_json_constraint_index = static_cast<int>(i);
        info.original_overall_idx = current_idx_counter;
        if (detail.const_expr_evaluates_to_nonzero.has_value())
            info.determined_wire_value =
                detail.const_expr_evaluates_to_nonzero.value();
        else
            info.determined_wire_value = std::nullopt;
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
        if (div_detail.const_expr_evaluates_to_nonzero.has_value())
            info.determined_wire_value =
                div_detail.const_expr_evaluates_to_nonzero.value();
        else
            info.determined_wire_value = std::nullopt;
        all_constraints_info_orig.push_back(info);
        current_idx_counter++;
    }
    bool result_is_const_zero = false;
    for (const auto &info : all_constraints_info_orig)
        if (info.determined_wire_value.has_value() &&
            !info.determined_wire_value.value()) {
            result_is_const_zero = true;
            break;
        }
    for (const auto &info : all_constraints_info_orig) {
        v_lines.push_back("     wire " + info.assigned_wire_name + ";");
        if (info.determined_wire_value.has_value())
            v_lines.push_back(
                "     assign " + info.assigned_wire_name + " = " +
                (info.determined_wire_value.value() ? "1'b1;" : "1'b0;"));
        else
            v_lines.push_back("     assign " + info.assigned_wire_name +
                              " = |(" + info.verilog_expression_body + ");");
    }
    std::vector<ConstraintInternalInfo> effective_constraints_for_result;
    if (!result_is_const_zero)
        for (const auto &info : all_constraints_info_orig)
            if (!(info.determined_wire_value.has_value() &&
                  info.determined_wire_value.value()))
                effective_constraints_for_result.push_back(info);
    std::vector<std::string> final_ordered_result_wire_names;
    if (!result_is_const_zero && !effective_constraints_for_result.empty())
        final_ordered_result_wire_names =
            get_ordered_wires_by_dsu_strategy(effective_constraints_for_result);
    string result_assign = "     assign result = ";
    if (result_is_const_zero)
        result_assign += "1'b0;";
    else if (final_ordered_result_wire_names.empty())
        result_assign += "1'b1;";
    else
        for (size_t i = 0; i < final_ordered_result_wire_names.size(); ++i) {
            result_assign += final_ordered_result_wire_names[i];
            result_assign +=
                (i == final_ordered_result_wire_names.size() - 1) ? ";" : " & ";
        }
    v_lines.push_back(result_assign);
    v_lines.push_back("endmodule");
    filesystem::path input_json_path_obj(input_json_path);
    string filename_no_ext = input_json_path_obj.stem().string();
    string parent_dir_name =
        input_json_path_obj.parent_path().filename().string();
    string test_id;
    if (!parent_dir_name.empty() && parent_dir_name != "." &&
        parent_dir_name != "/" && parent_dir_name != "..")
        test_id = parent_dir_name + "_" + filename_no_ext;
    else
        test_id = filename_no_ext;
    filesystem::path output_v_path =
        filesystem::path(output_v_dir) / (test_id + ".v");
    ofstream output_v_stream(output_v_path);
    if (!output_v_stream.is_open())
        return 1;
    for (const string &line : v_lines)
        output_v_stream << line << endl;
    output_v_stream.close();
    return 0;
}