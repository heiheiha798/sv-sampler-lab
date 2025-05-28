#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>            // For system()
#include <cmath>              // For ceil, pow
#include <fstream>            // For ifstream, ofstream
#include "solver_functions.h" // Includes JVC_ constants
#include "nlohmann/json.hpp"  // For quickly writing UNSAT result

using namespace std;
using namespace std::filesystem;
using json = nlohmann::json;

// Helper to call Yosys
int run_yosys(const string &yosys_executable, const string &verilog_file, const string &aig_file)
{
    string yosys_log_file = aig_file + ".yosys.log"; // Create a log file name
    // Using the robust Yosys script
    std::string yosys_script = "read_verilog " + verilog_file +
                             "; hierarchy -check; proc; opt; memory_dff; opt; fsm; opt" +
                             "; techmap; opt" +
                             "; abc -g AND; opt" +
                             "; aigmap" +
                             "; write_aiger -ascii " + aig_file +
                             ";";
    std::string yosys_command = yosys_executable + " -p \"" + yosys_script + "\" > \"" + yosys_log_file + "\" 2>&1";


    // std::cout << "Executing Yosys: " << yosys_command << std::endl;
    // std::cout << "Yosys log will be in: " << yosys_log_file << std::endl;
    int ret = system(yosys_command.c_str());

    if (ret != 0)
    {
        std::cerr << "Yosys failed with exit code " << ret << " for " << verilog_file << std::endl;
        std::cerr << "Check Yosys log for details: " << yosys_log_file << std::endl;
        std::ifstream log_stream(yosys_log_file);
        if (log_stream.is_open())
        {
            std::cerr << "--- Yosys Log Start ---" << std::endl;
            std::string line;
            while (getline(log_stream, line))
            {
                std::cerr << line << std::endl;
            }
            std::cerr << "--- Yosys Log End ---" << std::endl;
            log_stream.close();
        }
        else
        {
            std::cerr << "Could not open Yosys log file: " << yosys_log_file << std::endl;
        }
    }
    return ret;
}

int main(int argc, char *argv[])
{
    if (argc == 4 && string(argv[1]) == "json-to-v")
    {
        string input_json_path_str = argv[2];
        string output_v_dir_str = argv[3];
        std::string base_name;
        int status;
        int jvc_ret = json_v_converter(input_json_path_str, output_v_dir_str, base_name, &status);
        if (jvc_ret != 0 || (status != JVC_SUCCESS_SINGLE_FILE && status != JVC_UNSAT_PREPROCESSED))
        {
            cerr << "json-to-v mode encountered an issue with json_v_converter new logic or an error." << endl;
            return 1;
        }
        return 0;
    }
    else if (argc == 7 && string(argv[1]) == "aig-to-bdd")
    {
        string aig_file_path = argv[2];
        string original_json_path = argv[3];
        int num_samples = stoi(argv[4]);
        string result_json_path = argv[5];
        unsigned int random_seed = stoul(argv[6]);
        return aig_to_bdd_solver(aig_file_path, original_json_path, num_samples,
                                 result_json_path, random_seed);
    }
    else if (argc == 7 && string(argv[1]) == "solve-decomposed")
    {
        string constraint_json_abs_str = argv[2];
        int num_total_samples = stoi(argv[3]);
        string run_dir_abs_str = argv[4];
        unsigned int random_seed = stoul(argv[5]);
        string yosys_executable = argv[6];

        std::string base_name_no_ext;
        int jvc_status_or_num_comp;

        int jvc_ret = json_v_converter(constraint_json_abs_str, run_dir_abs_str,
                                       base_name_no_ext, &jvc_status_or_num_comp);

        if (jvc_ret != 0)
        {
            cerr << "json_v_converter failed with file/parse error." << endl;
            return 1;
        }

        std::filesystem::path run_dir_path(run_dir_abs_str);
        std::filesystem::path final_result_json_path = run_dir_path / "result.json";

        if (jvc_status_or_num_comp == JVC_UNSAT_PREPROCESSED)
        {
            cout << "Pipeline determined the problem is UNSAT during json_v_converter preprocessing." << endl;
            json unsat_json_output;
            unsat_json_output["assignment_list"] = json::array();
            ofstream ofs(final_result_json_path.string());
            if (!ofs.is_open())
            {
                cerr << "Failed to write UNSAT result.json" << endl;
                return 1;
            }
            ofs << unsat_json_output.dump(4) << endl;
            ofs.close();
            return 2; // Special exit code for preprocessor UNSAT
        }
        else if (jvc_status_or_num_comp == JVC_INTERNAL_ERROR)
        {
            cerr << "json_v_converter failed with internal error." << endl;
            return 1;
        }
        else if (jvc_status_or_num_comp == JVC_SUCCESS_SINGLE_FILE)
        {
            cout << "json_v_converter produced a single Verilog file." << endl;
            path verilog_file = run_dir_path / (base_name_no_ext + ".v");
            path aig_file = run_dir_path / (base_name_no_ext + ".aig");

            if (run_yosys(yosys_executable, verilog_file.string(), aig_file.string()) != 0)
            {
                return 1;
            }
            // For single file, aig_to_bdd_solver writes directly to final_result_json_path
            return aig_to_bdd_solver(aig_file.string(), constraint_json_abs_str, num_total_samples,
                                     final_result_json_path.string(), random_seed);
        }
        else
        { // jvc_status_or_num_comp = k > 1 (split)
            int num_components = jvc_status_or_num_comp;
            cout << "json_v_converter produced " << num_components << " component Verilog files." << endl;

            path mapping_file_path = run_dir_path / (base_name_no_ext + ".mapping.json");
            MappingFileContents map_contents;
            if (!load_mapping_file(mapping_file_path.string(), map_contents))
            {
                cerr << "Failed to load mapping file: " << mapping_file_path << endl;
                return 1;
            }

            map_contents.num_total_samples_requested = num_total_samples;

            // 新的动态采样策略
            vector<int> samples_per_component_list(num_components);
            if (num_total_samples == 0) {
                // 如果总采样数为0，所有组件都采样0次
                fill(samples_per_component_list.begin(), samples_per_component_list.end(), 0);
            } else {
                // 计算初始的每组件采样数（作为起始值）
                int base_samples = static_cast<int>(ceil(pow(static_cast<double>(num_total_samples), 1.0 / static_cast<double>(num_components))));
                if (base_samples == 0) base_samples = 1;
                
                // 动态分配策略
                int remaining_target = num_total_samples;
                int remaining_components = num_components;
                
                for (int i = 0; i < num_components; ++i) {
                    if (remaining_components == 1) {
                        // 最后一个组件，采样剩余需要的数量（但至少1个）
                        samples_per_component_list[i] = max(1, remaining_target);
                    } else {
                        // 计算当前组件应该采样的数量
                        // 策略：尽量多采样，但要为后续组件留一些空间
                        int max_for_current = min(base_samples + 100, remaining_target / remaining_components * 2);
                        samples_per_component_list[i] = max(1, max_for_current);
                        
                        // 更新剩余目标（假设当前组件能产生所需样本）
                        remaining_target = max(1, remaining_target / samples_per_component_list[i]);
                        remaining_components--;
                    }
                }
            }

            cout << "Dynamic sampling strategy: ";
            for (int i = 0; i < num_components; ++i) {
                cout << "Component " << i << ": " << samples_per_component_list[i] << " samples";
                if (i < num_components - 1) cout << ", ";
            }
            cout << endl;

            bool any_component_unsat = false;
            bool break_early_on_unsat = true;
            int component_index = 0;

            for (const auto &comp_info : map_contents.components)
            {
                cout << "Processing component " << comp_info.component_id << " (Verilog: " << comp_info.verilog_file << ")" << endl;
                
                // 使用动态计算的采样数
                int current_samples_needed = samples_per_component_list[component_index];
                cout << "Requesting " << current_samples_needed << " samples for this component." << endl;
                
                path comp_v_file = run_dir_path / comp_info.verilog_file;
                path comp_aig_file = run_dir_path / (comp_info.aig_file_stub + ".aig");
                path comp_input_json = run_dir_path / comp_info.input_json_file;
                path comp_result_json = run_dir_path / (comp_info.aig_file_stub + "_result.json");

                if (run_yosys(yosys_executable, comp_v_file.string(), comp_aig_file.string()) != 0)
                {
                    cerr << "Yosys failed for component " << comp_info.component_id << ". Aborting." << endl;
                    return 1;
                }

                unsigned int comp_random_seed = random_seed + comp_info.component_id;

                int bdd_ret = aig_to_bdd_solver(comp_aig_file.string(), comp_input_json.string(),
                                                current_samples_needed, comp_result_json.string(), comp_random_seed);

                // After BDD solver, check the result file status
                if (!std::filesystem::exists(comp_result_json)) {
                     cerr << "CRITICAL: BDD solver call completed for component " << comp_info.component_id 
                          << " (returned " << bdd_ret << ") but result file NOT CREATED: " 
                          << comp_result_json.string() << ". Aborting." << endl;
                     return 1; 
                }

                bool current_component_is_unsat = false;
                ifstream comp_res_ifs(comp_result_json.string());
                if (comp_res_ifs.is_open()) {
                    json comp_res_data;
                    try { 
                        comp_res_ifs >> comp_res_data; 
                        if (comp_res_data.contains("assignment_list") && 
                            comp_res_data["assignment_list"].is_array() && 
                            comp_res_data["assignment_list"].empty()) {
                            current_component_is_unsat = true;
                        }
                    } catch (const json::parse_error& e) {
                        cerr << "Warning: Could not parse result file for component " << comp_info.component_id 
                             << ": " << comp_result_json.string() << " (" << e.what() << "). Assuming not UNSAT for now." << endl;
                        // Decide how to handle parse error - treat as error or non-UNSAT?
                        // For now, let's not assume it's UNSAT if we can't parse it.
                    }
                    comp_res_ifs.close();
                } else {
                     // This case should ideally be caught by the exists() check above, but as a safeguard:
                     cerr << "CRITICAL: Could not open result file for component " << comp_info.component_id 
                          << " after BDD solver: " << comp_result_json.string() << ". Aborting." << endl;
                     return 1;
                }

                if (current_component_is_unsat) {
                    cout << "Component " << comp_info.component_id << " was UNSAT." << endl;
                    any_component_unsat = true;
                    if (break_early_on_unsat) {
                        break; 
                    }
                } else if (bdd_ret != 0) {
                    // BDD solver returned an error, AND the component was not determined to be UNSAT by an empty list
                    cerr << "aig_to_bdd_solver FAILED for component " << comp_info.component_id 
                         << " with code " << bdd_ret << " (and not clearly UNSAT). Aborting." << endl;
                    return 1; 
                }
                cout << "Component " << comp_info.component_id << " processed." << endl;
            } // End of for loop processing components

            if (any_component_unsat)
            {
                cout << "Overall problem is UNSAT due to one or more UNSAT components." << endl;
                json unsat_json_output;
                unsat_json_output["assignment_list"] = json::array();
                ofstream ofs(final_result_json_path.string());
                if (!ofs.is_open())
                {
                    cerr << "Failed to write final UNSAT result.json" << endl;
                    return 1;
                }
                ofs << unsat_json_output.dump(4) << endl;
                ofs.close();
                return 0; // Successfully determined UNSAT
            }
            else
            {
                cout << "All processed components are individually SAT (or not processed due to early UNSAT break). Merging results..." << endl;
                int merge_ret = merge_results(mapping_file_path.string(), run_dir_abs_str,
                                              num_total_samples, final_result_json_path.string(), random_seed);
                if (merge_ret != 0) {
                    cerr << "Error during results merging." << endl;
                    return 1;
                }
                return 0; // Successfully merged
            }
        }
    }
    else
    {
        cout << "Usage: " << endl;
        cout << "  " << argv[0] << " json-to-v <input_json_path> <output_v_dir>" << endl;
        cout << "  " << argv[0] << " aig-to-bdd <aig_file_path> <original_json_path> <num_samples> <result_json_path> <random_seed>" << endl;
        cout << "  " << argv[0] << " solve-decomposed <constraint_json_path> <num_samples> <run_output_dir> <random_seed> <yosys_executable_path>" << endl;
        return 1;
    }
    return 0;
}