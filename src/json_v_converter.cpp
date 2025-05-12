#include "nlohmann/json.hpp"
#include "solver_functions.h" // For function declarations if any other utilities were there (not strictly needed for this file's content)
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <numeric>   // For std::iota
#include <algorithm> // For std::sort, std::unique

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

// Structure to hold details about a parsed expression
struct ExpressionDetail {
    string verilog_expr_str;
    std::set<int> variable_ids;
    // Unlike the previous plan, we will populate a global map of divisors
    // directly within get_expression_details to simplify unique collection.
};

// Structure to hold internal information about each constraint (original or special)
struct ConstraintInternalInfo {
    string verilog_expression_body; // The part that goes inside |(...)
    std::set<int> variable_ids;
    string assigned_wire_name;
    int original_json_constraint_index; // -1 for special constraints, or index in constraint_list
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
            // Store the divisor detail in the global map if not already present based on its Verilog string
            // This ensures unique divisors are collected across the entire JSON.
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
                return current_detail; // Early exit for unhandled binary op
            }
            current_detail.verilog_expr_str =
                "(" + lhs_detail.verilog_expr_str + " " + op_symbol + " " +
                rhs_detail.verilog_expr_str + ")";
        }
    }
    return current_detail;
}

// Core module: JSON 到 Verilog 转换 and Component Info Generation
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
    std::map<std::string, ExpressionDetail>
        all_found_divisors_map; // To collect unique divisors

    int current_constraint_verilog_idx = 0; // Used for cnstrX_redor naming

    // Process original constraints from constraint_list
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

    // Process special constraints (divisors != 0)
    for (const auto &pair_str_detail : all_found_divisors_map) {
        const ExpressionDetail &div_detail = pair_str_detail.second;
        ConstraintInternalInfo info;
        info.verilog_expression_body =
            "(" + div_detail.verilog_expr_str + " != 0)";
        info.variable_ids =
            div_detail.variable_ids; // Vars from the divisor itself
        info.assigned_wire_name =
            "cnstr" + to_string(current_constraint_verilog_idx) + "_redor";
        info.original_json_constraint_index = -1; // Mark as special
        all_constraints_info.push_back(info);
        current_constraint_verilog_idx++;
    }

    // Generate Verilog wires and assignments for all effective constraints
    for (const auto &info : all_constraints_info) {
        v_lines.push_back("    wire " + info.assigned_wire_name + ";");
        v_lines.push_back("    assign " + info.assigned_wire_name + " = |(" +
                          info.verilog_expression_body + ");");
    }

    // --- Component Identification DSU Logic (used for ordering 'result' and for _components.json) ---
    std::map<int, std::vector<int>>
        dsu_root_to_internal_indices_map; // Populated regardless of all_constraints_info.empty() for consistency
    if (!all_constraints_info.empty()) {
        int num_effective_constraints = all_constraints_info.size();
        std::vector<int> dsu_parent(num_effective_constraints);
        std::iota(dsu_parent.begin(), dsu_parent.end(),
                  0); // Fill with 0, 1, 2, ...

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
        // Populate dsu_root_to_internal_indices_map after DSU operations
        for (int i = 0; i < num_effective_constraints; ++i) {
            dsu_root_to_internal_indices_map[find_set(i)].push_back(i);
        }
    }

    // --- MODIFICATION START: Generate result assignment based on DSU component order ---
    string result_assign = "    assign result = ";
    if (all_constraints_info.empty()) {
        result_assign += "1'b1;"; // No constraints, result is true
    } else {
        std::vector<std::string> ordered_result_wire_names;
        std::set<int> processed_indices; // To track indices already added

        // Iterate through DSU components. The order of components will depend on map iteration.
        // For a more deterministic component order, one could collect map keys (roots), sort them, then iterate.
        // However, for grouping, map iteration order is usually sufficient.
        for (const auto &pair_root_indices : dsu_root_to_internal_indices_map) {
            // pair_root_indices.second is std::vector<int> of internal indices for this component
            for (int internal_idx : pair_root_indices.second) {
                if (internal_idx >= 0 && static_cast<size_t>(internal_idx) <
                                             all_constraints_info.size()) {
                    if (processed_indices.find(internal_idx) ==
                        processed_indices
                            .end()) { // Ensure each wire is added once
                        ordered_result_wire_names.push_back(
                            all_constraints_info[internal_idx]
                                .assigned_wire_name);
                        processed_indices.insert(internal_idx);
                    }
                } else {
                    cerr << "Warning: DSU component provided an out-of-bounds "
                            "internal_idx: "
                         << internal_idx << " while ordering result wires."
                         << endl;
                }
            }
        }

        // Safety check: if DSU somehow didn't cover all constraints (highly unlikely if DSU logic is correct
        // and all_constraints_info maps 0..N-1 to DSU elements), add any remaining ones.
        // This also ensures that if dsu_root_to_internal_indices_map was empty (e.g. no constraints),
        // this loop won't add anything, which is fine.
        if (ordered_result_wire_names.size() != all_constraints_info.size() &&
            !all_constraints_info.empty()) {
            cerr << "Warning: Number of DSU-ordered wires ("
                 << ordered_result_wire_names.size()
                 << ") does not match total constraints ("
                 << all_constraints_info.size()
                 << "). Appending missing wires to result." << endl;
            for (size_t i = 0; i < all_constraints_info.size(); ++i) {
                if (processed_indices.find(static_cast<int>(i)) ==
                    processed_indices.end()) {
                    ordered_result_wire_names.push_back(
                        all_constraints_info[i].assigned_wire_name);
                    // No need to add to processed_indices here as we are just appending at the end.
                }
            }
        }

        if (!ordered_result_wire_names.empty()) {
            for (size_t i = 0; i < ordered_result_wire_names.size(); ++i) {
                result_assign += ordered_result_wire_names[i];
                if (i == ordered_result_wire_names.size() - 1) {
                    result_assign += ";";
                } else {
                    result_assign += " & ";
                }
            }
        } else { // Should only happen if all_constraints_info was also empty
            result_assign += "1'b1;";
        }
    }
    v_lines.push_back(result_assign);
    // --- MODIFICATION END ---
    v_lines.push_back("endmodule");

    // --- Component Identification for _components.json (existing logic) ---
    json components_json_output_data = json::object();
    if (!all_constraints_info.empty()) {
        // DSU related maps (var_to_constraint_indices_map, dsu_root_to_internal_indices_map)
        // are already populated from the DSU logic block above.

        json components_array = json::array();
        int comp_id_counter = 0;
        // Iterate dsu_root_to_internal_indices_map again to create the JSON "components" part
        for (const auto &pair_root_indices : dsu_root_to_internal_indices_map) {
            json component_entry = json::object();
            component_entry["component_id"] = comp_id_counter++;

            json constraint_wire_names_array = json::array();
            json constraint_internal_indices_array =
                json::array(); // Index in all_constraints_info
            std::set<int> component_total_vars;

            for (int internal_idx : pair_root_indices.second) {
                if (internal_idx >= 0 &&
                    static_cast<size_t>(internal_idx) <
                        all_constraints_info.size()) { // Bounds check
                    constraint_wire_names_array.push_back(
                        all_constraints_info[internal_idx].assigned_wire_name);
                    constraint_internal_indices_array.push_back(internal_idx);
                    component_total_vars.insert(
                        all_constraints_info[internal_idx].variable_ids.begin(),
                        all_constraints_info[internal_idx].variable_ids.end());
                } else {
                    cerr << "Warning: DSU component provided an out-of-bounds "
                            "internal_idx: "
                         << internal_idx
                         << " while generating _components.json." << endl;
                }
            }
            component_entry["constraint_wires"] = constraint_wire_names_array;
            component_entry["constraint_internal_indices"] =
                constraint_internal_indices_array;
            component_entry["variables"] =
                component_total_vars; // Set automatically handles uniqueness

            components_array.push_back(component_entry);
        }
        components_json_output_data["components"] = components_array;
    } else {
        components_json_output_data["components"] =
            json::array(); // Empty components array
    }

    // Add the list of wire names that are ANDed together to form the 'result' (as discussed previously for future use)
    json result_operand_wires_json_array = json::array();
    if (!all_constraints_info.empty()) {
        // Use the DSU-ordered wire names if available and complete, otherwise the original order
        // For consistency with the 'assign result' line, we can reuse ordered_result_wire_names if it was successfully generated
        // However, simpler to just iterate all_constraints_info for this specific field as it's for informational purposes
        // for a later script. The Verilog 'assign result' line already reflects the DSU-based order.
        // For this "result_operand_wire_names" field, the order is less critical than its completeness.
        // Let's use the original order of all_constraints_info to ensure all wires are listed.
        for (const auto &constraint_info_entry : all_constraints_info) {
            result_operand_wires_json_array.push_back(
                constraint_info_entry.assigned_wire_name);
        }
    }
    components_json_output_data["result_operand_wire_names"] =
        result_operand_wires_json_array;

    // --- File Naming and Writing ---
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

    // Write Verilog file
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

    // Write Component JSON file
    filesystem::path output_components_path =
        filesystem::path(output_v_dir) / (test_id + "_components.json");
    ofstream output_components_stream(output_components_path);
    if (output_components_stream.is_open()) {
        output_components_stream << components_json_output_data.dump(4) << endl;
        output_components_stream.close();
        cout << "Component metadata file generated successfully: "
             << output_components_path << endl;
    } else {
        cerr << "Error: Could not open component metadata file for writing: "
             << output_components_path << endl;
        // Not returning error for this, as Verilog generation might be primary goal for some tests
    }

    return 0;
}