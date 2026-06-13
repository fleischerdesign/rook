#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "rook/sync/sync_engine.hpp"

namespace rook::core {
class DomainActor;
}

namespace rook::ports {
class LlmPort;
class ToolPort;
class StorePort;
class ExtensionPort;
}

namespace rook::adapters::server {

class GrpcServiceAdapter;

struct Tenant {
    std::string id;
    std::string data_dir;

    std::unique_ptr<rook::core::DomainActor> actor;
    std::unique_ptr<rook::sync::SyncEngine> sync;
    std::unique_ptr<rook::ports::LlmPort> llm;
    std::unique_ptr<rook::ports::ToolPort> tool;
    std::unique_ptr<rook::ports::StorePort> store;
    std::unique_ptr<rook::ports::ExtensionPort> extensions;

    std::unique_ptr<GrpcServiceAdapter> adapter;

    std::chrono::steady_clock::time_point last_access;
};

class TenantManager {
public:
    explicit TenantManager(std::string base_data_dir);

    Tenant* getOrCreate(const std::string& tenant_id);
    Tenant* get(const std::string& tenant_id);
    void purgeIdle(std::chrono::seconds max_idle);
    std::vector<std::string> listTenants() const;

    void shutdown();

private:
    std::unique_ptr<Tenant> createTenant(const std::string& tenant_id);

    std::string m_base_data_dir;
    std::unordered_map<std::string, std::unique_ptr<Tenant>> m_tenants;
    mutable std::mutex m_mutex;
};

} // namespace rook::adapters::server
