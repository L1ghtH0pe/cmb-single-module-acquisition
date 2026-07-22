#include "common/logger.h"
#include "common/metrics.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

struct ProcessHandle {
#ifdef _WIN32
    PROCESS_INFORMATION info{};
#else
    pid_t pid{-1};
#endif
    int exit_code{1};
};

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::size_t count_raw_frames(const fs::path& dir) {
    std::size_t count = 0;
    if (!fs::exists(dir)) {
        return 0;
    }
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().filename().string().rfind("frame-", 0) == 0) {
            ++count;
        }
    }
    return count;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

fs::path exe_name(const std::string& base) {
#ifdef _WIN32
    return fs::path(base + ".exe");
#else
    return fs::path(base);
#endif
}

#ifdef _WIN32
ProcessHandle start_process(const fs::path& exe, const std::vector<std::string>& args, const fs::path& stdout_path, const fs::path& stderr_path) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    auto open_output = [&](const fs::path& path) -> HANDLE {
        return CreateFileA(path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    };

    HANDLE out_handle = open_output(stdout_path);
    HANDLE err_handle = open_output(stderr_path);
    if (out_handle == INVALID_HANDLE_VALUE || err_handle == INVALID_HANDLE_VALUE) {
        if (out_handle != INVALID_HANDLE_VALUE) CloseHandle(out_handle);
        if (err_handle != INVALID_HANDLE_VALUE) CloseHandle(err_handle);
        return {};
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_handle;
    si.hStdError = err_handle;

    std::ostringstream cmd;
    cmd << '"' << exe.string() << '"';
    for (const auto& arg : args) {
        cmd << ' ' << '"' << arg << '"';
    }
    std::string cmdline = cmd.str();

    ProcessHandle handle{};
    BOOL ok = CreateProcessA(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &si,
        &handle.info);

    CloseHandle(out_handle);
    CloseHandle(err_handle);

    if (!ok) {
        return {};
    }

    return handle;
}

int wait_process(ProcessHandle& handle) {
    WaitForSingleObject(handle.info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(handle.info.hProcess, &code);
    CloseHandle(handle.info.hThread);
    CloseHandle(handle.info.hProcess);
    handle.exit_code = static_cast<int>(code);
    return handle.exit_code;
}
#else
ProcessHandle start_process(const fs::path& exe, const std::vector<std::string>& args, const fs::path& stdout_path, const fs::path& stderr_path) {
    ProcessHandle handle{};
    pid_t pid = fork();
    if (pid < 0) {
        return handle;
    }
    if (pid == 0) {
        int out_fd = ::open(stdout_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        int err_fd = ::open(stderr_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (out_fd < 0 || err_fd < 0) {
            _exit(1);
        }
        dup2(out_fd, STDOUT_FILENO);
        dup2(err_fd, STDERR_FILENO);
        close(out_fd);
        close(err_fd);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(1);
    }

    handle.pid = pid;
    return handle;
}

int wait_process(ProcessHandle& handle) {
    int status = 1;
    waitpid(handle.pid, &status, 0);
    if (WIFEXITED(status)) {
        handle.exit_code = WEXITSTATUS(status);
    }
    return handle.exit_code;
}
#endif

void clean_runtime_dirs() {
    fs::remove_all("logs");
    fs::remove_all("captures/raw");
    fs::create_directories("logs");
    fs::create_directories("captures/raw");
    fs::create_directories("captures/meta");
}

}  // namespace

int main(int argc, char** argv) {
    const auto port = static_cast<std::uint16_t>(argc > 1 ? std::stoi(argv[1]) : 9000);
    const auto frames = static_cast<std::uint64_t>(argc > 2 ? std::stoull(argv[2]) : 100);

    clean_runtime_dirs();

    const auto receiver_exe = exe_name("receiver");
    const auto sender_exe = exe_name("sender");

    std::cout << "starting receiver: " << receiver_exe.string() << '\n';
    auto receiver = start_process(receiver_exe, {std::to_string(port), std::to_string(frames)}, "logs/receiver-stdout.txt", "logs/receiver-stderr.txt");
    if (
#ifdef _WIN32
        receiver.info.hProcess == nullptr
#else
        receiver.pid < 0
#endif
    ) {
        std::cerr << "failed to start receiver\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "starting sender: " << sender_exe.string() << '\n';
    auto sender = start_process(sender_exe, {"127.0.0.1", std::to_string(port), std::to_string(frames)}, "logs/sender-stdout.txt", "logs/sender-stderr.txt");
    if (
#ifdef _WIN32
        sender.info.hProcess == nullptr
#else
        sender.pid < 0
#endif
    ) {
        std::cerr << "failed to start sender\n";
        return 1;
    }

    wait_process(sender);
    wait_process(receiver);

    const auto sender_stdout = read_file("logs/sender-stdout.txt");
    const auto receiver_stdout = read_file("logs/receiver-stdout.txt");

    const auto sender_metrics = fs::exists("logs/sender-metrics.csv");
    const auto receiver_metrics = fs::exists("logs/receiver-metrics.csv");
    const auto raw_count = count_raw_frames("captures/raw");

    std::cout << "sender_exit=" << sender.exit_code << '\n';
    std::cout << "receiver_exit=" << receiver.exit_code << '\n';
    std::cout << "raw_files=" << raw_count << '\n';
    std::cout << "sender_metrics=" << (sender_metrics ? "yes" : "no") << '\n';
    std::cout << "receiver_metrics=" << (receiver_metrics ? "yes" : "no") << '\n';
    std::cout << "--- sender stdout ---\n" << sender_stdout;
    std::cout << "--- receiver stdout ---\n" << receiver_stdout;

    const bool ok = sender.exit_code == 0 && receiver.exit_code == 0 && raw_count == frames && sender_metrics && receiver_metrics && contains(sender_stdout, "sender sent ") && contains(receiver_stdout, "receiver received ");
    if (!ok) {
        std::cerr << "local_process_smoke failed\n";
        return 1;
    }

    std::cout << "local_process_smoke passed\n";
    return 0;
}
