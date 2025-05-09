#include <iostream>

// 包含 nlohmann/json 头文件
// nlohmann_json::nlohmann_json 目标通常会设置正确的包含路径
#include "nlohmann/json.hpp"

// 包含 CUDD 头文件
// CUDD::cudd 目标或手动查找应确保可以找到这些头文件
extern "C" { // CUDD 是 C 库
#include <cudd.h>
// 如果使用 CUDD C++ 包装器, 取消注释下一行并确保头文件路径正确
// #include <cuddObj.hh>
}


int main(int argc, char *argv[]) {
    std::cout << "Starting SVSamplerLabSolver..." << std::endl;

    // 测试 nlohmann_json
    nlohmann::json j;
    j["message"] = "Hello from nlohmann_json!";
    j["version"] = NLOHMANN_JSON_VERSION_MAJOR * 10000 + NLOHMANN_JSON_VERSION_MINOR * 100 + NLOHMANN_JSON_VERSION_PATCH;
    
    std::cout << "JSON Test: " << j.dump(4) << std::endl;

    // 测试 CUDD
    DdManager *gbm; /* 全局 BDD 管理器 */
    gbm = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0); /* 初始化一个新的 BDD 管理器 */
    if (gbm == NULL) {
        std::cerr << "Cudd_Init failed" << std::endl;
        return 1;
    }
    std::cout << "CUDD initialized successfully." << std::endl;
    
    DdNode *b_one = Cudd_ReadOne(gbm);
    if (Cudd_IsConstant(b_one)) {
        std::cout << "CUDD Cudd_ReadOne(gbm) node is constant." << std::endl;
    }
    // Cudd_Ref(b_one); // 在实际使用中，非临时节点需要管理引用计数

    Cudd_Quit(gbm); /* 释放 BDD 管理器 */
    std::cout << "CUDD quit successfully." << std::endl;

    // Yosys 通常作为命令行工具使用，而不是直接链接到求解器。
    // 如果您确实链接了 Yosys 库，可以在此处添加测试代码。
    // std::cout << "Yosys (if linked): Test code here." << std::endl;

    std::cout << "Solver placeholder linked successfully with nlohmann_json and CUDD." << std::endl;
    return 0;
}
