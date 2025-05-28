#ifndef SOLVER_FUNCTIONS_H
#define SOLVER_FUNCTIONS_H
#include <string>
#endif

#include "nlohmann/json.hpp"
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
#include <climits> // For ULLONG_MAX

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

// struct DSUComponentForVerilogOrder { // This was for the old strategy
//     size_t size_metric;
//     std::vector<std::string> wire_names_in_component;
//     int representative_dsu_root_idx;
// };

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
                    return std::nullopt; // Format like 'b1, 'hF needs width
                ec.bit_width = std::stoi(s.substr(0, prime_pos));
                if (ec.bit_width <= 0)
                    return std::nullopt;
                char type_char = s[prime_pos + 1];
                std::string val_str = s.substr(prime_pos + 2);
                if (val_str.empty())
                    return std::nullopt; // Value part missing
                unsigned long long num_val = 0;
                // Check for X or Z before conversion
                if (val_str.find_first_of("xzXZ") != std::string::npos)
                    return std::nullopt;

                if (type_char == 'b') {
                    if (val_str.find_first_not_of("01") != std::string::npos) return std::nullopt;
                    if (val_str.length() > ec.bit_width && ec.bit_width > 0) val_str = val_str.substr(val_str.length() - ec.bit_width); // ABC standard
                    num_val = std::stoull(val_str, nullptr, 2);
                } else if (type_char == 'h' || type_char == 'H') {
                    if (val_str.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return std::nullopt;
                     if (val_str.length() * 4 > ec.bit_width && ec.bit_width > 0) {
                        int expected_hex_chars = (ec.bit_width + 3) / 4;
                        if (val_str.length() > expected_hex_chars) val_str = val_str.substr(val_str.length() - expected_hex_chars);
                    }
                    num_val = std::stoull(val_str, nullptr, 16);
                } else if (type_char == 'd' || type_char == 'D') {
                    if (val_str.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 10);
                } else if (type_char == 'o' || type_char == 'O') {
                    if (val_str.find_first_not_of("01234567") != std::string::npos) return std::nullopt;
                     if (val_str.length() * 3 > ec.bit_width && ec.bit_width > 0) {
                        int expected_oct_chars = (ec.bit_width + 2) / 3;
                         if (val_str.length() > expected_oct_chars) val_str = val_str.substr(val_str.length() - expected_oct_chars);
                    }
                    num_val = std::stoull(val_str, nullptr, 8);
                } else
                    return std::nullopt; // Unknown base
                
                ec.value = static_cast<long long>(num_val); // Potential truncation if num_val > LLONG_MAX

                // Apply bit_width mask for is_zero and is_all_ones
                if (ec.bit_width > 0 && ec.bit_width < 64) { // Max stoull is unsigned long long
                    unsigned long long mask = (1ULL << ec.bit_width) - 1;
                    num_val &= mask;
                } else if (ec.bit_width == 64) {
                    // num_val is already correct if it fit in unsigned long long
                } else if (ec.bit_width > 64) {
                    // Cannot accurately represent with stoull and ULL mask for is_all_ones
                    // For now, we'll assume such large constants are rare or handled differently.
                    // is_zero check is still fine if num_val was 0.
                    // is_all_ones for >64 bits would require big-int or string manipulation.
                    // For simplicity, we'll say it's not all ones if bit_width > 64 unless it's zero.
                    ec.is_all_ones = false; 
                }


                ec.is_zero = (num_val == 0);
                if (ec.bit_width > 0 && ec.bit_width <= 64) { // Re-check after masking for is_all_ones
                    unsigned long long all_ones_mask_for_width = (ec.bit_width == 64) ? ULLONG_MAX : (1ULL << ec.bit_width) - 1;
                    ec.is_all_ones = (num_val == all_ones_mask_for_width);
                } else if (ec.bit_width == 0) { // Should have been caught by (ec.bit_width <= 0)
                     return std::nullopt;
                }
                // Ensure single bit constants are handled correctly for is_all_ones / is_zero
                if (ec.bit_width == 1) {
                    ec.is_all_ones = (num_val == 1);
                    ec.is_zero = (num_val == 0);
                }


                return ec;
            } else { // Decimal without '
                if (s.find_first_not_of("0123456789") != std::string::npos || s.empty())
                    return std::nullopt;
                ec.value = std::stoll(s);
                ec.is_zero = (ec.value == 0);
                // Default Verilog integer width is 32 bits unless it's 0 or 1
                if (ec.value == 1) {
                    ec.bit_width = 1; // Or should be 32 and value 1? Standard says sized literals.
                                      // Unsized unbased are decimal, width implementation-defined (often 32).
                    ec.is_all_ones = true; // 1'b1
                } else if (ec.value == 0) {
                    ec.bit_width = 1; // 1'b0
                } else {
                    ec.bit_width = 32; // Default for unsized decimal
                    // is_all_ones for 32-bit would be 0xFFFFFFFF
                    if (static_cast<unsigned long long>(ec.value) == 0xFFFFFFFFULL) {
                        ec.is_all_ones = true;
                    }
                }
                return ec;
            }
        } catch (const std::exception &e) {
            // std::cerr << "Exception parsing constant '" << s << "': " << e.what() << std::endl;
            return std::nullopt;
        }
        return std::nullopt;
    }
};

struct ExpressionDetail {
    string verilog_expr_str;
    std::set<int> variable_ids; // Store original PI IDs
    std::optional<bool> const_expr_evaluates_to_nonzero;
};

struct ConstraintInternalInfo {
    string verilog_expression_body;
    std::set<int> variable_ids; // Original PI IDs used in this constraint
    string assigned_wire_name;
    int original_json_constraint_index; // Index from input constraint_list
    int unique_constraint_id;      // A unique ID for this constraint (0 to N-1)
    std::optional<bool> determined_wire_value; // True if 1'b1, False if 1'b0
};

// Forward declaration
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map,
    const std::map<std::string, int>& var_name_to_id_map // For VAR nodes if they use names
);


static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map,
    const std::map<std::string, int>& var_name_to_id_map) {
    ExpressionDetail current_detail;
    string type = node["op"];
    current_detail.const_expr_evaluates_to_nonzero = std::nullopt;

    if (type == "VAR") {
        // Assuming variables are identified by "id" in the JSON expression tree
        // and these IDs correspond to the "id" field in the "variable_list"
        if (!node.contains("id") || !node["id"].is_number_integer()) {
             throw std::runtime_error("VAR node in expression missing or invalid 'id'");
        }
        int id = node["id"].get<int>();
        current_detail.verilog_expr_str = "var_" + to_string(id);
        current_detail.variable_ids.insert(id);
    } else if (type == "CONST") {
        current_detail.verilog_expr_str = node.value("value", "1'b0");
        std::optional<EvaluatedConstant> ec =
            EvaluatedConstant::from_verilog_string(
                current_detail.verilog_expr_str);
        if (ec.has_value()) {
            current_detail.const_expr_evaluates_to_nonzero = !ec->is_zero;
        } else {
            // Could not parse as a simple constant, treat as symbolic
            // Or throw error if CONST must be evaluatable. For now, assume it might be symbolic like `WIDTH'bx`
             current_detail.const_expr_evaluates_to_nonzero = std::nullopt;
        }
    } else if (type == "BIT_NEG" || type == "LOG_NEG" || type == "MINUS") {
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map, var_name_to_id_map);
        current_detail.variable_ids = lhs_detail.variable_ids;
        string op_symbol_default;
        if (type == "BIT_NEG") op_symbol_default = "~";
        else if (type == "LOG_NEG") op_symbol_default = "!";
        else if (type == "MINUS") op_symbol_default = "-"; // Unary minus

        bool evaluated = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
            std::optional<EvaluatedConstant> ec_lhs =
                EvaluatedConstant::from_verilog_string(lhs_detail.verilog_expr_str);
            if (ec_lhs.has_value()) {
                if (type == "LOG_NEG") {
                    current_detail.const_expr_evaluates_to_nonzero = ec_lhs->is_zero;
                    current_detail.verilog_expr_str = (ec_lhs->is_zero) ? "1'b1" : "1'b0";
                    evaluated = true;
                }
                // BIT_NEG and MINUS on constants could also be pre-evaluated
                // For simplicity, we are only pre-evaluating LOG_NEG for now
            }
        }

        if (!evaluated) {
            current_detail.verilog_expr_str = op_symbol_default + "(" + lhs_detail.verilog_expr_str + ")";
            // If input is not constant, output is not constant
            current_detail.const_expr_evaluates_to_nonzero = std::nullopt;
        }

    } else { // Binary operations
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map, var_name_to_id_map);
        ExpressionDetail rhs_detail =
            get_expression_details(node["rhs_expression"], all_divisors_map, var_name_to_id_map);

        current_detail.variable_ids = lhs_detail.variable_ids;
        current_detail.variable_ids.insert(rhs_detail.variable_ids.begin(),
                                           rhs_detail.variable_ids.end());

        if (type == "DIV" || type == "MOD") {
            // If RHS is not a provably non-zero constant, add its non-zero check as a constraint
            bool rhs_is_const_nonzero = false;
            if (rhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
                rhs_is_const_nonzero = rhs_detail.const_expr_evaluates_to_nonzero.value();
            }
            
            if (!rhs_is_const_nonzero) { // if not constant OR constant but zero
                 // We only add it if it's not already there (map handles uniqueness by string)
                all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
            }
        }

        bool evaluated_binary_const = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value() &&
            rhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
            std::optional<EvaluatedConstant> ec_lhs =
                EvaluatedConstant::from_verilog_string(lhs_detail.verilog_expr_str);
            std::optional<EvaluatedConstant> ec_rhs =
                EvaluatedConstant::from_verilog_string(rhs_detail.verilog_expr_str);

            if (ec_lhs.has_value() && ec_rhs.has_value()) {
                bool result_is_nonzero = false;
                bool evaluable = true;
                if (type == "EQ") result_is_nonzero = (ec_lhs->value == ec_rhs->value);
                else if (type == "NEQ") result_is_nonzero = (ec_lhs->value != ec_rhs->value);
                else if (type == "LOG_AND") result_is_nonzero = (!ec_lhs->is_zero && !ec_rhs->is_zero);
                else if (type == "LOG_OR") result_is_nonzero = (!ec_lhs->is_zero || !ec_rhs->is_zero);
                // Add more constant evaluations if needed (LT, GT, ADD, etc.)
                else evaluable = false;

                if (evaluable) {
                    current_detail.const_expr_evaluates_to_nonzero = result_is_nonzero;
                    current_detail.verilog_expr_str = result_is_nonzero ? "1'b1" : "1'b0";
                    evaluated_binary_const = true;
                }
            }
        }

        if (!evaluated_binary_const) {
            current_detail.const_expr_evaluates_to_nonzero = std::nullopt; // If not evaluated, result is symbolic
            string op_symbol;
            if (type == "IMPLY") {
                 // A -> B  is equivalent to !A || B
                current_detail.verilog_expr_str =
                    "( (!(" + lhs_detail.verilog_expr_str + ")) || (" +
                    rhs_detail.verilog_expr_str + ") )";
            } else {
                if (type == "ADD") op_symbol = "+";
                else if (type == "SUB") op_symbol = "-";
                else if (type == "MUL") op_symbol = "*";
                else if (type == "DIV") op_symbol = "/";
                else if (type == "MOD") op_symbol = "%";
                else if (type == "LSHIFT") op_symbol = "<<";
                else if (type == "RSHIFT") op_symbol = ">>";
                else if (type == "BIT_AND") op_symbol = "&";
                else if (type == "BIT_OR") op_symbol = "|";
                else if (type == "BIT_XOR") op_symbol = "^";
                else if (type == "LOG_AND") op_symbol = "&&";
                else if (type == "LOG_OR") op_symbol = "||";
                else if (type == "EQ") op_symbol = "==";
                else if (type == "NEQ") op_symbol = "!=";
                else if (type == "LT") op_symbol = "<";
                else if (type == "LTE") op_symbol = "<="; // Corrected
                else if (type == "GT") op_symbol = ">";   // Corrected
                else if (type == "GTE") op_symbol = ">="; // Corrected
                else {
                    current_detail.verilog_expr_str = "/* UNHANDLED_OP_STR: " + type + " */ 1'b1"; // Default to true to avoid breaking AND chain
                     std::cerr << "Warning: Unhandled operator type '" << type << "' in expression. Defaulting its Verilog to 1'b1." << std::endl;
                    // Or throw an error:
                    // throw std::runtime_error("Unhandled operator type: " + type);
                }
                 if (!op_symbol.empty()) {
                    current_detail.verilog_expr_str =
                        "(" + lhs_detail.verilog_expr_str + " " + op_symbol + " " +
                        rhs_detail.verilog_expr_str + ")";
                }
            }
        }
    }
    return current_detail;
}


struct DSUComponent {
    int root_representative_constraint_idx;
    std::set<int> constraint_unique_ids_in_component;
    std::set<int> pi_var_ids_in_component; // Original PI IDs
};


int json_v_converter(const string &input_json_path,
                     const string &output_v_dir_str) { // Changed output_v_dir to output_v_dir_str
    json original_json_data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open()) {
        cerr << "Error: Cannot open input JSON file: " << input_json_path << endl;
        return 1;
    }
    try {
        input_json_stream >> original_json_data;
    } catch (const json::parse_error &e) {
        input_json_stream.close();
        cerr << "Error: Parsing input JSON failed: " << e.what() << endl;
        return 1;
    }
    input_json_stream.close();

    if (!original_json_data.contains("variable_list") || !original_json_data["variable_list"].is_array() ||
        !original_json_data.contains("constraint_list") || !original_json_data["constraint_list"].is_array()) {
        cerr << "Error: JSON must contain 'variable_list' and 'constraint_list' arrays." << endl;
        return 1;
    }

    filesystem::path output_v_dir(output_v_dir_str);
    try {
        if (!filesystem::exists(output_v_dir)) {
            if (!filesystem::create_directories(output_v_dir)) {
                 cerr << "Error: Could not create output directory: " << output_v_dir_str << endl;
                 return 1;
            }
        } else if (!filesystem::is_directory(output_v_dir)) {
             cerr << "Error: Output path exists but is not a directory: " << output_v_dir_str << endl;
             return 1;
        }
    } catch (const filesystem::filesystem_error& e) {
        cerr << "Error with output directory '" << output_v_dir_str << "': " << e.what() << endl;
        return 1;
    }


    json variable_list_json = original_json_data["variable_list"];
    json constraint_list_json = original_json_data["constraint_list"];

    std::map<int, json> var_id_to_info_map;
    std::map<string, int> var_name_to_id_map; // If VAR nodes use names
    std::set<int> all_pi_ids_from_variable_list;

    for (const auto &var_json : variable_list_json) {
        if (!var_json.contains("id") || !var_json["id"].is_number_integer() ||
            !var_json.contains("name") || !var_json["name"].is_string() ||
            !var_json.contains("bit_width") || !var_json["bit_width"].is_number_integer()) {
            cerr << "Error: Variable entry is missing id, name, or bit_width, or types are incorrect." << endl;
            return 1;
        }
        int id = var_json["id"].get<int>();
        var_id_to_info_map[id] = var_json;
        var_name_to_id_map[var_json["name"].get<string>()] = id;
        all_pi_ids_from_variable_list.insert(id);
    }


    std::vector<ConstraintInternalInfo> all_constraints_info;
    std::map<std::string, ExpressionDetail> all_found_divisors_map;
    int unique_constraint_id_counter = 0;

    for (size_t i = 0; i < constraint_list_json.size(); ++i) {
        const auto &cnstr_json_node = constraint_list_json[i];
        ExpressionDetail detail = get_expression_details(cnstr_json_node, all_found_divisors_map, var_name_to_id_map);
        
        ConstraintInternalInfo info;
        info.verilog_expression_body = detail.verilog_expr_str;
        info.variable_ids = detail.variable_ids; // These are original PI IDs
        info.assigned_wire_name = "cnstr_" + to_string(unique_constraint_id_counter) + "_w";
        info.original_json_constraint_index = static_cast<int>(i);
        info.unique_constraint_id = unique_constraint_id_counter++;
        info.determined_wire_value = detail.const_expr_evaluates_to_nonzero;
        all_constraints_info.push_back(info);
    }

    for (const auto &pair_str_detail : all_found_divisors_map) {
        const ExpressionDetail &div_detail = pair_str_detail.second;
        ConstraintInternalInfo info;
        info.verilog_expression_body = "(" + div_detail.verilog_expr_str + " != 0)"; // Constraint is "divisor must be non-zero"
        info.variable_ids = div_detail.variable_ids;
        info.assigned_wire_name = "cnstr_" + to_string(unique_constraint_id_counter) + "_w"; // Unique name
        info.original_json_constraint_index = -1; // Indicates it's an auto-generated divisor constraint
        info.unique_constraint_id = unique_constraint_id_counter++;
        // If the divisor itself was a constant, its non-zero status might be known.
        if (div_detail.const_expr_evaluates_to_nonzero.has_value()) {
            info.determined_wire_value = div_detail.const_expr_evaluates_to_nonzero.value(); // The constraint is true if divisor is non-zero
        } else {
            info.determined_wire_value = std::nullopt;
        }
        all_constraints_info.push_back(info);
    }

    // --- DSU for Grouping Constraints by Shared PIs ---
    int num_total_constraints = all_constraints_info.size();
    std::vector<int> dsu_parent(num_total_constraints);
    std::iota(dsu_parent.begin(), dsu_parent.end(), 0);

    std::function<int(int)> find_set = [&](int i) -> int {
        if (dsu_parent[i] == i) return i;
        return dsu_parent[i] = find_set(dsu_parent[i]);
    };
    std::function<void(int, int)> unite_sets = [&](int i, int j) {
        i = find_set(i);
        j = find_set(j);
        if (i != j) dsu_parent[j] = i;
    };

    std::map<int, std::vector<int>> pi_id_to_constraint_indices_map;
    for (int i = 0; i < num_total_constraints; ++i) {
        // Skip constant constraints for DSU grouping as they don't introduce PI dependencies for grouping
        // Or rather, they don't *require* other PIs.
        // If a constraint is constant, it doesn't force merging with other constraints via PIs.
        // However, we still need to include them in *some* component if they are not '1'b1'.
        // For now, let's include all constraints in DSU.
        for (int pi_id : all_constraints_info[i].variable_ids) {
            pi_id_to_constraint_indices_map[pi_id].push_back(all_constraints_info[i].unique_constraint_id);
        }
    }

    for (const auto &pair_pi_constraints : pi_id_to_constraint_indices_map) {
        const std::vector<int> &constraints_sharing_pi = pair_pi_constraints.second;
        if (constraints_sharing_pi.size() > 1) {
            for (size_t i = 0; i < constraints_sharing_pi.size() - 1; ++i) {
                // Unite based on the unique_constraint_id which is the index in all_constraints_info
                unite_sets(constraints_sharing_pi[i], constraints_sharing_pi[i+1]);
            }
        }
    }

    std::map<int, DSUComponent> dsu_components_map;
    std::set<int> pi_ids_used_in_any_constraint;

    for (int i = 0; i < num_total_constraints; ++i) {
        int root = find_set(i);
        dsu_components_map[root].root_representative_constraint_idx = root;
        dsu_components_map[root].constraint_unique_ids_in_component.insert(all_constraints_info[i].unique_constraint_id);
        for (int pi_id : all_constraints_info[i].variable_ids) {
            dsu_components_map[root].pi_var_ids_in_component.insert(pi_id);
            pi_ids_used_in_any_constraint.insert(pi_id);
        }
    }
    
    json manifest_json;
    manifest_json["original_json_path"] = input_json_path;
    manifest_json["components"] = json::array();
    manifest_json["unsat_by_preprocessing"] = false;

    int component_file_idx = 0;
    bool overall_problem_is_unsat = false;

    for (auto &pair_root_component : dsu_components_map) {
        DSUComponent &component = pair_root_component.second;
        std::vector<string> component_v_lines;
        std::string component_module_name = "component_" + to_string(component_file_idx);
        
        component_v_lines.push_back("module " + component_module_name + "(");
        
        json component_manifest_entry;
        component_manifest_entry["id"] = component_file_idx;
        component_manifest_entry["verilog_file"] = component_module_name + ".v";
        component_manifest_entry["aig_file"] = component_module_name + ".aag"; // Placeholder
        component_manifest_entry["result_json_file"] = component_module_name + "_results.json"; // Placeholder
        component_manifest_entry["primary_inputs"] = json::array();
        component_manifest_entry["is_trivial_sat"] = false;
        component_manifest_entry["is_trivial_unsat"] = false;

        std::vector<string> input_declarations;
        std::set<int> sorted_pi_ids(component.pi_var_ids_in_component.begin(), component.pi_var_ids_in_component.end());

        for (int pi_id : sorted_pi_ids) {
            if (var_id_to_info_map.count(pi_id)) {
                const auto& var_info = var_id_to_info_map[pi_id];
                string original_name = var_info["name"];
                int bit_width = var_info["bit_width"];
                string verilog_var_name = "var_" + to_string(pi_id); // Consistent naming

                input_declarations.push_back("    input wire [" + to_string(bit_width - 1) + ":0] " + verilog_var_name);
                
                json pi_manifest_entry;
                pi_manifest_entry["original_json_id"] = pi_id;
                pi_manifest_entry["original_name"] = original_name;
                pi_manifest_entry["bit_width"] = bit_width;
                pi_manifest_entry["local_name_in_verilog"] = verilog_var_name;
                component_manifest_entry["primary_inputs"].push_back(pi_manifest_entry);
            } else {
                 cerr << "Warning: PI ID " << pi_id << " found in constraint but not in variable_list. Skipping for component " << component_file_idx << "." << endl;
            }
        }

        for (const auto& decl : input_declarations) {
            component_v_lines.push_back(decl + ",");
        }
        component_v_lines.push_back("    output wire result");
        component_v_lines.push_back(");");
        component_v_lines.push_back(""); // Blank line

        std::vector<string> constraint_wire_names_for_component_result;
        bool component_is_unsat = false;
        int effective_constraints_in_component = 0;

        for (int constraint_unique_id : component.constraint_unique_ids_in_component) {
            const auto& cnstr_info = all_constraints_info[constraint_unique_id]; // unique_id is the index
            component_v_lines.push_back("    wire " + cnstr_info.assigned_wire_name + ";");
            if (cnstr_info.determined_wire_value.has_value()) {
                if (!cnstr_info.determined_wire_value.value()) { // Constraint is 1'b0
                    component_v_lines.push_back("    assign " + cnstr_info.assigned_wire_name + " = 1'b0;");
                    component_is_unsat = true; // This component (and thus whole problem) is UNSAT
                    // We still write the Verilog, but manifest will mark it.
                } else { // Constraint is 1'b1
                    component_v_lines.push_back("    assign " + cnstr_info.assigned_wire_name + " = 1'b1;");
                    // This constraint is true, don't add its wire to the final AND unless it's the only one
                }
            } else {
                component_v_lines.push_back("    assign " + cnstr_info.assigned_wire_name + " = |(" + cnstr_info.verilog_expression_body + ");");
                constraint_wire_names_for_component_result.push_back(cnstr_info.assigned_wire_name);
                effective_constraints_in_component++;
            }
        }
        component_v_lines.push_back(""); // Blank line

        if (component_is_unsat) {
            component_v_lines.push_back("    assign result = 1'b0;");
            overall_problem_is_unsat = true;
            component_manifest_entry["is_trivial_unsat"] = true;
        } else if (constraint_wire_names_for_component_result.empty()) {
            // All constraints were 1'b1 or no effective constraints
            component_v_lines.push_back("    assign result = 1'b1;");
            component_manifest_entry["is_trivial_sat"] = true;
        } else {
            string result_assign_line = "    assign result = ";
            for (size_t i = 0; i < constraint_wire_names_for_component_result.size(); ++i) {
                result_assign_line += constraint_wire_names_for_component_result[i];
                if (i < constraint_wire_names_for_component_result.size() - 1) {
                    result_assign_line += " & ";
                }
            }
            result_assign_line += ";";
            component_v_lines.push_back(result_assign_line);
        }
        
        component_v_lines.push_back("endmodule");

        // Write component Verilog file
        filesystem::path component_v_path = output_v_dir / (component_module_name + ".v");
        ofstream component_v_stream(component_v_path);
        if (!component_v_stream.is_open()) {
            cerr << "Error: Cannot open component Verilog file for writing: " << component_v_path << endl;
            // Continue to next component, but problem is likely
        } else {
            for (const string &line : component_v_lines) {
                component_v_stream << line << endl;
            }
            component_v_stream.close();
        }
        manifest_json["components"].push_back(component_manifest_entry);
        component_file_idx++;
    }
    
    // Handle PIs not used in any constraint (these are "don't cares" for satisfiability)
    manifest_json["global_variables_not_in_any_component"] = json::array();
    for (int pi_id : all_pi_ids_from_variable_list) {
        if (pi_ids_used_in_any_constraint.find(pi_id) == pi_ids_used_in_any_constraint.end()) {
            if (var_id_to_info_map.count(pi_id)) {
                 const auto& var_info = var_id_to_info_map[pi_id];
                 json pi_manifest_entry;
                 pi_manifest_entry["original_json_id"] = pi_id;
                 pi_manifest_entry["original_name"] = var_info["name"];
                 pi_manifest_entry["bit_width"] = var_info["bit_width"];
                 manifest_json["global_variables_not_in_any_component"].push_back(pi_manifest_entry);
            }
        }
    }


    if (overall_problem_is_unsat) {
        manifest_json["unsat_by_preprocessing"] = true;
        cout << "Info: Problem determined to be UNSAT during preprocessing by json_v_converter." << endl;
    }
    manifest_json["num_components"] = component_file_idx;


    // Write manifest.json
    filesystem::path manifest_path = output_v_dir / "manifest.json";
    ofstream manifest_stream(manifest_path);
    if (!manifest_stream.is_open()) {
        cerr << "Error: Cannot open manifest.json for writing: " << manifest_path << endl;
        return 1; // Critical failure
    }
    manifest_stream << manifest_json.dump(4) << endl;
    manifest_stream.close();

    cout << "json_v_converter finished. Generated " << component_file_idx 
         << " component Verilog file(s) and manifest.json in " << output_v_dir_str << endl;
    if (overall_problem_is_unsat) {
        return 2; // Special return code for UNSAT detected by preprocessor
    }

    return 0;
}