#include "nova/cli.hpp"

#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>

#include "nova/ast.hpp"
#include "nova/bench/cli.hpp"
#include "nova/interp.hpp"
#include "nova/lexer.hpp"
#include "nova/optimizer.hpp"
#include "nova/tune/cli.hpp"
#include "nova/tune/evaluate.hpp"
#include "nova/tune/study.hpp"
#include "nova/parser.hpp"
#include "nova/semantic.hpp"
#include "nova/tac.hpp"

#ifndef NOVA_VERSION
#define NOVA_VERSION "unknown"
#endif

namespace nova {

std::string version() { return NOVA_VERSION; }

namespace {

void print_usage(std::ostream& os) {
    os << "Usage: nova <command> [arguments]\n"
          "       nova [option]\n"
          "\n"
          "NOVA Script commands:\n"
          "  tokens <file>  Print the token stream\n"
          "  ast <file>     Parse and print the syntax tree\n"
          "  tac <file>     Check the program and print three-address code\n"
          "  run <file>     Check and execute the program (print statements go to stdout)\n"
          "  opt <file> [--passes=a,b,...] [--show-all]\n"
          "                 Optimize the TAC. Passes: fold, propagate, simplify, dce (default: all, repeated\n"
          "                 to a fixed point). --show-all prints the TAC after every pass that changed it.\n"
          "\n"
          "Options:\n"
          "  -h, --help     Show this help and exit\n"
          "      --version  Print the version and exit\n"
          "\n"
          "Exit codes: 0 success, 1 input error (unreadable file, source or runtime error), 2 usage error.\n";
    os << '\n' << bench::bench_usage() << '\n' << tune::tune_usage() << tune::evaluate_usage() << tune::study_usage();
}

bool read_file(const std::string& path, std::string& contents) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    contents = ss.str();
    return true;
}

int usage_error(std::ostream& err, const std::string& msg) {
    err << "nova: " << msg << "\n"
        << "Try 'nova --help' for usage.\n";
    return 2;
}

void report(std::ostream& err, const std::string& file, const std::vector<Diagnostic>& ds) {
    for (const Diagnostic& d : ds) err << format_diagnostic(file, d) << "\n";
}

// Reads `path` and runs the front end up to `stage`. Returns 0 on success; otherwise prints
// diagnostics to `err` and returns the exit code (1).
enum class Stage { Lex, Parse, Semantic };

int front_end(const std::string& path, Stage stage, std::ostream& err, LexResult& lexed,
              std::unique_ptr<Program>& program) {
    std::string source;
    if (!read_file(path, source)) {
        err << "nova: cannot read '" << path << "'\n";
        return 1;
    }
    lexed = lex(source);
    if (!lexed.ok()) {
        report(err, path, lexed.diagnostics);
        return 1;
    }
    if (stage == Stage::Lex) return 0;
    ParseResult parsed = parse(lexed.tokens);
    if (!parsed.ok()) {
        report(err, path, parsed.diagnostics);
        return 1;
    }
    program = std::move(parsed.program);
    if (stage == Stage::Parse) return 0;
    SemanticResult checked = analyze(*program);
    if (!checked.ok()) {
        report(err, path, checked.diagnostics);
        return 1;
    }
    return 0;
}

int one_file(const std::vector<std::string>& args, std::ostream& err, const char* cmd) {
    if (args.size() == 2) return 0;
    return usage_error(err, std::string("'") + cmd + "' requires exactly one file argument");
}

int cmd_tokens(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (int rc = one_file(args, err, "tokens")) return rc;
    LexResult lexed;
    std::unique_ptr<Program> program;
    if (int rc = front_end(args[1], Stage::Lex, err, lexed, program)) return rc;
    out << dump_tokens(lexed.tokens);
    return 0;
}

int cmd_ast(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (int rc = one_file(args, err, "ast")) return rc;
    LexResult lexed;
    std::unique_ptr<Program> program;
    if (int rc = front_end(args[1], Stage::Parse, err, lexed, program)) return rc;
    out << dump_ast(*program);
    return 0;
}

int cmd_tac(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (int rc = one_file(args, err, "tac")) return rc;
    LexResult lexed;
    std::unique_ptr<Program> program;
    if (int rc = front_end(args[1], Stage::Semantic, err, lexed, program)) return rc;
    out << print_tac(generate_tac(*program));
    return 0;
}

int cmd_run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (int rc = one_file(args, err, "run")) return rc;
    LexResult lexed;
    std::unique_ptr<Program> program;
    if (int rc = front_end(args[1], Stage::Semantic, err, lexed, program)) return rc;
    RunResult r = run_tac(generate_tac(*program));
    out << format_output(r.output);
    if (!r.ok) {
        err << format_diagnostic(args[1], r.error) << "\n";
        return 1;
    }
    return 0;
}

int cmd_opt(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::string file;
    bool show_all = false;
    std::vector<PassInfo> passes = all_passes();
    for (size_t k = 1; k < args.size(); ++k) {
        const std::string& a = args[k];
        if (a == "--show-all") {
            show_all = true;
        } else if (a.rfind("--passes=", 0) == 0) {
            passes.clear();
            std::string list = a.substr(9);
            size_t pos = 0;
            while (true) {
                size_t comma = list.find(',', pos);
                std::string name = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                const PassInfo* p = find_pass(name);
                if (!p) return usage_error(err, "unknown pass '" + name + "' (available: fold, propagate, simplify, dce)");
                passes.push_back(*p);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (a.rfind("--", 0) == 0) {
            return usage_error(err, "unknown option '" + a + "' for 'opt'");
        } else if (file.empty()) {
            file = a;
        } else {
            return usage_error(err, "'opt' requires exactly one file argument");
        }
    }
    if (file.empty()) return usage_error(err, "'opt' requires exactly one file argument");
    LexResult lexed;
    std::unique_ptr<Program> program;
    if (int rc = front_end(file, Stage::Semantic, err, lexed, program)) return rc;
    TacProgram tac = generate_tac(*program);
    OptimizeResult result = optimize(tac, passes, show_all);
    if (show_all) {
        out << "== original ==\n" << print_tac(tac);
        for (const OptimizeStep& s : result.steps) {
            if (!s.changed) continue;
            out << "== after " << s.pass << " (iteration " << s.iteration << ") ==\n" << s.after;
        }
        out << "== final (" << (result.converged ? "fixed point" : "iteration cap reached") << " after "
            << result.iterations << " iteration" << (result.iterations == 1 ? "" : "s") << ") ==\n";
    }
    out << print_tac(result.program);
    if (!result.converged) {
        err << "nova: warning: optimizer stopped at the iteration cap without reaching a fixed point\n";
    }
    return 0;
}

}  // namespace

int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        print_usage(err);
        return 2;
    }
    const std::string& first = args[0];
    if (first == "--help" || first == "-h") {
        if (args.size() != 1) return usage_error(err, "unexpected arguments after '" + first + "'");
        print_usage(out);
        return 0;
    }
    if (first == "--version") {
        if (args.size() != 1) return usage_error(err, "unexpected arguments after '--version'");
        out << "nova " << version() << "\n";
        return 0;
    }
    if (first == "tokens") return cmd_tokens(args, out, err);
    if (first == "ast") return cmd_ast(args, out, err);
    if (first == "tac") return cmd_tac(args, out, err);
    if (first == "run") return cmd_run(args, out, err);
    if (first == "opt") return cmd_opt(args, out, err);
    if (first == "tune") return tune::run_tune_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "table") return tune::run_table_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "history") return tune::run_history_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "study") return tune::run_study_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "evaluate") return tune::run_evaluate_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "explain") return tune::run_explain_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    if (first == "bench") return bench::run_bench_command(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
    return usage_error(err, "unknown argument '" + first + "'");
}

}  // namespace nova
