#include <iostream>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_view_literals;

int main() {
    // Flush after every std::cout / std:cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;


    while (true) {
        std::cout << "$ ";

        std::string input;
        if (std::getline(std::cin, input).eof()) break;

        if (input.starts_with("exit"sv)) {
            return 0;
        } else if (input.starts_with("echo "sv)) {
            auto args = std::string_view(input).substr(5);
            std::cout << args << "\n";
        } else {
            std::cout << input << ": command not found\n";
        }
    }
}
