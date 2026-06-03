#include "rook/adapters/builtin/builtin_tool_port.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rook::adapters::builtin {

BuiltinToolPort::BuiltinToolPort() = default;

std::vector<rook::ports::ToolDefinition> BuiltinToolPort::listTools()
{
    return {
        {
            .name = "read_file",
            .description = "Read the contents of a file at the given path",
            .parameters = {
                {"path", "string", "Absolute path to the file", true},
                {"offset", "integer", "Line number to start reading from (optional)", false},
                {"limit", "integer", "Maximum number of lines to read (optional)", false},
            },
            .source = "builtin",
        },
        {
            .name = "write_file",
            .description = "Create or overwrite a file at the given path with the given content",
            .parameters = {
                {"path", "string", "Absolute path to the file", true},
                {"content", "string", "Content to write", true},
            },
            .source = "builtin",
        },
        {
            .name = "list_directory",
            .description = "List files and directories at the given path",
            .parameters = {
                {"path", "string", "Absolute path to the directory", true},
            },
            .source = "builtin",
        },
    };
}

static std::string readAll(int fd)
{
    std::string result;
    std::array<char, 4096> buf;
    for (;;) {
        ssize_t n = read(fd, buf.data(), buf.size());
        if (n == 0) break;
        if (n == -1) {
            if (errno == EINTR) continue;
            spdlog::error("Builtin: read failed: {}", strerror(errno));
            return "";
        }
        result.append(buf.data(), n);
        if (result.size() > 1024 * 1024) break;
    }
    return result;
}

rook::ports::ToolResult BuiltinToolPort::readFile(nlohmann::json args)
{
    rook::ports::ToolResult result;

    auto path = args.value("path", "");
    if (path.empty()) {
        result.is_error = true;
        result.content = "path is required";
        return result;
    }

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        result.is_error = true;
        result.content = std::string("cannot open ") + path + ": " + strerror(errno);
        return result;
    }

    result.content = readAll(fd);
    ::close(fd);

    return result;
}

rook::ports::ToolResult BuiltinToolPort::writeFile(nlohmann::json args)
{
    rook::ports::ToolResult result;

    auto path = args.value("path", "");
    auto content = args.value("content", "");

    if (path.empty()) {
        result.is_error = true;
        result.content = "path is required";
        return result;
    }

    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd == -1) {
        result.is_error = true;
        result.content = std::string("cannot write ") + path + ": " + strerror(errno);
        return result;
    }

    const char* ptr = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t n = write(fd, ptr, remaining);
        if (n == -1) {
            if (errno == EINTR) continue;
            ::close(fd);
            result.is_error = true;
            result.content = std::string("write failed: ") + strerror(errno);
            return result;
        }
        ptr += n;
        remaining -= n;
    }

    ::close(fd);
    result.content = "wrote " + std::to_string(content.size()) + " bytes to " + path;
    return result;
}

rook::ports::ToolResult BuiltinToolPort::listDirectory(nlohmann::json args)
{
    rook::ports::ToolResult result;

    auto path = args.value("path", "");

    if (path.empty()) {
        result.is_error = true;
        result.content = "path is required";
        return result;
    }

    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        result.is_error = true;
        result.content = std::string("cannot open directory ") + path + ": " + strerror(errno);
        return result;
    }

    std::string listing;
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;

        std::string full = path;
        if (full.back() != '/') full += '/';
        full += name;

        struct stat st;
        if (::lstat(full.c_str(), &st) == 0) {
            listing += (S_ISDIR(st.st_mode) ? "d" : "-");
            listing += (st.st_mode & S_IRUSR ? "r" : "-");
            listing += (st.st_mode & S_IWUSR ? "w" : "-");
            listing += (st.st_mode & S_IXUSR ? "x" : "-");
            listing += " " + std::to_string(st.st_size) + " " + name + "\n";
        } else {
            listing += "? " + name + "\n";
        }
    }

    ::closedir(dir);

    if (listing.empty()) listing = "(empty directory)";
    result.content = listing;
    return result;
}

rook::ports::ToolResult BuiltinToolPort::execute(const rook::ports::ToolCall& call)
{
    rook::ports::ToolResult result;
    result.id = call.id;

    nlohmann::json args;
    if (!call.arguments.empty()) {
        try {
            args = nlohmann::json::parse(call.arguments);
        } catch (...) {
            result.is_error = true;
            result.content = "invalid arguments JSON";
            return result;
        }
    }

    spdlog::info("Builtin: executing {}", call.name);

    if (call.name == "read_file") return readFile(std::move(args));
    if (call.name == "write_file") return writeFile(std::move(args));
    if (call.name == "list_directory") return listDirectory(std::move(args));

    result.is_error = true;
    result.content = "unknown builtin tool: " + call.name;
    return result;
}

std::unique_ptr<rook::ports::ToolPort> makeBuiltinToolPort()
{
    return std::make_unique<BuiltinToolPort>();
}

} // namespace rook::adapters::builtin
