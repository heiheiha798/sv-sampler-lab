#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "solver_functions.h" // 包含 aig_to_bdd_solver 的声明

int json_v_converter(const std::string &input_json_path,
                     const std::string &output_v_dir);

using namespace std;
using namespace std::filesystem;

// 主函数：根据参数数量选择执行模式
int main(int argc, char *argv[]) {
    // 模式 1: JSON 到 Verilog 转换
    if (argc == 4 && string(argv[1]) == "json-to-v") {
        string input_json_path_str = argv[2];
        string output_v_dir_str = argv[3];

        return json_v_converter(input_json_path_str, output_v_dir_str);
    }
    // 模式 2: AIG 到 BDD 求解
    else if (argc == 7 && string(argv[1]) == "aig-to-bdd") {
        string aig_file_path = argv[2];
        string original_json_path = argv[3];
        int num_samples = stoi(argv[4]);
        string result_json_path = argv[5];
        unsigned int random_seed = stoul(argv[6]);

        return aig_to_bdd_solver(aig_file_path, original_json_path, num_samples,
                                 result_json_path, random_seed);
    }
    // 参数数量或模式标志不正确
    else {
        cerr << "Usage (JSON to Verilog): " << argv[0]
             << " json-to-v <input_json_path> <output_v_dir>" << endl;
        cerr << "Usage (AIG to BDD):     " << argv[0]
             << " aig-to-bdd <aig_file_path> <original_json_path> "
                "<num_samples> <result_json_path> <random_seed>"
             << endl;
        return 1; // 返回非零值表示错误
    }
}
