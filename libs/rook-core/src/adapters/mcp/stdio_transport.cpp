#include "rook/adapters/mcp/stdio_transport.hpp"
#include "rook/adapters/security/bwrap_executor.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>

namespace rook::adapters::mcp {

StdioTransport::StdioTransport(std::string command, std::vector<std::string> args)
    : m_command(std::move(command))
    , m_args(std::move(args))
{
}

StdioTransport::~StdioTransport()
{
    stop();
}

void StdioTransport::setMessageHandler(MessageHandler handler)
{
    m_handler = std::move(handler);
}

void StdioTransport::setSandbox(std::optional<security::Capability> cap)
{
    m_sandbox = std::move(cap);
}

void StdioTransport::start()
{
    if (m_running.exchange(true)) return;

    signal(SIGPIPE, SIG_IGN);

    int pipe_to_child[2];
    int pipe_from_child[2];

    if (pipe(pipe_to_child) == -1 || pipe(pipe_from_child) == -1) {
        spdlog::error("StdioTransport: pipe() failed: {}", strerror(errno));
        m_running = false;
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        spdlog::error("StdioTransport: fork() failed: {}", strerror(errno));
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        close(pipe_from_child[0]);
        close(pipe_from_child[1]);
        m_running = false;
        return;
    }

    if (pid == 0) {
        dup2(pipe_to_child[0], STDIN_FILENO);
        dup2(pipe_from_child[1], STDOUT_FILENO);

        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        close(pipe_from_child[0]);
        close(pipe_from_child[1]);

        if (m_sandbox.has_value()) {
            auto bwrap_argv = security::buildBwrapArgs(*m_sandbox, m_command, m_args);
            std::vector<char*> argv;
            for (auto& arg : bwrap_argv) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            execvp("bwrap", argv.data());

            if (errno == ENOENT) {
                spdlog::warn("StdioTransport: bwrap not found, falling back to direct exec");
            } else {
                spdlog::error("StdioTransport: execvp(bwrap) failed: {}", strerror(errno));
                _exit(1);
            }
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(m_command.c_str()));
        for (auto& arg : m_args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(m_command.c_str(), argv.data());

        spdlog::error("StdioTransport: execvp({}) failed: {}", m_command, strerror(errno));
        _exit(1);
    }

    close(pipe_to_child[0]);
    close(pipe_from_child[1]);

    m_stdin_fd = pipe_to_child[1];
    m_stdout_fd = pipe_from_child[0];
    m_child_pid = pid;

    m_reader_thread = std::thread(&StdioTransport::readerLoop, this);

    spdlog::info("StdioTransport: started {} (pid={})", m_command, pid);
}

void StdioTransport::send(std::string_view json)
{
    if (!m_running || m_stdin_fd == -1) return;

    static std::mutex write_mutex;
    std::lock_guard<std::mutex> lock(write_mutex);

    std::string payload(json);
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    const char* ptr = payload.data();
    size_t remaining = payload.size();

    while (remaining > 0) {
        ssize_t written = write(m_stdin_fd, ptr, remaining);
        if (written == -1) {
            if (errno == EPIPE) {
                spdlog::warn("StdioTransport: child closed stdin");
                stop();
                return;
            }
            if (errno == EINTR) continue;
            spdlog::error("StdioTransport: write() failed: {}", strerror(errno));
            return;
        }
        ptr += written;
        remaining -= written;
    }
}

void StdioTransport::stop()
{
    if (!m_running.exchange(false)) return;

    cleanupChild();

    if (m_stdin_fd != -1) {
        close(m_stdin_fd);
        m_stdin_fd = -1;
    }

    if (m_stdout_fd != -1) {
        close(m_stdout_fd);
        m_stdout_fd = -1;
    }

    if (m_reader_thread.joinable()) {
        m_reader_thread.join();
    }
}

void StdioTransport::readerLoop()
{
    std::string line;
    std::array<char, 4096> buf;
    struct pollfd pfd;
    pfd.fd = m_stdout_fd;
    pfd.events = POLLIN;

    while (m_running) {
        int ret = poll(&pfd, 1, 100);
        if (ret == -1) {
            if (errno == EINTR) continue;
            spdlog::error("StdioTransport: poll() failed: {}", strerror(errno));
            break;
        }

        if (ret == 0) continue;

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            spdlog::warn("StdioTransport: child stdout closed");
            break;
        }

        ssize_t n = read(m_stdout_fd, buf.data(), buf.size());
        if (n == -1) {
            if (errno == EINTR) continue;
            spdlog::error("StdioTransport: read() failed: {}", strerror(errno));
            break;
        }

        if (n == 0) {
            spdlog::info("StdioTransport: child stdout EOF");
            break;
        }

        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!line.empty() && m_handler) {
                    m_handler(line);
                }
                line.clear();
            } else {
                line.push_back(buf[i]);
            }
        }
    }
}

void StdioTransport::cleanupChild()
{
    if (m_child_pid == -1) return;

    kill(m_child_pid, SIGTERM);

    int status = 0;
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(m_child_pid, &status, WNOHANG);
        if (result == m_child_pid) break;
        if (result == -1) break;
        usleep(100000);
    }

    if (waitpid(m_child_pid, &status, WNOHANG) == 0) {
        spdlog::warn("StdioTransport: child {} did not exit, sending SIGKILL", m_child_pid);
        kill(m_child_pid, SIGKILL);
        waitpid(m_child_pid, &status, 0);
    }

    spdlog::info("StdioTransport: child {} exited with status {}", m_child_pid, status);
    m_child_pid = -1;
}

std::unique_ptr<McpTransport> makeStdioTransport(
    std::string command,
    std::vector<std::string> args)
{
    return std::make_unique<StdioTransport>(
        std::move(command), std::move(args));
}

} // namespace rook::adapters::mcp
