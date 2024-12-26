#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <optional>
#include <ranges>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_view_literals;

struct Shell {
    using CmdArgs = std::vector<std::string>;
    using CmdHandler = std::function<void(Shell*, const CmdArgs&)>;

    std::unordered_map<std::string_view, CmdHandler> builtin_cmds;

    Shell() {
        #define BUILTIN(name) { #name, [](Shell* shell, const CmdArgs& args) { shell->handle_builtin_##name(args); } }
        builtin_cmds = {
            BUILTIN(exit),
            BUILTIN(echo),
            BUILTIN(type),
        };
        #undef BUILTIN
    }

    void print_prompt() {
        std::cout << "$ ";
    }

    CmdArgs parse_input(std::string_view input) const {
        std::vector<std::string> args;

        while (true) {
            auto i = input.find_first_not_of(' ');
            if (i == -1) break;
            input.remove_prefix(i);
            auto end = input.find_first_of(' ');
            
            auto arg = input.substr(0, end);
            args.emplace_back(arg);

            if (end != -1) {
                input.remove_prefix(end);
            } else {
                break;
            }
        }

        return args;
    }

    std::optional<std::filesystem::path> search_path(std::string_view name) {
        std::string_view path = std::getenv("PATH");
        if (path.empty()) return std::nullopt;

        for (const auto part : std::views::split(path, ':')) {
            auto full_path = std::filesystem::path(std::string_view(part)) / name;
            if (std::filesystem::exists(full_path)) {
                return full_path;
            }
        }

        return std::nullopt;
    }

    void loop() {
        while (true) {
            this->print_prompt();

            std::string input;
            if (std::getline(std::cin, input).eof()) break;

            auto args = this->parse_input(input);
            if (args.empty()) continue;

            const auto& command = args[0];

            if (auto it = this->builtin_cmds.find(std::string_view(command)); it != this->builtin_cmds.end()) {
                it->second(this, args);
            } else {
                std::cout << input << ": command not found\n";
            }
        }
    }

private:
    void handle_builtin_exit(const CmdArgs&) {
        std::exit(0);
    }

    void handle_builtin_echo(const CmdArgs& args) {
        auto separator = ""sv;
        for (int i = 1; i < args.size(); ++i) {
            std::cout << (std::exchange(separator, " "sv)) << args[i];
        }
        std::cout << '\n';
    }

    void handle_builtin_type(const CmdArgs& args) {
        auto cmd = args.at(1);
        auto it = this->builtin_cmds.find(cmd);
        if (it != this->builtin_cmds.end()) {
            std::cout << cmd << " is a shell builtin\n";
        } else if (auto opt = this->search_path(cmd); opt.has_value()) {
            std::cout << cmd << " is " << (*opt).string() << "\n";
        } else {
            std::cout << cmd << ": not found\n";
        }
    }
};

int main() {
    // Flush after every std::cout / std:cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    Shell shell;
    shell.loop();
}
