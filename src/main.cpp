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

    // 使用你指定的 Yosys 脚本内容
    // 注意：这里直接将你的脚本内容嵌入到字符串中。
    // Yosys 命令中的分号是重要的，它们分隔了不同的 Yosys 内部命令。
    // 我们需要确保Verilog文件名和AIG文件名被正确地插入。
    std::string yosys_script_template =
        "read_verilog \"{GENERATED_V_FILE}\"; " // {GENERATED_V_FILE} 是占位符
        "synth -auto-top; "
        "abc; "
        "aigmap; "
        "opt; "
        "clean; "
        "write_aiger -ascii \"{OUTPUT_AIG_FILE}\";"; // {OUTPUT_AIG_FILE} 是占位符

    // 替换占位符
    std::string yosys_script = yosys_script_template;
    size_t pos;

    // 替换 Verilog 文件占位符
    pos = yosys_script.find("{GENERATED_V_FILE}");
    if (pos != std::string::npos) {
        yosys_script.replace(pos, std::string("{GENERATED_V_FILE}").length(), verilog_file);
    } else {
        std::cerr << "Error: Placeholder {GENERATED_V_FILE} not found in Yosys script template." << std::endl;
        return 1; // Or handle error appropriately
    }

    // 替换 AIG 文件占位符
    pos = yosys_script.find("{OUTPUT_AIG_FILE}");
    if (pos != std::string::npos) {
        yosys_script.replace(pos, std::string("{OUTPUT_AIG_FILE}").length(), aig_file);
    } else {
        std::cerr << "Error: Placeholder {OUTPUT_AIG_FILE} not found in Yosys script template." << std::endl;
        return 1; // Or handle error appropriately
    }
    
    // 构建完整的Yosys命令，并将标准输出和标准错误重定向到日志文件
    std::string yosys_command = yosys_executable + " -p \"" + yosys_script + "\" > \"" + yosys_log_file + "\" 2>&1";


    // std::cout << "Executing Yosys: " << yosys_command << std::endl; // 用于调试
    // std::cout << "Yosys log will be in: " << yosys_log_file << std::endl; // 用于调试
    int ret = system(yosys_command.c_str()); // 执行Yosys命令

    if (ret != 0) // 如果Yosys执行失败
    {
        // 打印错误信息和日志文件路径
        std::cerr << "Yosys failed with exit code " << ret << " for " << verilog_file << std::endl;
        std::cerr << "Check Yosys log for details: " << yosys_log_file << std::endl;
        // 尝试读取并打印Yosys日志文件的内容
        std::ifstream log_stream(yosys_log_file);
        if (log_stream.is_open())
        {
            std::cerr << "--- Yosys Log Start ---" << std::endl;
            std::string line_content; // Renamed to avoid conflict with using std::line
            while (getline(log_stream, line_content))
            {
                std::cerr << line_content << std::endl;
            }
            std::cerr << "--- Yosys Log End ---" << std::endl;
            log_stream.close();
        }
        else
        {
            std::cerr << "Could not open Yosys log file: " << yosys_log_file << std::endl;
        }
    }
    return ret; // 返回Yosys的退出码
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

            int samples_per_component = 0;
            if (num_components > 0 && num_total_samples > 0)
            {
                samples_per_component = static_cast<int>(ceil(pow(static_cast<double>(num_total_samples), 1.0 / static_cast<double>(num_components))));
                if (samples_per_component == 0) samples_per_component = 1; // Ensure at least 1 if total > 0
                samples_per_component += 50; // Your requested safeguard
            } else if (num_total_samples == 0) { // If 0 samples requested overall
                samples_per_component = 0;
            }


            cout << "Requesting " << samples_per_component << " samples per component." << endl;

            bool any_component_unsat = false;
            bool break_early_on_unsat = true; // Set to false if you want to process all components even if one is UNSAT

            for (const auto &comp_info : map_contents.components)
            {
                cout << "Processing component " << comp_info.component_id << " (Verilog: " << comp_info.verilog_file << ")" << endl;
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
                                                samples_per_component, comp_result_json.string(), comp_random_seed);
                
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