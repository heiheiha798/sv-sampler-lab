// #include "solver_functions.h"

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <cstdio>
// #include <cstdlib>
// #include <algorithm>
// #include <iomanip>
// #include <sstream>
// #include <map>

// #include "cudd.h"

// using json = nlohmann::json;
// using namespace std;

// // 实现辅助函数：将变量值转换为指定位宽的十六进制字符串
// string to_hex_string(unsigned long long value, int bit_width)
// {
//     if (bit_width <= 0)
//         return "";
//     // 计算所需的十六进制字符数 (每个十六进制字符代表 4 比特)
//     int hex_chars = (bit_width + 3) / 4;
//     stringstream ss;
//     ss << hex << setfill('0') << setw(hex_chars) << value;
//     string hex_str = ss.str();
//     // 确保字符串长度不超过位宽所需的十六进制字符数
//     if (hex_str.length() > hex_chars)
//     {
//         hex_str = hex_str.substr(hex_str.length() - hex_chars);
//     }
//     return hex_str;
// }

// // 实现主 BDD 求解器函数
// int aig_to_bdd_solver(const string &aig_file_path, const string &original_json_path, int num_samples, const string &result_json_path, unsigned int random_seed)
// {
//     DdManager *manager;
//     DdNode *bdd_circuit_output = nullptr;
//     int nM = 0, nI = 0, nL = 0, nO = 0, nA = 0;

//     // 1. 初始化 CUDD 管理器
//     manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
//     if (!manager)
//     {
//         cerr << "Error: CUDD manager initialization failed." << endl;
//         return 1;
//     }

//     // 设置随机种子
//     Cudd_Srandom(manager, random_seed);

//     // --- 手动 AIGER 解析和 BDD 构建 (取代 Cudd_ReadAiger) ---
//     // 这是一个简化的框架，您需要根据 AIGER 格式规范 (例如 aag M I L O A) 来填充完整的解析逻辑。
//     // AIGER 格式描述：https://github.com/arminbiere/aiger/blob/master/FORMAT

//     map<int, DdNode *> literal_to_bdd_map; // 映射 AIGER 文字到其对应的 BDD 节点 (AIGER literals are 2*idx for positive, 2*idx+1 for negative)

//     ifstream aig_file_stream(aig_file_path);
//     if (!aig_file_stream.is_open())
//     {
//         cerr << "Error: Could not open AIG file: " << aig_file_path << endl;
//         Cudd_Quit(manager);
//         return 1;
//     }

//     string line;
//     // 1. 解析 AIGER 头信息 (aag M I L O A)
//     if (!getline(aig_file_stream, line) || line.substr(0, 3) != "aag")
//     {
//         cerr << "Error: Invalid or empty AIGER file (header)." << endl;
//         aig_file_stream.close();
//         Cudd_Quit(manager);
//         return 1;
//     }
//     stringstream header_ss(line.substr(4));
//     if (!(header_ss >> nM >> nI >> nL >> nO >> nA))
//     {
//         cerr << "Error: Malformed AIGER header values." << endl;
//         aig_file_stream.close();
//         Cudd_Quit(manager);
//         return 1;
//     }

//     if (nL != 0)
//     {
//         cerr << "Warning: AIGER file contains latches (nL > 0), this solver only handles combinational circuits." << endl;
//     }
//     if (nO == 0)
//     {
//         cerr << "Error: AIGER file has no outputs." << endl;
//         aig_file_stream.close();
//         Cudd_Quit(manager);
//         return 1;
//     }
//     // 注意：如果 nO > 1，这里只处理第一个输出。您的工具通常只生成一个输出。

//     cout << "AIGER Header: M=" << nM << " I=" << nI << " L=" << nL << " O=" << nO << " A=" << nA << endl;

//     // 2. 创建输入变量的 BDD 节点
//     // CUDD 变量索引从 0 开始。AIGER 文字的输入变量索引通常从 1 开始。
//     // AIGER 文字：2*var_index for positive, 2*var_index + 1 for negative
//     // 同时存储输入变量的 BDD 节点到单独的数组中，用于 Cudd_bddPickOneMinterm
//     vector<DdNode *> input_vars_bdd(nI);
//     for (int i = 0; i < nI; ++i)
//     {
//         DdNode *var_node = Cudd_bddNewVar(manager);               // CUDD 会自动分配变量索引 (0, 1, 2...)
//         literal_to_bdd_map[2 * (i + 1)] = var_node;               // 存储肯定文字的 BDD
//         literal_to_bdd_map[2 * (i + 1) + 1] = Cudd_Not(var_node); // 存储否定文字的 BDD
//         Cudd_Ref(var_node);                                       // 增加引用计数
//         Cudd_Ref(Cudd_Not(var_node));                             // 增加引用计数
//         input_vars_bdd[i] = var_node;                             // 存储到输入变量数组
//     }

//     // 3. 处理输出文字 (O 行) - 通常在 AND 门之前，但 AIGER 格式允许在任意位置。这里为了简化，假设它们在输入之后。
//     vector<int> output_literals(nO);
//     for (int i = 0; i < nO; ++i)
//     {
//         if (!getline(aig_file_stream, line))
//         {
//             cerr << "Error: Malformed AIGER file (missing output literal)." << endl;
//             aig_file_stream.close();
//             Cudd_Quit(manager);
//             return 1;
//         }
//         if (sscanf(line.c_str(), "%d", &output_literals[i]) != 1)
//         {
//             cerr << "Error: Failed to parse output literal." << endl;
//             aig_file_stream.close();
//             Cudd_Quit(manager);
//             return 1;
//         }
//     }

//     // 4. 处理 AND 门 (A 行)
//     // AIGER AND 门通常以差分编码定义。这是最复杂的部分。
//     // AIGER text format for AND gates usually lists absolute literals
//     // Example: `10 8 9` means literal 10 is AND of literals 8 and 9.
//     // The AIGER format linked in README (https://github.com/arminbiere/aiger/blob/master/FORMAT) specifies:
//     // An AIGER file contains 5 numbers, M I L O A, then lines for inputs, latches, outputs, and then AND gates.
//     // AND gates are defined as `lhs_literal rhs1_literal rhs2_literal` (absolute values).

//     for (int i = 0; i < nA; ++i)
//     {
//         if (!getline(aig_file_stream, line))
//         {
//             cerr << "Error: Malformed AIGER file (missing AND gate line)." << endl;
//             aig_file_stream.close();
//             Cudd_Quit(manager);
//             return 1;
//         }
//         stringstream ss(line);
//         int output_lit, input1_lit, input2_lit;
//         // AIGER text format for AND gates usually lists absolute literals
//         if (!(ss >> output_lit >> input1_lit >> input2_lit))
//         {
//             cerr << "Error: Failed to parse AND gate line: " << line << endl;
//             aig_file_stream.close();
//             Cudd_Quit(manager);
//             return 1;
//         }

//         // 获取输入文字对应的 BDD 节点
//         // 检查文字是否存在于 map 中
//         auto it1 = literal_to_bdd_map.find(input1_lit);
//         auto it2 = literal_to_bdd_map.find(input2_lit);

//         if (it1 == literal_to_bdd_map.end() || it2 == literal_to_bdd_map.end())
//         {
//             cerr << "Error: AIGER parsing failed for AND gate (" << output_lit << "): input literal " << input1_lit << " or " << input2_lit << " not found in map." << endl;
//             // Clean up existing nodes in map before exiting
//             // Rely on Cudd_Quit to clean up nodes with ref count > 0
//             aig_file_stream.close();
//             Cudd_Quit(manager);
//             return 1;
//         }

//         DdNode *input1_bdd = it1->second;
//         DdNode *input2_bdd = it2->second;

//         // 计算 AND 门对应的 BDD
//         DdNode *and_node = Cudd_bddAnd(manager, input1_bdd, input2_bdd);
//         Cudd_Ref(and_node); // 增加引用计数，因为要存入 map

//         literal_to_bdd_map[output_lit] = and_node;
//         literal_to_bdd_map[output_lit + 1] = Cudd_Not(and_node); // 存储否定形式
//         Cudd_Ref(Cudd_Not(and_node));                            // 增加否定形式的引用计数
//     }
//     aig_file_stream.close(); // Close the AIGER file stream.

//     // 5. 获取最终输出的 BDD 节点 (第一个输出)
//     if (nO > 0 && literal_to_bdd_map.count(output_literals[0]))
//     {
//         bdd_circuit_output = literal_to_bdd_map[output_literals[0]];
//         Cudd_Ref(bdd_circuit_output); // 增加引用计数，因为这是最终结果，在 Cudd_Quit 前不释放
//     }
//     else
//     {
//         cerr << "Error: Final output BDD node not found in map or no outputs defined." << endl;
//         // Rely on Cudd_Quit to clean up nodes with ref count > 0
//         Cudd_Quit(manager);
//         return 1;
//     }

//     // 释放 literal_to_bdd_map 中所有 BDD 节点的引用计数，除了最终的 bdd_circuit_output
//     // 在 CUDD 中，当你将一个节点存储到 map 中并 Cudd_Ref 它，当不再需要时，需要 Cudd_RecursiveDeref
//     // 这里的管理要特别小心，避免双重释放或内存泄漏。
//     // 简单的做法是：在 Cudd_Init() 后，将所有 BDD 节点注册到 manager，在 Cudd_Quit() 时管理器会全部释放。
//     // 但如果需要手动管理引用计数，确保每个 Cudd_Ref 都有一个对应的 Cudd_RecursiveDeref。
//     // For now, let's rely on CUDD's garbage collection at Cudd_Quit for nodes in the map,
//     // as they were Cudd_Ref'd when inserted.

//     cout << "AIG 文件解析成功，正在构建 BDD 并生成解..." << endl;

//     // 4. 开启 CUDD 动态变量重排 (可选，但强烈推荐用于性能优化)
//     Cudd_AutodynEnable(manager, CUDD_REORDER_SIFT);

//     // 5. 采样并生成解
//     json result_json;
//     json assignment_list = json::array();

//     // 读取原始 JSON 获取变量位宽信息，以便正确格式化输出
//     json original_data;
//     ifstream original_json_stream(original_json_path);
//     if (original_json_stream.is_open())
//     {
//         original_json_stream >> original_data;
//         original_json_stream.close();
//     }
//     else
//     {
//         cerr << "Warning: Could not open original JSON for variable info: " << original_json_path << endl;
//     }
//     json original_variable_list = original_data.contains("variable_list") ? original_data["variable_list"] : json::array();

//     // 将 input_vars_bdd 转换为 DdNode** 数组，用于 Cudd_bddPickOneMinterm
//     DdNode **input_vars_array = nullptr;
//     if (nI > 0)
//     {
//         input_vars_array = new DdNode *[nI];
//         for (int i = 0; i < nI; ++i)
//         {
//             input_vars_array[i] = input_vars_bdd[i];
//         }
//     }

//     for (int i = 0; i < num_samples; ++i)
//     {
//         // Cudd_bddPickOneMinterm 返回一个 minterm (一个完整的赋值)
//         // 修正：传递 input_vars_array 和 nI 作为参数
//         DdNode *minterm_node = Cudd_bddPickOneMinterm(manager, bdd_circuit_output, input_vars_array, nI);

//         if (!minterm_node)
//         {
//             cerr << "警告: 未能找到更多满足条件的解。" << endl;
//             if (i == 0) // 如果一个解都找不到
//             {
//                 assignment_list = json::array(); // 返回空数组
//             }
//             break; // 停止采样
//         }

//         json assignment_entry = json::array();
//         int current_cudd_var_idx = 0; // 追踪当前处理的 CUDD 变量索引

//         // 遍历原始 JSON 中的变量列表，提取每个变量的赋值
//         for (const auto &var_info : original_variable_list)
//         {
//             if (!var_info.contains("id") || !var_info["id"].is_number() ||
//                 !var_info.contains("bit_width") || !var_info["bit_width"].is_number())
//             {
//                 cerr << "Warning: Skipping malformed variable info in original JSON." << endl;
//                 continue; // 跳过格式错误的变量信息
//             }

//             int original_var_id = var_info.value("id", -1);
//             int bit_width = var_info.value("bit_width", 1);

//             unsigned long long variable_combined_value = 0;

//             // 提取该变量的每个比特的赋值
//             for (int bit_idx = 0; bit_idx < bit_width; ++bit_idx)
//             {
//                 // 检查是否超出 CUDD 输入变量总数
//                 if (current_cudd_var_idx >= nI)
//                 {
//                     cerr << "Error: Mismatch between original JSON variable count/bit width and AIGER input count (nI)." << endl;
//                     Cudd_RecursiveDeref(manager, minterm_node);       // Deref minterm node
//                     Cudd_RecursiveDeref(manager, bdd_circuit_output); // Deref final output
//                     if (input_vars_array)
//                         delete[] input_vars_array; // Free allocated memory
//                     Cudd_Quit(manager);
//                     return 1; // Exit with error
//                 }

//                 // 获取当前比特对应的 CUDD 变量的 BDD 节点
//                 DdNode *bit_bdd = Cudd_bddIthVar(manager, current_cudd_var_idx);
//                 Cudd_Ref(bit_bdd); // Ref the variable BDD

//                 unsigned long long bit_assignment = 0;

//                 // 检查该比特在 minterm 中的赋值 (使用 AND 操作替代 Cudd_bddVarIsAssigned)
//                 // 如果 minterm_node AND bit_bdd == minterm_node, 则 bit_bdd 为真 (赋值为 1)
//                 DdNode *check_one = Cudd_bddAnd(manager, minterm_node, bit_bdd);
//                 Cudd_Ref(check_one); // Ref the result

//                 if (check_one == minterm_node)
//                 {
//                     bit_assignment = 1;
//                 }
//                 else
//                 {
//                     // 如果不是 1，则检查其否定形式
//                     DdNode *not_bit_bdd = Cudd_Not(bit_bdd); // Cudd_Not does not increment ref count if node exists
//                     Cudd_Ref(not_bit_bdd);                   // Ref the negated variable BDD

//                     DdNode *check_zero = Cudd_bddAnd(manager, minterm_node, not_bit_bdd);
//                     Cudd_Ref(check_zero); // Ref the result

//                     if (check_zero == minterm_node)
//                     {
//                         bit_assignment = 0;
//                     }
//                     else
//                     {
//                         // 对于 minterm，每个变量都应该有确定的赋值 (0 或 1)
//                         cerr << "Warning: Could not determine assignment for bit " << bit_idx << " of variable with ID " << original_var_id << " (CUDD var " << current_cudd_var_idx << ") in minterm." << endl;
//                         bit_assignment = 0; // 默认赋值为 0
//                     }
//                     Cudd_RecursiveDeref(manager, check_zero);  // Deref check_zero
//                     Cudd_RecursiveDeref(manager, not_bit_bdd); // Deref not_bit_bdd
//                 }

//                 Cudd_RecursiveDeref(manager, check_one); // Deref check_one
//                 Cudd_RecursiveDeref(manager, bit_bdd);   // Deref bit_bdd

//                 // 将比特赋值组合到变量的完整值中
//                 // 假设是小端序 (LSB first)，即 bit_idx 0 是最低位
//                 variable_combined_value |= (bit_assignment << bit_idx);

//                 current_cudd_var_idx++; // 移动到下一个 CUDD 变量
//             }

//             // 将组合后的变量值格式化为十六进制字符串并添加到结果中
//             assignment_entry.push_back({{"value", to_hex_string(variable_combined_value, bit_width)}});
//         }

//         // 检查是否使用了所有 CUDD 输入变量
//         if (current_cudd_var_idx != nI)
//         {
//             cerr << "Warning: Mismatch between total bits in original JSON variables and AIGER input count (nI)." << endl;
//             // 这通常表示 JSON 中的变量定义与 AIGER 输入数量不匹配
//         }

//         assignment_list.push_back(assignment_entry);

//         Cudd_RecursiveDeref(manager, minterm_node); // 释放 minterm 节点
//     }

//     // 释放为 input_vars_array 分配的内存
//     if (input_vars_array)
//     {
//         delete[] input_vars_array;
//     }

//     result_json["assignment_list"] = assignment_list;

//     // 6. 写入结果到 JSON 文件
//     std::ofstream output_json_stream(result_json_path);
//     if (!output_json_stream.is_open())
//     {
//         std::cerr << "Error: Failed to open output JSON file: " << result_json_path << std::endl;
//         // Rely on Cudd_Quit to clean up nodes with ref count > 0
//         Cudd_RecursiveDeref(manager, bdd_circuit_output); // Dereference the final output BDD node
//         Cudd_Quit(manager);
//         return 1;
//     }
//     output_json_stream << result_json.dump(4) << std::endl; // 使用 4 空格缩进输出
//     output_json_stream.close();

//     // 7. 清理 CUDD 管理器
//     Cudd_RecursiveDeref(manager, bdd_circuit_output); // Dereference the final output BDD node
//     Cudd_Quit(manager);                               // 释放所有剩余的 BDD 节点和管理器内存
//     std::cout << "BDD 求解器完成。结果已写入到 " << result_json_path << std::endl;
//     return 0;
// }

#include "solver_functions.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>  // For sscanf
#include <cstdlib> // For stoi, stoul
#include <algorithm>
#include <iomanip> // For setw, setfill
#include <sstream> // For stringstream
#include <map>

#include "cudd.h" // CUDD library header

using json = nlohmann::json;
using namespace std;

// 实现辅助函数：将变量值转换为指定位宽的十六进制字符串
string to_hex_string(unsigned long long value, int bit_width)
{
    if (bit_width <= 0)
        return "";
    // 计算所需的十六进制字符数 (每个十六进制字符代表 4 比特)
    int hex_chars = (bit_width + 3) / 4;
    stringstream ss;
    ss << hex << setfill('0') << setw(hex_chars) << value;
    string hex_str = ss.str();
    // 确保字符串长度不超过位宽所需的十六进制字符数
    if (hex_str.length() > hex_chars)
    {
        hex_str = hex_str.substr(hex_str.length() - hex_chars);
    }
    return hex_str;
}

// 实现主 BDD 求解器函数
int aig_to_bdd_solver(const string &aig_file_path, const string &original_json_path, int num_samples, const string &result_json_path, unsigned int random_seed)
{
    DdManager *manager;
    DdNode *bdd_circuit_output = nullptr;
    int nM = 0, nI = 0, nL = 0, nO = 0, nA = 0;

    // 1. 初始化 CUDD 管理器
    manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (!manager)
    {
        cerr << "Error: CUDD manager initialization failed." << endl;
        return 1;
    }

    // *** 新增：在初始化后立即启用自动动态变量重排 ***
    // 使用 SIFT 算法，这是一种常用且效果较好的重排算法
    Cudd_AutodynEnable(manager, CUDD_REORDER_SIFT);
    cout << "Debug: Automatic dynamic variable reordering enabled (CUDD_REORDER_SIFT)." << endl;

    // 设置随机种子
    Cudd_Srandom(manager, random_seed);

    // --- 手动 AIGER 解析和 BDD 构建 ---
    map<int, DdNode *> literal_to_bdd_map;

    ifstream aig_file_stream(aig_file_path);
    if (!aig_file_stream.is_open())
    {
        cerr << "Error: Could not open AIG file: " << aig_file_path << endl;
        Cudd_Quit(manager);
        return 1;
    }

    string line;
    // 1. 解析 AIGER 头信息 (aag M I L O A)
    if (!getline(aig_file_stream, line) || line.substr(0, 3) != "aag")
    {
        cerr << "Error: Invalid or empty AIGER file (header)." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }
    stringstream header_ss(line.substr(4));
    if (!(header_ss >> nM >> nI >> nL >> nO >> nA))
    {
        cerr << "Error: Malformed AIGER header values." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }

    if (nL != 0)
    {
        cerr << "Warning: AIGER file contains latches (nL > 0), this solver only handles combinational circuits." << endl;
    }
    if (nO == 0)
    {
        cerr << "Error: AIGER file has no outputs." << endl;
        aig_file_stream.close();
        Cudd_Quit(manager);
        return 1;
    }

    cout << "AIGER Header: M=" << nM << " I=" << nI << " L=" << nL << " O=" << nO << " A=" << nA << endl;

    // 2. 创建输入变量的 BDD 节点并读取输入文字行
    vector<DdNode *> input_vars_bdd(nI);
    cout << "Debug: Reading " << nI << " input literals." << endl;
    for (int i = 0; i < nI; ++i)
    {
        if (!getline(aig_file_stream, line))
        {
            cerr << "Error: Malformed AIGER file (missing input literal line " << i + 1 << ")." << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
        int input_lit;
        if (sscanf(line.c_str(), "%d", &input_lit) != 1)
        {
            cerr << "Error: Failed to parse input literal on line: '" << line << "'" << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }

        DdNode *var_node = Cudd_bddNewVar(manager);
        if (!var_node)
        {
            cerr << "Error: CUDD_bddNewVar failed for input " << i << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }

        literal_to_bdd_map[input_lit] = var_node;
        literal_to_bdd_map[input_lit + 1] = Cudd_Not(var_node);
        Cudd_Ref(var_node);
        Cudd_Ref(Cudd_Not(var_node));
        input_vars_bdd[i] = var_node;
    }
    cout << "Debug: Finished reading " << nI << " input literals." << endl;

    // 3. 读取锁存器文字行 (跳过)
    cout << "Debug: Reading " << nL << " latch literals." << endl;
    for (int i = 0; i < nL; ++i)
    {
        if (!getline(aig_file_stream, line))
        {
            cerr << "Error: Malformed AIGER file (missing latch literal line " << i + 1 << ")." << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
    }
    cout << "Debug: Finished reading " << nL << " latch literals." << endl;

    // 4. 读取输出文字行
    vector<int> output_literals(nO);
    cout << "Debug: Reading " << nO << " output literals." << endl;
    for (int i = 0; i < nO; ++i)
    {
        if (!getline(aig_file_stream, line))
        {
            cerr << "Error: Malformed AIGER file (missing output literal line " << i + 1 << ")." << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
        cout << "Debug: Read output literal line " << i + 1 << ": '" << line << "'" << endl;
        int output_lit;
        if (sscanf(line.c_str(), "%d", &output_lit) != 1)
        {
            cerr << "Error: Failed to parse output literal on line: '" << line << "'" << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
        output_literals[i] = output_lit;
    }
    cout << "Debug: Finished reading " << nO << " output literals." << endl;

    // 调试性地读取AND门前的行（如果存在）
    if (nA > 0)
    {
        std::streampos original_pos = aig_file_stream.tellg();
        string line_before_ands_debug;
        if (getline(aig_file_stream, line_before_ands_debug))
        {
            cout << "Debug: Line read just before AND gates loop (using tellg/seekg): '" << line_before_ands_debug << "'" << endl;
            aig_file_stream.seekg(original_pos); // 将文件指针重置回读取前的位置
        }
        else
        {
            cout << "Debug: Could not read line just before AND gates loop (using tellg/seekg, might be EOF or empty file after outputs)." << endl;
        }
        aig_file_stream.clear(); // 清除由于尝试读取可能产生的eof或其他错误标志
    }

    // 5. 处理 AND 门 (A 行)
    cout << "Debug: Reading " << nA << " AND gates." << endl;
    for (int i = 0; i < nA; ++i)
    {
        if (!getline(aig_file_stream, line))
        {
            cerr << "Error: Malformed AIGER file (missing AND gate line " << i + 1 << " of " << nA << ")." << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
        // cout << "Debug: Read AND gate line " << i + 1 << ": '" << line << "'" << endl; // 这行可以按需开启，如果AND门很多会刷屏
        stringstream ss(line);
        int output_lit, input1_lit, input2_lit;
        // cout << "Debug: Attempting to parse line " << i + 1 << ": '" << line << "'" << endl; // 同上，可按需开启
        if (!(ss >> output_lit >> input1_lit >> input2_lit))
        {
            cerr << "Debug: Parsing failed for line: '" << line << "'" << endl;
            cerr << "Error: Failed to parse AND gate line: " << i + 1 << endl;
            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }
        // cout << "Debug: Successfully parsed line " << i + 1 << ": output_lit=" << output_lit << ", input1_lit=" << input1_lit << ", input2_lit=" << input2_lit << endl; // 同上

        auto it1 = literal_to_bdd_map.find(input1_lit);
        auto it2 = literal_to_bdd_map.find(input2_lit);

        if (it1 == literal_to_bdd_map.end() || it2 == literal_to_bdd_map.end())
        {
            cerr << "Error: AIGER parsing failed for AND gate (" << output_lit << " = " << input1_lit << " & " << input2_lit
                 << ") on line " << i + 1 << ": input literal ";
            if (it1 == literal_to_bdd_map.end())
                cerr << input1_lit << " (input1) ";
            if (it1 == literal_to_bdd_map.end() && it2 == literal_to_bdd_map.end())
                cerr << "and ";
            if (it2 == literal_to_bdd_map.end())
                cerr << input2_lit << " (input2) ";
            cerr << "not found in map." << endl;

            aig_file_stream.close();
            Cudd_Quit(manager);
            return 1;
        }

        DdNode *input1_bdd = it1->second;
        DdNode *input2_bdd = it2->second;

        // cout << "Debug: About to create BDD for AND gate line " << i + 1
        //      << " (output " << output_lit << " = " << input1_lit << " AND " << input2_lit << ")" << endl; // 按需开启
        DdNode *and_node = Cudd_bddAnd(manager, input1_bdd, input2_bdd);
        Cudd_Ref(and_node);

        // cout << "Debug: Successfully created BDD for AND gate line " << i + 1 << endl;  // 按需开启

        literal_to_bdd_map[output_lit] = and_node;
        literal_to_bdd_map[output_lit + 1] = Cudd_Not(and_node);
        Cudd_Ref(Cudd_Not(and_node));
    }
    cout << "Debug: Finished reading " << nA << " AND gates." << endl;
    aig_file_stream.close();

    // 6. 获取最终输出的 BDD 节点 (第一个输出)
    if (nO > 0)
    {
        if (output_literals.empty() || !literal_to_bdd_map.count(output_literals[0]))
        {
            cerr << "Error: Final output BDD node for literal "
                 << (output_literals.empty() ? -1 : output_literals[0])
                 << " not found in map, or output_literals vector is empty." << endl;
            Cudd_Quit(manager);
            return 1;
        }
        bdd_circuit_output = literal_to_bdd_map[output_literals[0]];
        Cudd_Ref(bdd_circuit_output);
    }
    else
    {
        cerr << "Error: No outputs defined in AIGER file (nO=0), cannot get final BDD." << endl;
        Cudd_Quit(manager);
        return 1;
    }

    cout << "AIG 文件解析成功，BDD 构建初步完成。" << endl;

    // *** 原有的 Cudd_AutodynEnable 已移至 Cudd_Init 之后 ***
    // cout << "Debug: (Original place for AutodynEnable, now enabled earlier)" << endl;

    // 5. 采样并生成解
    json result_json;
    json assignment_list = json::array();

    ifstream original_json_stream(original_json_path);
    json original_data;
    if (original_json_stream.is_open())
    {
        original_json_stream >> original_data;
        original_json_stream.close();
    }
    else
    {
        cerr << "Warning: Could not open original JSON for variable info: " << original_json_path << endl;
    }
    json original_variable_list = original_data.contains("variable_list") ? original_data["variable_list"] : json::array();

    DdNode **input_vars_array = nullptr;
    if (nI > 0)
    {
        input_vars_array = new DdNode *[nI];
        for (int i = 0; i < nI; ++i)
        {
            input_vars_array[i] = input_vars_bdd[i];
        }
    }

    cout << "Debug: Starting to pick " << num_samples << " samples." << endl;
    if (bdd_circuit_output == Cudd_ReadLogicZero(manager))
    {
        cout << "Warning: The BDD for the circuit output is constant ZERO. No satisfying assignments exist." << endl;
        num_samples = 0;
    }
    else if (bdd_circuit_output == Cudd_ReadOne(manager) && nI > 0)
    { // Only relevant if there are inputs
        cout << "Info: The BDD for the circuit output is constant ONE. All assignments to inputs are solutions." << endl;
    }
    else if (bdd_circuit_output == Cudd_ReadOne(manager) && nI == 0)
    {
        cout << "Info: The BDD for the circuit output is constant ONE and there are no inputs. This is a tautology." << endl;
        // If num_samples > 0, we might produce one "empty" assignment.
    }

    for (int i = 0; i < num_samples; ++i)
    {
        // For constant ONE BDD with no variables, Cudd_bddPickOneMinterm might return NULL or a special value.
        // If nI is 0 and bdd_circuit_output is ONE, minterm is essentially "true".
        DdNode *minterm_node = nullptr;
        if (nI > 0)
        { // Only pick minterm if there are variables to assign
            minterm_node = Cudd_bddPickOneMinterm(manager, bdd_circuit_output, input_vars_array, nI);
        }
        else if (bdd_circuit_output == Cudd_ReadOne(manager))
        {                                         // No inputs, circuit is TRUE
            minterm_node = Cudd_ReadOne(manager); // Represent the "true" assignment
        }
        // else if nI == 0 and bdd_circuit_output is ZERO, minterm_node remains nullptr (handled below)

        if (!minterm_node || minterm_node == Cudd_ReadLogicZero(manager))
        {
            cerr << "Warning: Could not find more satisfying assignments (sample " << i + 1 << " of " << num_samples << ")." << endl;
            if (i == 0)
            {
                assignment_list = json::array();
            }
            break;
        }
        if (minterm_node != Cudd_ReadOne(manager) || nI > 0)
        { // Don't Ref BDD_ONE if it's just a placeholder for nI=0 case without actually picking
            Cudd_Ref(minterm_node);
        }

        json assignment_entry = json::array();
        int current_cudd_var_idx = 0;

        for (const auto &var_info : original_variable_list)
        {
            if (!var_info.contains("id") || !var_info["id"].is_number() ||
                !var_info.contains("bit_width") || !var_info["bit_width"].is_number())
            {
                cerr << "Warning: Skipping malformed variable info in original JSON." << endl;
                continue;
            }

            int bit_width = var_info.value("bit_width", 1);
            unsigned long long variable_combined_value = 0;

            for (int bit_idx = 0; bit_idx < bit_width; ++bit_idx)
            {
                if (current_cudd_var_idx >= nI)
                {
                    cerr << "Error: Mismatch between total bits in original JSON variables and AIGER input count (nI). "
                         << "current_cudd_var_idx=" << current_cudd_var_idx << ", nI=" << nI << endl;
                    if (minterm_node != Cudd_ReadOne(manager) || nI > 0)
                        Cudd_RecursiveDeref(manager, minterm_node);
                    Cudd_RecursiveDeref(manager, bdd_circuit_output);
                    if (input_vars_array)
                        delete[] input_vars_array;
                    Cudd_Quit(manager);
                    return 1;
                }

                DdNode *current_bit_var_bdd = input_vars_array[current_cudd_var_idx];
                unsigned long long bit_assignment = 0;

                if (minterm_node == Cudd_ReadOne(manager) && nI > 0)
                {
                    // If the whole BDD is ONE, any assignment works. Cudd_bddPickOneMinterm might return a specific one (e.g., all zeros or all ones).
                    // Or, it might return BDD_ONE() itself if it can't pick specific variables.
                    // Here, we need to properly extract variable values if minterm_node is BDD_ONE() but represents a full assignment.
                    // Cudd_bddPickOneMinterm should return a cube (product of literals).
                    // If minterm_node is BDD_ONE(), it means the function is a tautology.
                    // We can choose any value for the bits, e.g., 0.
                    // However, a more robust way: Cudd_bddPickOneMinterm for a tautology over vars {x1..xn}
                    // should return a minterm like x1*x2*...*xn or some specific assignment.
                    // The logic below should correctly find if var is in positive or negative phase in that picked minterm.
                }

                DdNode *temp_check_one = Cudd_bddAnd(manager, minterm_node, current_bit_var_bdd);
                Cudd_Ref(temp_check_one);

                if (temp_check_one == minterm_node)
                {
                    bit_assignment = 1;
                }
                else
                {
                    DdNode *not_current_bit_var_bdd = Cudd_Not(current_bit_var_bdd);
                    DdNode *temp_check_zero = Cudd_bddAnd(manager, minterm_node, not_current_bit_var_bdd);
                    Cudd_Ref(temp_check_zero);
                    if (temp_check_zero == minterm_node)
                    {
                        bit_assignment = 0;
                    }
                    else
                    {
                        // This can happen if minterm_node is BDD_ONE and current_bit_var_bdd is not among the primary inputs
                        // or if the minterm doesn't depend on this variable (should not happen for a full minterm).
                        // Or if minterm_node itself is not a proper cube.
                        cerr << "Warning: Could not determine assignment for CUDD var idx " << current_cudd_var_idx
                             << " in minterm. Defaulting to 0. Minterm may be BDD_ONE or complex." << endl;
                    }
                    Cudd_RecursiveDeref(manager, temp_check_zero);
                }
                Cudd_RecursiveDeref(manager, temp_check_one);

                variable_combined_value |= (bit_assignment << bit_idx);
                current_cudd_var_idx++;
            }
            assignment_entry.push_back({{"value", to_hex_string(variable_combined_value, bit_width)}});
        }
        // Handle nI=0, bdd_circuit_output=ONE, num_samples > 0 case:
        // produce one empty assignment if original_variable_list is also empty.
        if (nI == 0 && bdd_circuit_output == Cudd_ReadOne(manager) && original_variable_list.empty())
        {
            // assignment_entry would be empty, assignment_list gets one empty entry.
        }

        if (current_cudd_var_idx != nI && !original_variable_list.empty() && nI > 0)
        {
            cerr << "Warning: Processed " << current_cudd_var_idx << " CUDD variables, but AIGER file has nI=" << nI << " inputs. "
                 << "This may happen if original_json variables cover fewer bits than nI." << endl;
        }

        assignment_list.push_back(assignment_entry);
        if (minterm_node != Cudd_ReadOne(manager) || nI > 0)
        { // Avoid dereferencing the global BDD_ONE constant if it was used as a placeholder
            Cudd_RecursiveDeref(manager, minterm_node);
        }
    }

    if (input_vars_array)
    {
        delete[] input_vars_array;
    }

    result_json["assignment_list"] = assignment_list;

    std::ofstream output_json_stream(result_json_path);
    if (!output_json_stream.is_open())
    {
        std::cerr << "Error: Failed to open output JSON file: " << result_json_path << std::endl;
        Cudd_RecursiveDeref(manager, bdd_circuit_output);
        Cudd_Quit(manager);
        return 1;
    }
    output_json_stream << result_json.dump(4) << std::endl;
    output_json_stream.close();

    Cudd_RecursiveDeref(manager, bdd_circuit_output);

    // 可选：打印CUDD统计信息
    // cout << "CUDD statistics before Cudd_Quit:" << endl;
    // Cudd_PrintInfo(manager, stdout); // 这会打印很多关于CUDD内部状态的详细信息

    Cudd_Quit(manager);
    std::cout << "BDD 求解器完成。结果已写入到 " << result_json_path << std::endl;
    return 0;
}