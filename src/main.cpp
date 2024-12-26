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

#include <sys/wait.h>
#include <unistd.h>

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
            BUILTIN(pwd),
            BUILTIN(cd),
        };
        #undef BUILTIN
    }

    void print_prompt() {
        std::cout << "$ ";
    }

    CmdArgs parse_input(std::string_view input) const {
        std::vector<std::string> args;

        auto get_char = [&] {
            char c = input[0];
            input.remove_prefix(1);
            return c;
        };

        while (!input.empty()) {
            auto i = input.find_first_not_of(' ');
            if (i == -1) break;
            input.remove_prefix(i);

            std::string arg;
            while (!input.empty()) {
                char ch = get_char();
                if (std::isspace(ch)) break;
                if (ch == '\'') {
                    auto end = input.find_first_of('\'');
                    arg += input.substr(0, end);
                    if (end != -1) {
                        input.remove_prefix(end + 1);
                    } else {
                        input = {};
                    }
                } else {
                    arg.push_back(ch);
                }
            }
            args.emplace_back(std::move(arg));
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

    // Turns ~ into the home dir
    std::filesystem::path expand_path(std::filesystem::path const& orig) {
        std::string_view home = std::getenv("HOME");
        std::filesystem::path output;
        bool first = true;
        for (auto part : orig) {
            if (first && part == "~"sv) {
                part = home;
            }
            first = false;
            output /= part;
        }
        return output;
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
            } else if (auto opt = this->search_path(command); opt) {
                auto path = std::move(opt.value());

                if (fork() == 0) {
                    auto range =
                        std::ranges::ref_view(args)
                        | std::views::transform([](const auto& str) { return str.c_str(); })
                        | std::views::common;
                    std::vector unix_args(range.begin(), range.end());
                    unix_args.push_back(nullptr);
                    execv(path.c_str(), const_cast<char* const*>(unix_args.data()));
                }
                wait(nullptr);
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

    void handle_builtin_pwd(const CmdArgs&) {
        std::cout << std::filesystem::current_path().string() << "\n";
    }

    void handle_builtin_cd(const CmdArgs& args) {
        std::filesystem::path new_path = this->expand_path(args.at(1));
        if (!std::filesystem::is_directory(new_path)) {
            std::cout << "cd: " << new_path.string() << ": No such file or directory\n";
        } else {
            std::filesystem::current_path(new_path);
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
