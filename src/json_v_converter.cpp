#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

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
             type == "UNARY_PLUS" || type == "UNARY_MINUS")
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
        else if (type == "UNARY_MINUS")
            op_symbol = "-";

        return op_symbol + "(" + lhs + ")";
    }

    // 二元运算符
    else if (type == "ADD" || type == "SUB" || type == "MUL" || type == "DIV" || type == "MOD" ||
             type == "LSHIFT" || type == "RSHIFT" ||
             type == "BIT_AND" || type == "BIT_OR" || type == "BIT_XOR" || type == "LOG_AND" || type == "LOG_OR" ||
             type == "EQ" || type == "NE" || type == "LT" || type == "LE" || type == "GT" || type == "GE")
    {
        string lhs = expression_tree(node["lhs_expression"]);
        string rhs = expression_tree(node["rhs_expression"]);

        // 特殊关照 DIV 和 MOD 的分母
        if (type == "DIV" || type == "MOD")
            special_cnstrs.push_back(rhs);

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
        else if (type == "NE")
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

    return "";
}

// 核心模块
int json_v_converter(string input_json_path, string output_v_dir)
{

    // 读取输入至data
    json data;
    ifstream input_json_stream(input_json_path);
    input_json_stream >> data;
    input_json_stream.close();

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
        // eg:     input wire [6:0] var0,
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
        string line2 = "    assign cnstr" + to_string(num_cnstr) + "_redor = |(" + cnstr + ");";
        v_lines.push_back(line1);
        v_lines.push_back(line2);
        num_cnstr++;
    }

    // 结果输出
    string result_assign = "    assign result = ";
    for (int i = 0; i < num_cnstr; i++)
    {
        result_assign += "cnstr" + to_string(i) + "_redor";
        if (i == num_cnstr - 1)
            result_assign += ";";
        else
            result_assign += " & ";
    }

    v_lines.push_back(result_assign);
    v_lines.push_back("endmodule");

    // 文件名处理和写入
    string parent_dir_name;
    string filename_no_ext;

    size_t last_slash_pos = input_json_path.find_last_of("/\\");
    string filename_with_ext = (last_slash_pos == string::npos) ? input_json_path : input_json_path.substr(last_slash_pos + 1);

    size_t last_dot_pos = filename_with_ext.find_last_of('.');
    if (string::npos != last_dot_pos)
    {
        filename_no_ext = filename_with_ext.substr(0, last_dot_pos);
    }
    else
    {
        filename_no_ext = filename_with_ext;
    }

    if (last_slash_pos != string::npos)
    {
        size_t second_last_slash_pos = input_json_path.find_last_of("/\\", last_slash_pos - 1);
        if (second_last_slash_pos != string::npos)
        {
            parent_dir_name = input_json_path.substr(second_last_slash_pos + 1, last_slash_pos - (second_last_slash_pos + 1));
        }
        else
        {
            if (last_slash_pos > 0)
            {
                parent_dir_name = input_json_path.substr(0, last_slash_pos);
            }
        }
        if (parent_dir_name.empty() || parent_dir_name == "." || parent_dir_name == ".." || parent_dir_name.find_first_of("/\\") != string::npos)
        {
            parent_dir_name.clear();
        }
    }

    string test_id;
    if (!parent_dir_name.empty())
    {
        test_id = parent_dir_name + "_" + filename_no_ext;
    }
    else
    {
        test_id = filename_no_ext;
    }

    string output_v_filename = output_v_dir;
    if (!output_v_dir.empty() && output_v_dir.back() != '/' && output_v_dir.back() != '\\')
    {
        output_v_filename += "/";
    }
    output_v_filename += test_id + ".v";

    ofstream output_v_stream(output_v_filename);

    for (const string &line : v_lines)
    {
        output_v_stream << line << endl;
    }
    output_v_stream.close();
    return 0;
}

int main(){
    string in = "/root/sv-sampler-lab/opt1/0.json";
    string out = "/root/sv-sampler-lab/src/generated_files";
    int convert = json_v_converter(in, out);
    return 0;
}