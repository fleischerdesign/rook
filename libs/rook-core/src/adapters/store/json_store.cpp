#include "rook/ports/store_port.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

namespace rook::adapters::store {

class JsonStoreAdapter final : public ports::StorePort {
public:
    explicit JsonStoreAdapter(std::filesystem::path base_dir)
        : m_base_dir(std::move(base_dir))
    {
        std::filesystem::create_directories(m_base_dir / "chats");
    }

    void saveChat(const ports::ChatRecord& record) override {
        nlohmann::json j;
        j["id"] = record.id;
        j["title"] = record.title;
        j["model"] = record.model;
        j["created_at"] = std::chrono::system_clock::to_time_t(record.created_at);
        j["updated_at"] = std::chrono::system_clock::to_time_t(record.updated_at);
        j["messages"] = nlohmann::json::parse(record.messages_json.empty() ? "[]" : record.messages_json);
        if (!record.active_skill_ids_json.empty())
            j["active_skill_ids"] = nlohmann::json::parse(record.active_skill_ids_json);
        j["pinned"] = record.pinned;
        j["pinned_at"] = record.pinned_at;

        auto path = chatPath(record.id);
        std::ofstream out(path);
        out << j.dump(2);

        updateIndex(record);
    }

    std::optional<ports::ChatRecord> loadChat(std::string_view id) override {
        auto path = chatPath(id);
        if (!std::filesystem::exists(path)) return std::nullopt;

        std::ifstream in(path);
        auto j = nlohmann::json::parse(in);

        ports::ChatRecord rec;
        rec.id = j["id"];
        rec.title = j["title"];
        rec.model = j.value("model", "");

        auto ca = j["created_at"].get<time_t>();
        rec.created_at = std::chrono::system_clock::from_time_t(ca);

        auto ua = j["updated_at"].get<time_t>();
        rec.updated_at = std::chrono::system_clock::from_time_t(ua);

        rec.messages_json = j["messages"].dump();
        if (j.contains("active_skill_ids"))
            rec.active_skill_ids_json = j["active_skill_ids"].dump();
        rec.pinned = j.value("pinned", false);
        rec.pinned_at = j.value("pinned_at", uint64_t{0});

        return rec;
    }

    std::vector<ports::ChatRecord> listChats() override {
        std::vector<ports::ChatRecord> result;
        auto indexPath = m_base_dir / "index.json";

        if (!std::filesystem::exists(indexPath)) return result;

        std::ifstream in(indexPath);
        auto j = nlohmann::json::parse(in);

        for (const auto& entry : j) {
            ports::ChatRecord rec;
            rec.id = entry["id"];
            rec.title = entry.value("title", "Untitled");
            rec.model = entry.value("model", "");
            result.push_back(rec);
        }

        return result;
    }

    void deleteChat(std::string_view id) override {
        std::filesystem::remove(chatPath(id));

        auto chats = listChats();
        std::erase_if(chats,
            [id](const auto& r) { return r.id == id; });

        nlohmann::json index = nlohmann::json::array();
        for (const auto& chat : chats) {
            index.push_back({
                {"id", chat.id},
                {"title", chat.title},
                {"model", chat.model},
            });
        }

        std::ofstream out(m_base_dir / "index.json");
        out << index.dump(2);
    }

    std::string loadConfig() override {
        auto path = m_base_dir / "config.json";
        if (!std::filesystem::exists(path)) return "{}";

        std::ifstream in(path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        );
    }

    void saveConfig(std::string_view json) override {
        std::ofstream out(m_base_dir / "config.json");
        out << json;
    }

private:
    std::filesystem::path m_base_dir;

    std::filesystem::path chatPath(std::string_view id) const {
        return m_base_dir / "chats" / (std::string(id) + ".json");
    }

    void updateIndex(const ports::ChatRecord& record) {
        nlohmann::json index = nlohmann::json::array();
        auto indexPath = m_base_dir / "index.json";

        if (std::filesystem::exists(indexPath)) {
            std::ifstream in(indexPath);
            index = nlohmann::json::parse(in);
        }

        bool found = false;
        for (auto& entry : index) {
            if (entry["id"] == record.id) {
                entry["title"] = record.title;
                found = true;
                break;
            }
        }

        if (!found) {
            index.push_back({
                {"id", record.id},
                {"title", record.title},
                {"model", record.model},
            });
        }

        std::ofstream out(indexPath);
        out << index.dump(2);
    }
};

std::unique_ptr<ports::StorePort> makeJsonStore(const std::string& base_dir) {
    return std::make_unique<JsonStoreAdapter>(base_dir);
}

} // namespace rook::adapters::store
