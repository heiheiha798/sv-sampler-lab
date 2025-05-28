#include "nlohmann/json.hpp"
#include "solver_functions.h" // For JVC_ constants
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

// Define constants (could be in solver_functions.h if used by main directly,
// but typically they are an internal detail of the function using them,
// communicated via the output parameter)
const int JVC_INTERNAL_ERROR = -1;
const int JVC_UNSAT_PREPROCESSED = -2;
const int JVC_SUCCESS_SINGLE_FILE = 1;
static const int SPLIT_COMPONENTS_THRESHOLD = 3; // If num components > this, split. (So 8 or less = single file)


struct DSUComponentForVerilogOrder // Kept for reference or minor use
{
    size_t size_metric;
    std::vector<std::string> wire_names_in_component;
    int representative_dsu_root_idx;
};

struct EvaluatedConstant
{
    long long value;
    int bit_width;
    bool is_zero;
    bool is_all_ones;
    static std::optional<EvaluatedConstant>
    from_verilog_string(const std::string &s)
    {
        EvaluatedConstant ec;
        ec.is_zero = false;
        ec.is_all_ones = false;
        ec.bit_width = -1;
        try
        {
            size_t prime_pos = s.find('\'');
            if (prime_pos != std::string::npos)
            {
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
                if (type_char == 'b')
                {
                    if (val_str.find_first_not_of("01xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 2);
                }
                else if (type_char == 'h' || type_char == 'H')
                {
                    if (val_str.find_first_not_of(
                            "0123456789abcdefABCDEFxzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 16);
                }
                else if (type_char == 'd' || type_char == 'D')
                {
                    if (val_str.find_first_not_of("0123456789xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 10);
                }
                else if (type_char == 'o' || type_char == 'O')
                {
                    if (val_str.find_first_not_of("01234567xzXZ") !=
                            std::string::npos ||
                        val_str.find_first_of("xzXZ") != std::string::npos)
                        return std::nullopt;
                    num_val = std::stoull(val_str, nullptr, 8);
                }
                else
                    return std::nullopt;
                ec.value = static_cast<long long>(num_val);
                ec.is_zero = (num_val == 0);
                if (ec.bit_width > 0 && ec.bit_width <= 64)
                {
                    unsigned long long all_ones_mask =
                        (ec.bit_width == 64) ? ULLONG_MAX
                                             : (1ULL << ec.bit_width) - 1;
                    ec.is_all_ones = (num_val == all_ones_mask);
                }
                else if (ec.bit_width == 0) // Should not happen due to check above
                    return std::nullopt;

                // Fix for 1-bit constants
                if (ec.bit_width == 1 && num_val == 1)
                    ec.is_all_ones = true;
                if (ec.bit_width == 1 && num_val == 0)
                    ec.is_zero = true; // Already set, but explicit
                
                return ec;
            }
            else // Decimal number without '
            {
                if (s.find_first_not_of("0123456789") != std::string::npos || s.empty())
                    return std::nullopt;
                ec.value = std::stoll(s);
                ec.is_zero = (ec.value == 0);
                // Sized literals like 32'd10 become "10" if no explicit size
                // Default Verilog literal size is 32 bits
                if (ec.value == 1) {
                    ec.bit_width = 1; // Typically, '1' is 1'b1 unless context forces wider
                    ec.is_all_ones = true;
                } else if (ec.value == 0) {
                     ec.bit_width = 1; // same for 0
                }
                else {
                     ec.bit_width = 32; // Default for unsized decimal numbers in Verilog
                }
                return ec;
            }
        }
        catch (const std::exception &e)
        {
            // cerr << "Exception in EvaluatedConstant: " << e.what() << " for string " << s << endl;
            return std::nullopt;
        }
        // Should not be reached if logic is correct
        return std::nullopt;
    }
};

struct ExpressionDetail
{
    string verilog_expr_str;
    std::set<int> variable_ids; // 0-indexed IDs corresponding to original var list
    std::optional<bool> const_expr_evaluates_to_nonzero;
};

struct ConstraintInternalInfo
{
    string verilog_expression_body;
    std::set<int> variable_ids; // 0-indexed IDs
    string assigned_wire_name;
    int original_json_constraint_index; // -1 for divisor constraints
    int original_overall_idx;           // Unique index among all_constraints_info_orig
    std::optional<bool> determined_wire_value; // true if 1, false if 0
};

// Forward declaration
static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map);

static ExpressionDetail get_expression_details(
    const json &node,
    std::map<std::string, ExpressionDetail> &all_divisors_map)
{
    ExpressionDetail current_detail;
    string type = node["op"];
    current_detail.const_expr_evaluates_to_nonzero = std::nullopt;

    if (type == "VAR")
    {
        int id = node.value("id", 0); // This ID is the index in the original JSON variable_list
        current_detail.verilog_expr_str = "var_" + to_string(id);
        current_detail.variable_ids.insert(id);
    }
    else if (type == "CONST")
    {
        current_detail.verilog_expr_str = node.value("value", "1'b0");
        std::optional<EvaluatedConstant> ec =
            EvaluatedConstant::from_verilog_string(
                current_detail.verilog_expr_str);
        if (ec.has_value())
        {
            current_detail.const_expr_evaluates_to_nonzero = !ec->is_zero;
        }
    }
    else if (type == "BIT_NEG" || type == "LOG_NEG" || type == "MINUS")
    {
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map);
        current_detail.variable_ids = lhs_detail.variable_ids;
        string op_symbol_default;
        if (type == "BIT_NEG") op_symbol_default = "~";
        else if (type == "LOG_NEG") op_symbol_default = "!";
        else if (type == "MINUS") op_symbol_default = "-";

        bool evaluated = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value())
        {
            // Attempt to evaluate if the operand is a constant
            std::optional<EvaluatedConstant> ec_lhs =
                EvaluatedConstant::from_verilog_string(
                    lhs_detail.verilog_expr_str);
            if (ec_lhs.has_value())
            {
                if (type == "LOG_NEG") {
                    current_detail.const_expr_evaluates_to_nonzero = ec_lhs->is_zero;
                    current_detail.verilog_expr_str = (ec_lhs->is_zero) ? "1'b1" : "1'b0";
                    evaluated = true;
                }
                // BIT_NEG and MINUS on constants are not simplified to 1'b0/1'b1 here,
                // just their expression string is formed. We only care about zero/non-zero for const_expr_evaluates_to_nonzero.
                // For BIT_NEG: ~0 is non-zero (all ones). ~non-zero can be zero or non-zero.
                // Example: ~1'b1 = 1'b0 (zero). ~2'b10 = 2'b01 (non-zero).
                // This needs more careful handling if we want to propagate BIT_NEG results.
                // For now, LOG_NEG is the main one for boolean constant propagation.
            }
        }
        
        if (!evaluated) {
            current_detail.verilog_expr_str =
                op_symbol_default + "(" + lhs_detail.verilog_expr_str + ")";
            // For BIT_NEG and MINUS, if lhs was constant, we don't know if result is zero/non-zero without full eval
            // unless it was LOG_NEG.
            if (type != "LOG_NEG") current_detail.const_expr_evaluates_to_nonzero = std::nullopt;
        }
    }
    else // Binary operations
    {
        ExpressionDetail lhs_detail =
            get_expression_details(node["lhs_expression"], all_divisors_map);
        ExpressionDetail rhs_detail =
            get_expression_details(node["rhs_expression"], all_divisors_map);

        current_detail.variable_ids = lhs_detail.variable_ids;
        current_detail.variable_ids.insert(rhs_detail.variable_ids.begin(),
                                           rhs_detail.variable_ids.end());

        if (type == "DIV" || type == "MOD")
        {
            // If RHS is not a provably non-zero constant, add it as a divisor constraint
            if (rhs_detail.const_expr_evaluates_to_nonzero.has_value()) {
                if (!rhs_detail.const_expr_evaluates_to_nonzero.value()) { // RHS is constant zero
                    // Division by zero! This expression is problematic.
                    // For now, we'll add it to divisors_map, which implies (rhs != 0)
                    // This will lead to an unsatisfiable (divisor_expr != 0) constraint.
                     all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
                }
                // If RHS is const non-zero, no divisor constraint needed for it.
            } else { // RHS is not a constant or its value is unknown
                 all_divisors_map[rhs_detail.verilog_expr_str] = rhs_detail;
            }
        }

        bool evaluated_binary_const = false;
        if (lhs_detail.const_expr_evaluates_to_nonzero.has_value() &&
            rhs_detail.const_expr_evaluates_to_nonzero.has_value())
        {
            std::optional<EvaluatedConstant> ec_lhs = EvaluatedConstant::from_verilog_string(lhs_detail.verilog_expr_str);
            std::optional<EvaluatedConstant> ec_rhs = EvaluatedConstant::from_verilog_string(rhs_detail.verilog_expr_str);

            if (ec_lhs.has_value() && ec_rhs.has_value()) {
                bool result_is_nonzero = false; // temp
                bool evaluable = true; // Can we evaluate this op to a simple boolean?
                if (type == "EQ") result_is_nonzero = (ec_lhs->value == ec_rhs->value);
                else if (type == "NEQ") result_is_nonzero = (ec_lhs->value != ec_rhs->value);
                else if (type == "LOG_AND") result_is_nonzero = (!ec_lhs->is_zero && !ec_rhs->is_zero);
                else if (type == "LOG_OR") result_is_nonzero = (!ec_lhs->is_zero || !ec_rhs->is_zero);
                // LT, LTE, GT, GTE for constants
                else if (type == "LT") result_is_nonzero = (ec_lhs->value < ec_rhs->value);
                else if (type == "LTE") result_is_nonzero = (ec_lhs->value <= ec_rhs->value);
                else if (type == "GT") result_is_nonzero = (ec_lhs->value > ec_rhs->value);
                else if (type == "GTE") result_is_nonzero = (ec_lhs->value >= ec_rhs->value);
                else evaluable = false; // Other ops (ADD, SUB, etc.) don't directly yield a boolean for this logic

                if (evaluable) {
                    current_detail.const_expr_evaluates_to_nonzero = result_is_nonzero;
                    current_detail.verilog_expr_str = result_is_nonzero ? "1'b1" : "1'b0";
                    evaluated_binary_const = true;
                }
            }
        }

        if (!evaluated_binary_const) {
            current_detail.const_expr_evaluates_to_nonzero = std::nullopt; // Can't be sure
            string op_symbol;
            if (type == "IMPLY") {
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
                else if (type == "LTE") op_symbol = "<="; // Fixed typo: == to =
                else if (type == "GT") op_symbol = ">";   // Fixed typo: == to =
                else if (type == "GTE") op_symbol = ">=";  // Fixed typo: == to =
                else {
                    current_detail.verilog_expr_str = "/* UNHANDLED_OP_STR: " + type + " */ 1'b1"; // Default to true to avoid false UNSAT
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


// New struct for DSU-based component generation
struct ComponentGenerationInfo {
    std::vector<ConstraintInternalInfo> constraints_in_group;
    std::set<int> variable_ids_in_group; // 0-indexed IDs from original variable_list
    json component_specific_variable_list_json; // For the component's own mini-JSON
    std::vector<std::string> original_variable_names_in_group; // For mapping file
    int component_id_num;
};

// New DSU analysis function
static std::vector<ComponentGenerationInfo> group_constraints_by_dsu(
    const std::vector<ConstraintInternalInfo>& effective_constraints,
    const json& original_variable_list_json) 
{
    std::vector<ComponentGenerationInfo> component_groups;
    if (effective_constraints.empty()) {
        return component_groups;
    }

    int num_effective = effective_constraints.size();
    std::vector<int> dsu_parent(num_effective);
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

    std::map<int, std::vector<int>> var_to_effective_idx_map;
    for (int i = 0; i < num_effective; ++i) {
        for (int var_id : effective_constraints[i].variable_ids) {
            var_to_effective_idx_map[var_id].push_back(i);
        }
    }

    for (const auto& pair_var_indices : var_to_effective_idx_map) {
        const std::vector<int>& indices = pair_var_indices.second;
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
    
    int current_comp_id = 0;
    for (const auto& pair_entry : dsu_root_to_effective_indices) {
        ComponentGenerationInfo comp_info;
        comp_info.component_id_num = current_comp_id++;
        std::set<int> var_ids_for_this_comp;
        for (int eff_idx : pair_entry.second) {
            comp_info.constraints_in_group.push_back(effective_constraints[eff_idx]);
            var_ids_for_this_comp.insert(
                effective_constraints[eff_idx].variable_ids.begin(),
                effective_constraints[eff_idx].variable_ids.end()
            );
        }
        comp_info.variable_ids_in_group = var_ids_for_this_comp;

        comp_info.component_specific_variable_list_json = json::array();
        for (int var_id : var_ids_for_this_comp) {
            // var_id is the index into original_variable_list_json
            if (var_id < original_variable_list_json.size()) {
                 comp_info.component_specific_variable_list_json.push_back(original_variable_list_json[var_id]);
                 comp_info.original_variable_names_in_group.push_back(original_variable_list_json[var_id].value("name",""));
            }
        }
        component_groups.push_back(comp_info);
    }
    // Sort components by size or some other metric if desired, e.g., smallest first
     std::sort(component_groups.begin(), component_groups.end(), 
        [](const ComponentGenerationInfo& a, const ComponentGenerationInfo& b){
            if (a.constraints_in_group.size() != b.constraints_in_group.size()) {
                return a.constraints_in_group.size() < b.constraints_in_group.size();
            }
            return a.component_id_num < b.component_id_num; // Fallback to original id
        });
    // Re-assign component_id_num after sorting
    for(size_t i = 0; i < component_groups.size(); ++i) {
        component_groups[i].component_id_num = i;
    }

    return component_groups;
}


// For single Verilog file generation - largely original logic but may need tweaks
static std::vector<std::string> get_ordered_wires_for_single_verilog(
    const std::vector<ConstraintInternalInfo> &effective_constraints)
{
    std::vector<std::string> final_ordered_wires;
    if (effective_constraints.empty()) return final_ordered_wires;

    // This DSU is for ordering wires for a single output, not for splitting.
    // The DSU logic from the original get_ordered_wires_by_dsu_strategy is fine here.
    int num_effective = effective_constraints.size();
    std::vector<int> dsu_parent(num_effective);
    std::iota(dsu_parent.begin(), dsu_parent.end(), 0);
    std::function<int(int)> find_set = [&](int i) -> int {
        if (dsu_parent[i] == i) return i;
        return dsu_parent[i] = find_set(dsu_parent[i]);
    };
    std::function<void(int, int)> unite_sets = [&](int i, int j) {
        i = find_set(i); j = find_set(j);
        if (i != j) dsu_parent[j] = i;
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
    
    struct DSUComponentForOrdering { // Local struct for sorting
        int root_representative_idx;
        size_t num_constraints;
        size_t total_pi_support_size;
        size_t size_metric; // Heuristic for sorting components
        std::vector<int> effective_constraint_indices_in_component;
    };

    std::map<int, std::vector<int>> dsu_root_to_effective_indices;
    for (int i = 0; i < num_effective; ++i) {
        dsu_root_to_effective_indices[find_set(i)].push_back(i);
    }

    std::vector<DSUComponentForOrdering> component_list;
    for (const auto &pair_entry : dsu_root_to_effective_indices) {
        DSUComponentForOrdering comp;
        comp.root_representative_idx = pair_entry.first;
        comp.effective_constraint_indices_in_component = pair_entry.second;
        comp.num_constraints = comp.effective_constraint_indices_in_component.size();
        std::set<int> component_pis;
        for (int eff_idx : comp.effective_constraint_indices_in_component) {
            component_pis.insert(
                effective_constraints[eff_idx].variable_ids.begin(),
                effective_constraints[eff_idx].variable_ids.end());
        }
        comp.total_pi_support_size = component_pis.size();
        comp.size_metric = comp.num_constraints; // Or use total_pi_support_size, or a mix
        component_list.push_back(comp);
    }

    std::sort(component_list.begin(), component_list.end(),
        [](const DSUComponentForOrdering &a, const DSUComponentForOrdering &b) {
            if (a.size_metric != b.size_metric) return a.size_metric < b.size_metric; // Smallest first
            return a.root_representative_idx < b.root_representative_idx;
        });

    for (const auto &comp_info : component_list) {
        for (int effective_idx : comp_info.effective_constraint_indices_in_component) {
            final_ordered_wires.push_back(effective_constraints[effective_idx].assigned_wire_name);
        }
    }
    return final_ordered_wires;
}


int json_v_converter(const string &input_json_path,
                     const string &output_dir_for_files,
                     string &base_file_name_for_outputs_no_ext, // Out param
                     int *status_or_num_components)             // Out param
{
    *status_or_num_components = JVC_INTERNAL_ERROR; // Default to error

    json data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open()) return 1; // File error
    try {
        input_json_stream >> data;
    } catch (const json::parse_error &e) {
        input_json_stream.close();
        return 1; // Parse error
    }
    input_json_stream.close();

    if (!data.contains("variable_list") || !data["variable_list"].is_array() ||
        !data.contains("constraint_list") || !data["constraint_list"].is_array()) {
        return 1; // Format error
    }

    filesystem::path input_json_path_obj(input_json_path);
    string filename_no_ext = input_json_path_obj.stem().string();
    string parent_dir_name = input_json_path_obj.parent_path().filename().string();
    if (!parent_dir_name.empty() && parent_dir_name != "." && parent_dir_name != ".." && parent_dir_name != "/") {
         base_file_name_for_outputs_no_ext = parent_dir_name + "_" + filename_no_ext;
    } else {
         base_file_name_for_outputs_no_ext = filename_no_ext;
    }

    json variable_list_json = data["variable_list"];
    json constraint_list_json = data["constraint_list"];

    std::vector<ConstraintInternalInfo> all_constraints_info_orig;
    std::map<std::string, ExpressionDetail> all_found_divisors_map;
    int current_idx_counter = 0;

    for (size_t i = 0; i < constraint_list_json.size(); ++i) {
        const auto &cnstr_json_node = constraint_list_json[i];
        ExpressionDetail detail = get_expression_details(cnstr_json_node, all_found_divisors_map);
        ConstraintInternalInfo info;
        info.verilog_expression_body = detail.verilog_expr_str;
        info.variable_ids = detail.variable_ids;
        info.assigned_wire_name = "cnstr" + to_string(current_idx_counter) + "_redor";
        info.original_json_constraint_index = static_cast<int>(i);
        info.original_overall_idx = current_idx_counter;
        if (detail.const_expr_evaluates_to_nonzero.has_value()) {
            info.determined_wire_value = detail.const_expr_evaluates_to_nonzero.value();
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
        info.verilog_expression_body = "(" + divisor_expr_str + " != 0)"; // Constraint is "divisor must be non-zero"
        info.variable_ids = div_detail.variable_ids;
        info.assigned_wire_name = "cnstr" + to_string(current_idx_counter) + "_redor"; // Divisor constraint
        info.original_json_constraint_index = -1; // Mark as divisor constraint
        info.original_overall_idx = current_idx_counter;
        if (div_detail.const_expr_evaluates_to_nonzero.has_value()) {
             // If divisor is const zero, (const_zero != 0) is false.
             // If divisor is const non-zero, (const_non_zero != 0) is true.
            info.determined_wire_value = div_detail.const_expr_evaluates_to_nonzero.value();
        } else {
            info.determined_wire_value = std::nullopt;
        }
        all_constraints_info_orig.push_back(info);
        current_idx_counter++;
    }

    bool result_is_const_zero = false;
    for (const auto &info : all_constraints_info_orig) {
        if (info.determined_wire_value.has_value() && !info.determined_wire_value.value()) {
            result_is_const_zero = true;
            break;
        }
    }

    if (result_is_const_zero) {
        *status_or_num_components = JVC_UNSAT_PREPROCESSED;
        // For consistency, still generate a Verilog file that outputs 0.
        // The main orchestrator will handle the UNSAT status.
        vector<string> v_lines;
        v_lines.push_back("module from_json_const_zero(");
        v_lines.push_back("     output wire result");
        v_lines.push_back(");");
        v_lines.push_back("     assign result = 1'b0;");
        v_lines.push_back("endmodule");
        
        filesystem::path output_v_path = filesystem::path(output_dir_for_files) / (base_file_name_for_outputs_no_ext + ".v");
        ofstream output_v_stream(output_v_path);
        if (!output_v_stream.is_open()) {
            *status_or_num_components = JVC_INTERNAL_ERROR; // Override, as file write failed
            return 1; // Error
        }
        for (const string &line : v_lines) output_v_stream << line << endl;
        output_v_stream.close();
        return 0; // Success (even though UNSAT)
    }

    std::vector<ConstraintInternalInfo> effective_constraints_for_processing;
    for (const auto &info : all_constraints_info_orig) {
        if (!(info.determined_wire_value.has_value() && info.determined_wire_value.value())) {
            // Include if not const 1'b1. Const 1'b0s were handled by result_is_const_zero.
            // Or if it is const 1'b0, it should be included to make the product 0.
            // Re-check: if result_is_const_zero is false, no constraint is const 1'b0.
            // So effective_constraints only filters out const 1'b1.
            effective_constraints_for_processing.push_back(info);
        }
    }
    
    // If all constraints were true or no constraints, result is true.
    // effective_constraints_for_processing will be empty. group_constraints_by_dsu will return empty.
    // This case is handled as a single component problem.

    std::vector<ComponentGenerationInfo> components = 
        group_constraints_by_dsu(effective_constraints_for_processing, variable_list_json);

    if (components.size() <= SPLIT_COMPONENTS_THRESHOLD || components.empty()) { // Includes components.size() == 0 or 1
        // --- SINGLE VERILOG FILE ---
        *status_or_num_components = JVC_SUCCESS_SINGLE_FILE;
        vector<string> v_lines;
        v_lines.push_back("module from_json(");
        bool first_var = true;
        for (const auto &var_json : variable_list_json) {
            if (!var_json.contains("name") || !var_json["name"].is_string() ||
                !var_json.contains("bit_width") || !var_json["bit_width"].is_number())
                continue; // Skip malformed
            string name = var_json.value("name", "default_var_name");
            int bit_width = var_json.value("bit_width", 1);
            if (!first_var) v_lines.back() += ","; // Add comma to previous line
            v_lines.push_back("     input wire [" + to_string(max(0, bit_width - 1)) + ":0] " + name);
            first_var = false;
        }
        if (!first_var) v_lines.back() += ",";
        v_lines.push_back("     output wire result");
        v_lines.push_back(");");

        for (const auto &info : all_constraints_info_orig) { // Declare all original wires
            v_lines.push_back("     wire " + info.assigned_wire_name + ";");
            if (info.determined_wire_value.has_value()) {
                v_lines.push_back("     assign " + info.assigned_wire_name + " = " +
                                  (info.determined_wire_value.value() ? "1'b1;" : "1'b0;"));
            } else {
                v_lines.push_back("     assign " + info.assigned_wire_name + " = |(" + info.verilog_expression_body + ");");
            }
        }
        
        std::vector<std::string> final_ordered_result_wire_names = 
            get_ordered_wires_for_single_verilog(effective_constraints_for_processing);

        string result_assign = "     assign result = ";
        if (final_ordered_result_wire_names.empty()) { // All constraints were const true, or no effective constraints
            result_assign += "1'b1;";
        } else {
            for (size_t i = 0; i < final_ordered_result_wire_names.size(); ++i) {
                result_assign += final_ordered_result_wire_names[i];
                result_assign += (i == final_ordered_result_wire_names.size() - 1) ? ";" : " & ";
            }
        }
        v_lines.push_back(result_assign);
        v_lines.push_back("endmodule");

        filesystem::path output_v_path = filesystem::path(output_dir_for_files) / (base_file_name_for_outputs_no_ext + ".v");
        ofstream output_v_stream(output_v_path);
        if (!output_v_stream.is_open()) {
            *status_or_num_components = JVC_INTERNAL_ERROR;
            return 1;
        }
        for (const string &line : v_lines) output_v_stream << line << endl;
        output_v_stream.close();

    } else {
        // --- SPLIT INTO MULTIPLE VERILOG FILES ---
        *status_or_num_components = components.size();
        json mapping_json_output;
        mapping_json_output["original_variable_list"] = variable_list_json;
        mapping_json_output["components"] = json::array();

        for (const auto& comp_gen_info : components) {
            int comp_idx = comp_gen_info.component_id_num;
            string comp_base_name = base_file_name_for_outputs_no_ext + "_comp_" + to_string(comp_idx);

            // Write mini-JSON for this component's variable list
            json comp_mini_json;
            comp_mini_json["variable_list"] = comp_gen_info.component_specific_variable_list_json;
            filesystem::path comp_json_path = filesystem::path(output_dir_for_files) / (comp_base_name + ".json");
            ofstream comp_json_stream(comp_json_path);
            if (!comp_json_stream.is_open()) { /* error */ *status_or_num_components = JVC_INTERNAL_ERROR; return 1;}
            comp_json_stream << comp_mini_json.dump(4) << endl;
            comp_json_stream.close();

            // Generate Verilog for this component
            vector<string> v_lines_comp;
            string module_name = "from_json_comp_" + to_string(comp_idx);
            v_lines_comp.push_back("module " + module_name + "(");
            bool first_var_comp = true;
            for(const auto& var_entry_json : comp_gen_info.component_specific_variable_list_json){
                string var_name = var_entry_json.value("name", "default_comp_var");
                int bit_width = var_entry_json.value("bit_width",1);
                if(!first_var_comp) v_lines_comp.back() += ",";
                v_lines_comp.push_back("    input wire [" + to_string(max(0,bit_width-1)) + ":0] " + var_name);
                first_var_comp = false;
            }
            if(!first_var_comp) v_lines_comp.back() += ",";
            v_lines_comp.push_back("    output wire result_comp_" + to_string(comp_idx));
            v_lines_comp.push_back(");");

            std::vector<std::string> comp_constraint_wire_names;
            for (const auto& cnstr_info : comp_gen_info.constraints_in_group) {
                v_lines_comp.push_back("    wire " + cnstr_info.assigned_wire_name + ";");
                 if (cnstr_info.determined_wire_value.has_value()) {
                    v_lines_comp.push_back("    assign " + cnstr_info.assigned_wire_name + " = " +
                                      (cnstr_info.determined_wire_value.value() ? "1'b1;" : "1'b0;"));
                } else {
                    v_lines_comp.push_back("    assign " + cnstr_info.assigned_wire_name + " = |(" + cnstr_info.verilog_expression_body + ");");
                }
                comp_constraint_wire_names.push_back(cnstr_info.assigned_wire_name);
            }
            
            string comp_result_assign = "    assign result_comp_" + to_string(comp_idx) + " = ";
            if (comp_constraint_wire_names.empty()) {
                comp_result_assign += "1'b1;"; // Component is trivially true
            } else {
                for (size_t i = 0; i < comp_constraint_wire_names.size(); ++i) {
                    comp_result_assign += comp_constraint_wire_names[i];
                    comp_result_assign += (i == comp_constraint_wire_names.size() - 1) ? ";" : " & ";
                }
            }
            v_lines_comp.push_back(comp_result_assign);
            v_lines_comp.push_back("endmodule");

            filesystem::path comp_v_path = filesystem::path(output_dir_for_files) / (comp_base_name + ".v");
            ofstream comp_v_stream(comp_v_path);
            if (!comp_v_stream.is_open()) { /* error */ *status_or_num_components = JVC_INTERNAL_ERROR; return 1;}
            for (const string &line : v_lines_comp) comp_v_stream << line << endl;
            comp_v_stream.close();

            // Add to mapping JSON
            json comp_map_entry;
            comp_map_entry["component_id"] = comp_idx;
            comp_map_entry["verilog_file"] = comp_base_name + ".v";
            comp_map_entry["input_json_file"] = comp_base_name + ".json";
            comp_map_entry["aig_file_stub"] = comp_base_name; // For .aig and _result.json
            comp_map_entry["variable_names"] = comp_gen_info.original_variable_names_in_group;
            mapping_json_output["components"].push_back(comp_map_entry);
        }
        
        filesystem::path mapping_json_path = filesystem::path(output_dir_for_files) / (base_file_name_for_outputs_no_ext + ".mapping.json");
        ofstream mapping_stream(mapping_json_path);
        if (!mapping_stream.is_open()) { /* error */ *status_or_num_components = JVC_INTERNAL_ERROR; return 1; }
        mapping_stream << mapping_json_output.dump(4) << endl;
        mapping_stream.close();
    }

    return 0; // Success
}