#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#include "byteveil_decompiler/Decompile.h"
#include "byteveil_decompiler/IR.h"
#include "byteveil_decompiler/Lua51.h"
#include "byteveil_decompiler/MoonSec.h"
#include "byteveil_decompiler/Protectors.h"
#include "byteveil_decompiler/Unpack.h"
#include "Luau/Compiler.h"
#include "lualib.h"

namespace luau { struct IdentityEncoder final : Luau::BytecodeEncoder { uint8_t encodeOp(uint8_t op) override { return op; } }; }

// Pathological or heavily-obfuscated input can make the Luau-path lifter
// generate a runaway amount of AST/printed output (observed in practice: a
// dispatcher-heavy VM-style obfuscator body that this lifter, once it stops
// bailing out early via placeholders, expands into an enormous tree). An
// uncontrolled allocation failure there surfaces as SIGSEGV/SIGBUS deep
// inside the Luau printer -- outside any try/catch, since it's a signal, not
// a C++ exception -- and kills the whole process. Capping address space
// makes malloc fail predictably instead, which libstdc++ reports as
// std::bad_alloc: a normal, catchable exception the existing decompile()
// try/catch below already handles.
static void limitAddressSpace(std::uint64_t bytes) {
#if defined(_WIN32)
    // A Job Object is the Windows equivalent of the POSIX address-space
    // resource limit. Keep the handle alive for the lifetime of the process.
    static HANDLE job = nullptr;
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(bytes);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, GetCurrentProcess()))
    {
        CloseHandle(job);
        job = nullptr;
    }
#else
    struct rlimit rl{};
    if (getrlimit(RLIMIT_AS, &rl) == 0) {
        if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur > bytes) {
            rl.rlim_cur = static_cast<rlim_t>(bytes);
            setrlimit(RLIMIT_AS, &rl);
        }
    }
#endif
}

// The Luau-path decompiler is a best-effort static lifter, not a verified one:
// on bytecode shaped in ways its opcode-length/register-tracking assumptions
// don't cover (seen in practice on heavily nested, VM-style obfuscator
// output), it can still end up doing a very large amount of work in a tight
// loop even after the out-of-range/underflow guards elsewhere in this
// codebase. Rather than let a single pathological input hang the CLI
// indefinitely, bound the whole decompile() call with a wall-clock watchdog
// and fail cleanly instead.
class DecompileWatchdog {
public:
    explicit DecompileWatchdog(unsigned int timeoutSec) : timeoutSec(timeoutSec) {
        if (timeoutSec == 0)
            return;
        worker = std::thread([this] {
            std::unique_lock<std::mutex> lock(mutex);
            if (!condition.wait_for(lock, std::chrono::seconds(this->timeoutSec), [this] { return stopped; })) {
                std::cerr << "error: decompiler exceeded " << this->timeoutSec
                          << "s timeout (bytecode shape likely defeats this lifter's assumptions; "
                             "raise with --timeout or use --format protectors/unpack instead)\n";
                std::cerr.flush();
                std::_Exit(124);
            }
        });
    }

    ~DecompileWatchdog() { stop(); }
    DecompileWatchdog(const DecompileWatchdog&) = delete;
    DecompileWatchdog& operator=(const DecompileWatchdog&) = delete;

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopped = true;
        }
        condition.notify_one();
        if (worker.joinable())
            worker.join();
    }

private:
    unsigned int timeoutSec;
    bool stopped = false;
    std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
};

struct Options { std::string input, output, format = "lua", cfg; bool analyze = false, compileSource = true, quiet = false, noColor = false; unsigned int timeoutSec = 20; };
static constexpr const char* VERSION = "ByteVeil 0.5.0";

static void usage(const char* n)
{
    std::cout << "ByteVeil - Luau bytecode decompiler and safe static analyzer\n\nUsage: " << n << " <input> [options]\n\nOptions:\n"
              << "  -o, --output FILE       Write output to FILE\n"
              << "  --format lua|luau|json|ir|structured|protectors|unpack|moonsec|moonsec-bytecode Output format (default: lua)\n"
              << "  --disassemble           Print deterministic low-level disassembly\n"
              << "  --dump-constants        Print constant table summary\n"
              << "  --dump-prototypes       Print prototype summary\n"
              << "  --cfg FILE              Write root CFG as Graphviz DOT\n"
              << "  --analyze               Print static indicators; never executes input\n"
              << "  --bytecode              Treat input as precompiled bytecode\n"
              << "  --source                Treat input as Luau source (default)\n"
              << "  --no-color              Disable colored diagnostics\n"
              << "  --timeout SECONDS       Abort decompile after SECONDS wall-clock (default: 20, 0 = no limit)\n"
              << "  -q, --quiet             Suppress informational messages\n"
              << "  --version               Show version\n  -h, --help              Show this help\n";
}

static bool readFile(const std::string& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); out = s.str(); return true; }
static bool writeFile(const std::string& p, const std::string& data) { std::ofstream f(p, std::ios::binary); if (!f) return false; f.write(data.data(), data.size()); return bool(f); }
static bool has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

static void indicator(std::ostream& out, const char* name, bool present, const char* evidence)
{
    out << name << ": " << (present ? "yes" : "no") << "\n";
    if (present) out << "  evidence: " << evidence << "\n";
}

static void analyze(const std::string& data, bool binary)
{
    size_t quoted = 0; bool inQuote = false; char quote = 0;
    for (char c : data) { if (!inQuote && (c == '\'' || c == '"')) { inQuote = true; quote = c; ++quoted; } else if (inQuote && c == quote) inQuote = false; }
    std::cout << "format: " << (binary ? "Luau bytecode" : "Lua/Luau source") << "\nbytes: " << data.size() << "\nquoted_strings: " << quoted << "\n";
    indicator(std::cout, "moonsec_marker", has(data, "MoonSec"), "MoonSec");
    indicator(std::cout, "dynamic_loader_markers", has(data, "getfenv") || has(data, "_ENV") || has(data, "setfenv") || has(data, "loadstring") || has(data, "load("), "load/getfenv/_ENV");
    indicator(std::cout, "network_or_executor_names_visible", has(data, "HttpGet") || has(data, "request") || has(data, "writefile"), "HttpGet/request/writefile");
    indicator(std::cout, "long_encoded_strings", data.size() >= 512 && quoted >= 8, "large input with many quoted strings");
    std::cout << "confidence: " << ((has(data, "MoonSec") || has(data, "loadstring") || has(data, "HttpGet")) ? "medium" : "low") << "\n";
}

static bool parse(int ac, char** av, Options& o)
{
    for (int i = 1; i < ac; ++i) {
        std::string a = av[i];
        if (a == "-h" || a == "--help") { usage(av[0]); std::exit(0); }
        if (a == "--version") { std::cout << VERSION << '\n'; std::exit(0); }
        if (a == "-o" || a == "--output" || a == "--cfg") { if (++i >= ac) return false; if (a == "--cfg") { o.cfg = av[i]; o.format = "cfg"; } else o.output = av[i]; }
        else if (a == "--format") {
            if (++i >= ac) return false;
            o.format = av[i];
            if (o.format != "lua" && o.format != "luau" && o.format != "json" && o.format != "ir" &&
                o.format != "disassemble" && o.format != "cfg" && o.format != "structured" && o.format != "constants" &&
                o.format != "prototypes" && o.format != "protectors" && o.format != "unpack" && o.format != "moonsec" &&
                o.format != "moonsec-bytecode") return false;
        }
        else if (a == "--disassemble") o.format = "disassemble";
        else if (a == "--dump-constants") o.format = "constants";
        else if (a == "--dump-prototypes") o.format = "prototypes";
        else if (a == "--analyze") o.analyze = true;
        else if (a == "--bytecode") o.compileSource = false;
        else if (a == "--source") o.compileSource = true;
        else if (a == "--no-color") o.noColor = true;
        else if (a == "--timeout") {
            if (++i >= ac) return false;
            char* end = nullptr;
            unsigned long timeout = std::strtoul(av[i], &end, 10);
            if (!end || *end != 0 || timeout > 86400) return false;
            o.timeoutSec = static_cast<unsigned int>(timeout);
        }
        else if (a == "-q" || a == "--quiet") o.quiet = true;
        else if (o.input.empty()) o.input = a;
        else return false;
    }
    return !o.input.empty();
}

int main(int ac, char** av)
{
    limitAddressSpace(2ull * 1024 * 1024 * 1024); // 2GiB, see limitAddressSpace() comment above
    if (ac == 1) { usage(av[0]); return 0; }
    Options o; if (!parse(ac, av, o)) { usage(av[0]); return 2; }
    std::string input; if (!readFile(o.input, input)) { std::cerr << "error: cannot read " << o.input << '\n'; return 1; }
    bool lua51 = ByteVeil::Lua51::isChunk(input);
    bool looks = input.size() >= 5 && input.compare(0, 5, "\x1bLuau", 5) == 0;
    bool binary = !o.compileSource || looks || lua51;
    if (o.analyze) {
        analyze(input, binary);
        return 0;
    }
    if (o.format == "protectors") {
        std::string result = ByteVeil::Protectors::analyze(input);
        if (o.output.empty()) std::cout << result;
        else if (!writeFile(o.output, result)) { std::cerr << "error: cannot write " << o.output << '\n'; return 1; }
        return 0;
    }
    if (o.format == "unpack") {
        std::string result = ByteVeil::Unpack::inspect(input);
        if (o.output.empty()) std::cout << result;
        else if (!writeFile(o.output, result)) { std::cerr << "error: cannot write " << o.output << '\n'; return 1; }
        return 0;
    }
    if (o.format == "moonsec") {
        std::string result = ByteVeil::MoonSec::inspect(input);
        if (o.output.empty()) std::cout << result;
        else if (!writeFile(o.output, result)) { std::cerr << "error: cannot write " << o.output << '\n'; return 1; }
        return 0;
    }
    if (o.format == "moonsec-bytecode") {
        if (o.output.empty()) {
            std::cerr << "error: --format moonsec-bytecode requires -o/--output for the binary serialized prototype\n";
            return 2;
        }
        ByteVeil::MoonSec::Payload payload;
        std::string error;
        if (!ByteVeil::MoonSec::extract(input, payload, error)) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        if (!writeFile(o.output, payload.bytes)) {
            std::cerr << "error: cannot write " << o.output << '\n';
            return 1;
        }
        if (!o.quiet)
            std::cerr << "wrote " << o.output << " (" << payload.bytes.size()
                      << " bytes; MoonSec virtual opcode mapping has not been applied)\n";
        return 0;
    }
    if (lua51) {
        std::string error;
        std::string mode = (o.format == "luau") ? "lua" : o.format;
        std::string result = ByteVeil::Lua51::inspect(input, mode, error);
        if (result.empty()) { std::cerr << "error: " << error << '\n'; return 1; }
        if (!o.cfg.empty() && o.format == "cfg" && !writeFile(o.cfg, result)) { std::cerr << "error: cannot write " << o.cfg << '\n'; return 1; }
        if (o.output.empty()) std::cout << result << (result.back() == '\n' ? "" : "\n");
        else if (!writeFile(o.output, result)) { std::cerr << "error: cannot write " << o.output << '\n'; return 1; }
        else if (!o.quiet) std::cerr << "wrote " << o.output << '\n';
        return 0;
    }
    std::string bc = input;
    if (!binary) {
        luau::IdentityEncoder encoder; bc = Luau::compile(input, {}, {}, &encoder);
        if (bc.empty()) { std::cerr << "error: source compilation failed: " << o.input << '\n'; return 1; }
    }
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
    if (!state) { std::cerr << "error: cannot create Luau state\n"; return 1; }
    luaL_openlibs(state.get()); luaL_sandbox(state.get()); luaL_sandboxthread(state.get());
    std::string error, result;
    if (o.format != "lua" && o.format != "luau") {
        result = inspectBytecode(state.get(), bc, o.format, error);
        if (result.empty()) { std::cerr << "error: " << error << '\n'; return 1; }
        if (!o.cfg.empty() && o.format == "cfg" && !writeFile(o.cfg, result)) { std::cerr << "error: cannot write " << o.cfg << '\n'; return 1; }
    } else {
        DecompileWatchdog watchdog(o.timeoutSec);
        try { result = Luau::Decompiler::decompile(state.get(), bc); }
        catch (const std::exception& e) { std::cerr << "error: decompiler exception: " << e.what() << '\n'; return 1; }
        catch (...) { std::cerr << "error: decompiler exception: unknown failure\n"; return 1; }
        watchdog.stop();
        if (result.empty() || result.rfind("error:", 0) == 0) { std::cerr << "error: " << (result.empty() ? "decompiler returned no output" : result) << '\n'; return 1; }
    }
    if (o.output.empty()) std::cout << result << (result.empty() || result.back() == '\n' ? "" : "\n");
    else if (!writeFile(o.output, result + (result.empty() || result.back() == '\n' ? "" : "\n"))) { std::cerr << "error: cannot write " << o.output << '\n'; return 1; }
    else if (!o.quiet) std::cerr << "wrote " << o.output << '\n';
    return 0;
}
