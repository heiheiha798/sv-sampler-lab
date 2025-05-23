#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "solver_functions.h"
int json_v_converter(const std::string &input_json_path,
                     const std::string &output_v_dir);
using namespace std;
using namespace std::filesystem;
int main(int argc, char *argv[]) {
    if (argc == 4 && string(argv[1]) == "json-to-v") {
        string input_json_path_str = argv[2];
        string output_v_dir_str = argv[3];
        return json_v_converter(input_json_path_str, output_v_dir_str);
    } else if (argc == 7 && string(argv[1]) == "aig-to-bdd") {
        string aig_file_path = argv[2];
        string original_json_path = argv[3];
        int num_samples = stoi(argv[4]);
        string result_json_path = argv[5];
        unsigned int random_seed = stoul(argv[6]);
        return aig_to_bdd_solver(aig_file_path, original_json_path, num_samples,
                                 result_json_path, random_seed);
    } else
        return 1;
}