#ifndef SOLVER_FUNCTIONS_H
#define SOLVER_FUNCTIONS_H

#include <string>
#include <vector>
#include "nlohmann/json.hpp" // 包含 nlohmann/json 头文件

// 声明辅助函数：将变量值转换为指定位宽的十六进制字符串
std::string to_hex_string(unsigned long long value, int bit_width);

// 声明主 BDD 求解器函数
// original_json_path 用于在 BDD 求解器中获取变量的原始信息（如位宽和ID）
int aig_to_bdd_solver(const std::string &aig_file_path, const std::string &original_json_path, int num_samples, const std::string &result_json_path, unsigned int random_seed);

// 声明 JSON 到 Verilog 转换函数
int json_v_converter(const std::string &input_json_path, const std::string &output_v_dir);

#endif // SOLVER_FUNCTIONS_H