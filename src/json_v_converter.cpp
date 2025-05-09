#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"
#include "solver_functions.h" // 包含 aig_to_bdd_solver 的声明 (虽然 json_v_converter 不直接使用，但为了项目结构保留)
#include <filesystem>

using json = nlohmann::json;
using namespace std;
using namespace std::filesystem;

// 本文件实现功能：将input_json_path的json格式文件输入
// 转化成verilog代码，输出.v文件到output_v_dir，以便后续yosys处理

// 用于递归提取json中的约束
vector<string> special_cnstrs; // 储存分母不能为零导致的约束的表达式
string expression_tree(const json &node)
{
    string type = node["op"];

    // 变量或定值
    if (type == "VAR")
    {
        int id = node.value("id", 0);
        return string("var_" + to_string(id));
    }
    else if (type == "CONST")
    {
        return node.value("value", "1'b0");
    }

    // 一元运算
    else if (type == "BIT_NEG" || type == "LOG_NEG" ||
             type == "RED_AND" || type == "RED_OR" || type == "RED_XOR" ||
             type == "RED_NAND" || type == "RED_NOR" || type == "RED_XNOR" ||
             type == "UNARY_PLUS" || type == "UNARY_MINUS" || type == "MINUS")
    {
        string lhs = expression_tree(node["lhs_expression"]);
        string op_symbol;
        if (type == "BIT_NEG")
            op_symbol = "~";
        else if (type == "LOG_NEG")
            op_symbol = "!";
        else if (type == "RED_AND")
            op_symbol = "&";
        else if (type == "RED_OR")
            op_symbol = "|";
        else if (type == "RED_XOR")
            op_symbol = "^";
        else if (type == "RED_NAND")
            op_symbol = "~&";
        else if (type == "RED_NOR")
            op_symbol = "~|";
        else if (type == "RED_XNOR")
            op_symbol = "~^";
        else if (type == "UNARY_PLUS")
            op_symbol = "+";
        else if (type == "UNARY_MINUS" || type == "MINUS")
            op_symbol = "-";

        return op_symbol + "(" + lhs + ")";
    }

    // 二元运算符
    else if (type == "ADD" || type == "SUB" || type == "MUL" || type == "DIV" || type == "MOD" ||
             type == "LSHIFT" || type == "RSHIFT" ||
             type == "BIT_AND" || type == "BIT_OR" || type == "BIT_XOR" || type == "LOG_AND" || type == "LOG_OR" ||
             type == "EQ" || type == "NEQ" || type == "LT" || type == "LE" || type == "GT" || type == "GE" || type == "IMPLY")
    {
        string lhs = expression_tree(node["lhs_expression"]);
        string rhs = expression_tree(node["rhs_expression"]);

        // 特殊关照 DIV 和 MOD 的分母
        if (type == "DIV" || type == "MOD")
            special_cnstrs.push_back(rhs);

        if (type == "IMPLY")
            return "( (!(" + lhs + ")) || (" + rhs + ") )";
        else
        {
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
            else if (type == "LT")
                op_symbol = "<";
            else if (type == "LE")
                op_symbol = "<=";
            else if (type == "GT")
                op_symbol = ">";
            else if (type == "GE")
                op_symbol = ">=";
            return "(" + lhs + " " + op_symbol + " " + rhs + ")";
        }
    }

    return "";
}

// 核心模块：JSON 到 Verilog 转换
int json_v_converter(const string &input_json_path, const string &output_v_dir)
{
    // 清空 special_cnstrs，确保每次调用都是新的状态
    special_cnstrs.clear();

    // 读取输入至data
    json data;
    ifstream input_json_stream(input_json_path);
    if (!input_json_stream.is_open())
    {
        cerr << "Error: Could not open input JSON file: " << input_json_path << endl;
        return 1;
    }
    try
    {
        input_json_stream >> data;
    }
    catch (const json::parse_error &e)
    {
        cerr << "Error: JSON parse error: " << e.what() << " in file " << input_json_path << endl;
        return 1;
    }
    input_json_stream.close();

    // 检查必要的字段是否存在
    if (!data.contains("variable_list") || !data["variable_list"].is_array() ||
        !data.contains("constraint_list") || !data["constraint_list"].is_array())
    {
        cerr << "Error: Invalid JSON format. Missing 'variable_list' or 'constraint_list'." << endl;
        return 1;
    }

    // 需要构建的.v文件
    vector<string> v_lines;

    // 根据输入格式，data中有两个列表variable_list和constraint_list
    json variable_list = data["variable_list"];
    json constraint_list = data["constraint_list"];
    int N = variable_list.size();
    int M = constraint_list.size();

    // 变量声明
    v_lines.push_back("module from_json(");
    for (const auto &var : variable_list)
    {
        // eg:     input wire [6:0] var0,
        if (!var.contains("name") || !var["name"].is_string() ||
            !var.contains("bit_width") || !var["bit_width"].is_number())
        {
            cerr << "Warning: Skipping malformed variable entry in JSON." << endl;
            continue;
        }
        string name = var.value("name", "default_name");
        int bit_width = var.value("bit_width", 1);
        v_lines.push_back("    input wire [" + to_string(bit_width - 1) + ":0] " + name + ",");
    }
    v_lines.push_back("    output wire result");
    v_lines.push_back(");");

    // 普通约束
    int num_cnstr = 0;
    for (const auto &cnstr : constraint_list)
    {
        // eg:      assign cnstr0 = ~var0 + var1;
        //          assign cnstr0_redor = |cnstr0;
        string line1 = "    wire cnstr" + to_string(num_cnstr) + "_redor;";
        string line2 = "    assign cnstr" + to_string(num_cnstr) + "_redor = |(" + expression_tree(cnstr) + ");";

        v_lines.push_back(line1);
        v_lines.push_back(line2);
        num_cnstr++;
    }

    // 特殊约束：分母不为零
    for (string cnstr : special_cnstrs)
    {
        string line1 = "    wire cnstr" + to_string(num_cnstr) + "_redor;";
        // 分母不为零的约束是表达式本身不等于0
        string line2 = "    assign cnstr" + to_string(num_cnstr) + "_redor = |(" + cnstr + " != 0);";
        v_lines.push_back(line1);
        v_lines.push_back(line2);
        num_cnstr++;
    }

    // 结果输出
    string result_assign = "    assign result = ";
    if (num_cnstr == 0)
    {
        // 如果没有约束，结果始终为真 (1'b1)
        result_assign += "1'b1;";
    }
    else
    {
        for (int i = 0; i < num_cnstr; i++)
        {
            result_assign += "cnstr" + to_string(i) + "_redor";
            if (i == num_cnstr - 1)
                result_assign += ";";
            else
                result_assign += " & ";
        }
    }

    v_lines.push_back(result_assign);
    v_lines.push_back("endmodule");

    // 文件名处理和写入
    filesystem::path input_json_path_obj(input_json_path);
    string filename_no_ext = input_json_path_obj.stem().string();
    string parent_dir_name = input_json_path_obj.parent_path().filename().string();

    string test_id;
    if (!parent_dir_name.empty() && parent_dir_name != "." && parent_dir_name != "/")
    {
        test_id = parent_dir_name + "_" + filename_no_ext;
    }
    else
    {
        test_id = filename_no_ext;
    }

    filesystem::path output_v_path = filesystem::path(output_v_dir) / (test_id + ".v");

    ofstream output_v_stream(output_v_path);

    if (!output_v_stream.is_open())
    {
        cerr << "Error: Could not open output Verilog file: " << output_v_path << endl;
        return 1;
    }

    for (const string &line : v_lines)
    {
        output_v_stream << line << endl;
    }
    output_v_stream.close();

    cout << "Verilog file generated successfully: " << output_v_path << endl;

    return 0;
}

// 注意：原有的 main 函数已移除，现在 main 函数位于单独的 main.cpp 文件中。
