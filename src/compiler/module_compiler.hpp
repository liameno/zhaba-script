#pragma once
#include <filesystem>
#include <fstream>
#include <string>

#include "../interpreter/to_bytecode.hpp"
#include "../interpreter/zhvm.hpp"
#include "../core/core.hpp"
#include "../parser/parser.hpp"
#include "./to_c.hpp"

void compileFile(std::filesystem::path file_path) {
  auto start_time = clock();

  auto stree = parseFile(resolvePath(file_path));

  if (zhdata.flags["show_st"]) {
    std::cout << "st:\n";
  }
  if (zhdata.flags["show_st_cool"]) {
    std::cout << "st:\n";
  }

  if (zhdata.flags["B"]) {
    if (!zhdata.flags["pure"])
      std::cout << "[INFO] compiling complete in "
                << std::to_string((clock() - start_time) * 1.0 / CLOCKS_PER_SEC)
                << std::endl;
    zhin::ByteCode bytecode;
    zhin::toB(bytecode, stree);
    auto run_time = clock();
    zhin::ZHVM zhvm(bytecode);

    while (zhvm.runChunk()) {
    }

    if (!zhdata.flags["pure"])
      std::cout << "[INFO] run complete in "
                << std::to_string((clock() - run_time) * 1.0 / CLOCKS_PER_SEC)
                << std::endl;
  } else {
    std::string c_code = module2C(stree);
    if (zhdata.flags["show_c"]) {
      std::cout << "C:" << std::endl << c_code << std::endl;
    }

    auto tmp_file = std::ofstream("zhaba_tmp.c");
    tmp_file << c_code;
    tmp_file.close();

    std::cout << "[INFO] compiling complete in "
              << std::to_string((clock() - start_time) * 1.0 / CLOCKS_PER_SEC)
              << std::endl;

    auto c_comptime = clock();

    auto cmd =
        (zhdata.options["compiler"] + " zhaba_tmp.c -o zhaba_tmp -O3 -w");
    system(cmd.c_str());

    std::cout << "[INFO] C compiling complete in "
              << std::to_string((clock() - c_comptime) * 1.0 / CLOCKS_PER_SEC)
              << std::endl;

    auto run_time = clock();

    system(".\\zhaba_tmp.exe");

    if (!zhdata.flags["pure"])
      std::cout << "[INFO] run complete in "
                << std::to_string((clock() - run_time) * 1.0 / CLOCKS_PER_SEC)
                << std::endl;
  }
}
