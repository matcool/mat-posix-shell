#include <iostream>

int main() {
    // Flush after every std::cout / std:cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;


    while (true) {
        std::cout << "$ ";

        std::string input;
        if (std::getline(std::cin, input).eof()) break;
        std::cout << input << ": command not found\n";
    }
}
