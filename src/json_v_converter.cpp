#include "nlohmann/json.hpp"
#include "solver_functions.h" // 假设 solver_functions.h 包含必要的声明
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <numeric> // For std::iota
#include <algorithm> // For std::sort, std::unique (set_union might not be needed here)

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

// Structure to hold details about a parsed expression
struct ExpressionDetail {
    string verilog_expr_str;
    std::set<int> variable_ids;
};

// Structure to hold internal information about each constraint
struct ConstraintInternalInfo {
    string verilog_expression_body;
    std::set<int> variable_ids;
    string assigned_wire_name;
    int original_json_constraint_index;
};

// Forward declaration
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map);

// Recursive function to parse JSON expression tree and collect details
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map) {
    ExpressionDetail current_detail;
    string type = node["op"];

    if (type == "VAR") {
        int id = node.value("id", 0);
        current_detail.verilog_expr_str = "var_" + to_string(id);
        current_detail.variable_ids.insert(id);
    } else if (type == "CONST") {
        current_detail.verilog_expr_str = node.value("value", "1'b0");
    } else if (type == "BIT_NEG" || type == "LOG_NEG" || type == "MINUS") {
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map);
        current_detail.variable_ids = lhs_detail.variable_ids;

        string op_symbol;
        if (type == "BIT_NEG")
            op_symbol = "~";
        else if (type == "LOG_NEG")
            op_symbol = "!";
        else if (type == "MINUS")
            op_symbol = "-";
        current_detail.verilog_expr_str =
            op_symbol + "(" + lhs_detail.verilog_expr_str + ")";
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
                all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
            }
        }

        if (type == "IMPLY") {
            current_detail.verilog_expr_str =
                "( (!(" + lhs_detail.verilog_expr_str + ")) || (" +
                rhs_detail.verilog_expr_str + ") )";
        } else {
            string op_symbol;
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
                cerr << "Warning: Unhandled or unknown binary 'op' type "
                        "encountered in get_expression_details: "
                     << type << endl;
                current_detail.verilog_expr_str =
                    "/* UNHANDLED_BINARY_OP: " + type + " */";
                return current_detail;
            }
            current_detail.verilog_expr_str =
                "(" + lhs_detail.verilog_expr_str + " " + op_symbol + " " +
                rhs_detail.verilog_expr_str + ")";
        }
    }
    return current_detail;
}

// Core module: JSON 到 Verilog 转换
int json_v_converter(const string &input_json_path,
                     const string &output_v_dir) {
    json data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open()) {
        cerr << "Error: Could not open input JSON file: " << input_json_path
             << endl;
        return 1;
    }
    try {
        input_json_stream >> data;
    } catch (const json::parse_error &e) {
        cerr << "Error: JSON parse error: " << e.what() << " in file "
             << input_json_path << endl;
        input_json_stream.close();
        return 1;
    }
    input_json_stream.close();

    if (!data.contains("variable_list") || !data["variable_list"].is_array() ||
        !data.contains("constraint_list") ||
        !data["constraint_list"].is_array()) {
        cerr << "Error: Invalid JSON format. Missing 'variable_list' or "
                "'constraint_list'."
             << endl;
        return 1;
    }

    vector<string> v_lines;
    json variable_list = data["variable_list"];
    json constraint_list_json = data["constraint_list"];

    v_lines.push_back("module from_json(");
    for (const auto &var : variable_list) {
        if (!var.contains("name") || !var["name"].is_string() ||
            !var.contains("bit_width") || !var["bit_width"].is_number()) {
            cerr << "Warning: Skipping malformed variable entry in JSON."
                 << endl;
            continue;
        }
        string name = var.value("name", "default_name");
        int bit_width = var.value("bit_width", 1);
        v_lines.push_back("    input wire [" + to_string(bit_width - 1) +
                          ":0] " + name + ",");
    }
    v_lines.push_back("    output wire result");
    v_lines.push_back(");");

    std::vector<ConstraintInternalInfo> all_constraints_info;
    std::map<std::string, ExpressionDetail> all_found_divisors_map;
    int current_constraint_verilog_idx = 0;

    for (size_t i = 0; i < constraint_list_json.size(); ++i) {
        const auto &cnstr_json_node = constraint_list_json[i];
        ExpressionDetail detail =
            get_expression_details(cnstr_json_node, all_found_divisors_map);
        ConstraintInternalInfo info;
        info.verilog_expression_body = detail.verilog_expr_str;
        info.variable_ids = detail.variable_ids;
        info.assigned_wire_name =
            "cnstr" + to_string(current_constraint_verilog_idx) + "_redor";
        info.original_json_constraint_index = static_cast<int>(i);
        all_constraints_info.push_back(info);
        current_constraint_verilog_idx++;
    }

    for (const auto &pair_str_detail : all_found_divisors_map) {
        const ExpressionDetail &div_detail = pair_str_detail.second;
        ConstraintInternalInfo info;
        info.verilog_expression_body =
            "(" + div_detail.verilog_expr_str + " != 0)";
        info.variable_ids = div_detail.variable_ids;
        info.assigned_wire_name =
            "cnstr" + to_string(current_constraint_verilog_idx) + "_redor";
        info.original_json_constraint_index = -1;
        all_constraints_info.push_back(info);
        current_constraint_verilog_idx++;
    }

    for (const auto &info : all_constraints_info) {
        v_lines.push_back("    wire " + info.assigned_wire_name + ";");
        v_lines.push_back("    assign " + info.assigned_wire_name + " = |(" +
                          info.verilog_expression_body + ");");
    }

    std::map<int, std::vector<int>> dsu_root_to_internal_indices_map;
    if (!all_constraints_info.empty()) {
        int num_effective_constraints = all_constraints_info.size();
        std::vector<int> dsu_parent(num_effective_constraints);
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

        std::map<int, std::vector<int>> var_to_constraint_indices_map;
        for (int i = 0; i < num_effective_constraints; ++i) {
            for (int var_id : all_constraints_info[i].variable_ids) {
                var_to_constraint_indices_map[var_id].push_back(i);
            }
        }

        for (const auto &pair_var_cnstrs_indices :
             var_to_constraint_indices_map) {
            const std::vector<int> &cnstr_indices =
                pair_var_cnstrs_indices.second;
            if (cnstr_indices.size() > 1) {
                for (size_t i = 0; i < cnstr_indices.size() - 1; ++i) {
                    unite_sets(cnstr_indices[i], cnstr_indices[i + 1]);
                }
            }
        }
        for (int i = 0; i < num_effective_constraints; ++i) {
            dsu_root_to_internal_indices_map[find_set(i)].push_back(i);
        }
    }

    std::vector<std::string> final_ordered_result_wire_names;
    if (!all_constraints_info.empty()) {
        // Iterate dsu_root_to_internal_indices_map directly.
        // The order of components will be std::map's iteration order (typically by key, i.e., DSU root index).
        // The order of wires within components will be their order in the map's std::vector<int> values.
        for (const auto &pair_root_indices : dsu_root_to_internal_indices_map) {
            for (int internal_idx : pair_root_indices.second) {
                if (internal_idx >= 0 && static_cast<size_t>(internal_idx) <
                                             all_constraints_info.size()) {
                    final_ordered_result_wire_names.push_back(
                        all_constraints_info[internal_idx].assigned_wire_name);
                } else {
                    cerr << "Warning: Invalid internal_idx " << internal_idx
                         << " from DSU map while populating "
                            "final_ordered_result_wire_names."
                         << endl;
                }
            }
        }

        // Sanity check: DSU should cover all constraints.
        if (final_ordered_result_wire_names.size() !=
            all_constraints_info.size()) {
            cerr << "Critical Warning: The number of wires in "
                    "final_ordered_result_wire_names ("
                 << final_ordered_result_wire_names.size()
                 << ") does not match the total number of effective "
                    "constraints ("
                 << all_constraints_info.size()
                 << "). This implies DSU did not cover all constraints for "
                    "result line ordering. "
                 << "The 'result' assignment might be incomplete or incorrect."
                 << endl;
            // To prevent incomplete 'result' line, potentially clear and fallback, or ensure this doesn't happen.
            // If this occurs, it's a logic error in DSU processing or collection.
            // For now, we proceed with what final_ordered_result_wire_names contains.
        }
    }

    // Generate result assignment using final_ordered_result_wire_names
    string result_assign = "    assign result = ";
    if (all_constraints_info.empty()) {
        result_assign += "1'b1;";
    } else if (final_ordered_result_wire_names.empty() &&
               !all_constraints_info.empty()) {
        // This case means constraints existed, but the DSU-based ordering resulted in an empty list.
        // This indicates a problem. Fallback to original sequential order for safety.
        cerr << "Error: final_ordered_result_wire_names is empty despite "
                "constraints existing. "
             << "Defaulting to original sequential order for 'result' "
                "assignment."
             << endl;
        for (size_t i = 0; i < all_constraints_info.size(); ++i) {
            result_assign += all_constraints_info[i].assigned_wire_name;
            result_assign +=
                (i == all_constraints_info.size() - 1) ? ";" : " & ";
        }
    } else if (!final_ordered_result_wire_names.empty()) {
        for (size_t i = 0; i < final_ordered_result_wire_names.size(); ++i) {
            result_assign += final_ordered_result_wire_names[i];
            result_assign +=
                (i == final_ordered_result_wire_names.size() - 1) ? ";" : " & ";
        }
    } else { // Should be covered by the first condition (all_constraints_info.empty())
        result_assign += "1'b1;";
    }
    v_lines.push_back(result_assign);
    v_lines.push_back("endmodule");

    // --- REMOVED: Component JSON generation and writing ---

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