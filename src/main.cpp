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
#include <fstream>

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_view_literals;

struct RawArg {
    std::string text;
    bool raw = false;
};

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

    std::vector<RawArg> parse_input(std::string_view input) const {
        std::vector<RawArg> args;

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
            bool raw = true;
            while (!input.empty()) {
                char ch = get_char();
                if (std::isspace(ch)) {
                    break;
                } else if (ch == '"') {
                    raw = false;
                    for (ch = get_char(); !input.empty() && ch != '"'; ch = get_char()) {
                        if (ch == '\\') {
                            auto next = get_char();
                            switch (next) {
                                default:
                                    arg.push_back(ch);
                                    [[gnu::fallthrough]];
                                case '"':
                                case '$':
                                case '`':
                                case '\\':
                                case '\n':
                                    arg.push_back(next);
                            }
                        } else {
                            arg.push_back(ch);
                        }
                    }
                } else if (ch == '\'') {
                    raw = false;
                    auto end = input.find_first_of('\'');
                    arg += input.substr(0, end);
                    if (end != -1) {
                        input.remove_prefix(end + 1);
                    } else {
                        input = {};
                    }
                } else if (ch == '\\') {
                    raw = false;
                    // take next character as is, no special processing
                    ch = get_char();
                    arg.push_back(ch);
                } else {
                    arg.push_back(ch);
                }
            }
            args.emplace_back(RawArg {
                .text = std::move(arg),
                .raw = raw,
            });
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
            std::unordered_map<int, int> redirected_files;

            std::string input;
            if (std::getline(std::cin, input).eof()) break;

            auto raw_args = this->parse_input(input);
            if (raw_args.empty()) continue;

            const auto& command = raw_args[0].text;
            CmdArgs args;

            bool add_args = true;
            for (int i = 0; i < raw_args.size(); ++i) {
                const auto& arg = raw_args[i];
                if (arg.raw && (
                    arg.text == ">"sv || arg.text == "1>"sv || arg.text == "2>"sv
                    || arg.text == ">>"sv || arg.text == "1>>"sv || arg.text == "2>>"sv)
                ) {
                    int target_fd = (arg.text[0] == '>' || arg.text[0] == '1') ? STDOUT_FILENO : STDERR_FILENO;
                    int flag = arg.text.ends_with(">>"sv) ? O_APPEND : O_TRUNC;

                    int temp = dup(target_fd);
                    int fd = open(raw_args.at(i + 1).text.c_str(), O_CREAT | O_RDWR | flag, 0b110'100'100);
                    dup2(fd, target_fd);

                    redirected_files[target_fd] = temp;

                    i++;
                    add_args = false;
                } else if (add_args) {
                    args.emplace_back(std::move(arg.text));
                }
            }

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
                std::cerr << command << ": command not found\n";
            }

            for (auto [replaced, temp] : redirected_files) {
                close(replaced);
                dup2(temp, replaced);
                close(temp);
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
            std::cerr << cmd << ": not found\n";
        }
    }

    void handle_builtin_pwd(const CmdArgs&) {
        std::cout << std::filesystem::current_path().string() << "\n";
    }

    void handle_builtin_cd(const CmdArgs& args) {
        std::filesystem::path new_path = this->expand_path(args.at(1));
        if (!std::filesystem::is_directory(new_path)) {
            std::cerr << "cd: " << new_path.string() << ": No such file or directory\n";
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
