// main.cpp - command-line interface for the line counter.
//
// Usage:
//   cloc [directory]    count code lines under <directory>, default is the
//                       current directory.

#include "counter.hpp"

#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const fs::path dir = (argc > 1) ? fs::path(argv[1]) : fs::path(".");
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        std::cerr << "Error: '" << dir.string() << "' is not a valid directory.\n";
        return 1;
    }
    CountResult res = countDirectory(dir);

    const int W_NAME = 15, W_FILES = 10, W_BLANK = 11, W_COMMENT = 12, W_CODE = 12;
    const std::string sep(W_NAME + W_FILES + W_BLANK + W_COMMENT + W_CODE, '-');

    std::cout << sep << '\n';
    std::cout << std::left  << std::setw(W_NAME)    << "Language"
              << std::right << std::setw(W_FILES)   << "files"
                            << std::setw(W_BLANK)   << "blank"
                            << std::setw(W_COMMENT) << "comment"
                            << std::setw(W_CODE)    << "code" << '\n';
    std::cout << sep << '\n';

    auto printRow = [&](const std::string& name, long f, long b, long cm, long cd) {
        std::cout << std::left  << std::setw(W_NAME)    << name
                  << std::right << std::setw(W_FILES)   << f
                                << std::setw(W_BLANK)   << b
                                << std::setw(W_COMMENT) << cm
                                << std::setw(W_CODE)    << cd << '\n';
    };

    for (const auto& r : res.languages)
        printRow(r.name, r.files, r.blank, r.comment, r.code);

    std::cout << sep << '\n';
    printRow("SUM", res.totalFiles, res.totalBlank, res.totalComment, res.totalCode);
    std::cout << sep << '\n';

    return 0;
}
