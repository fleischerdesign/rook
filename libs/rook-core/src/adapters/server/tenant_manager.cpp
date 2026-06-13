#include "rook/adapters/server/tenant_manager.hpp"
#include "rook/adapters/server/grpc_service_adapter.hpp"
#include "rook/core/domain_actor.hpp"
#include "rook/sync/sync_engine.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/tool_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/builtin/builtin_tool_port.hpp"
#include "rook/adapters/composite/composite_tool_port.hpp"
#include "rook/adapters/mcp/null_tool_port.hpp"
#include "rook/core/settings.hpp"
#include <random>
#include <spdlog/spdlog.h>

namespace rook::adapters::server {

static std::string generateNodeId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";

    std::string id(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        int r = dis(gen);
        if (i == 14) r = 4;
        if (i == 19) r = (r & 3) | 8;
        id[i] = hex[r];
    }
    return id;
}

TenantManager::TenantManager(std::string base_data_dir)
    : m_base_data_dir(std::move(base_data_dir))
{
}

Tenant* TenantManager::getOrCreate(const std::string& tenant_id)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_tenants.find(tenant_id);
        if (it != m_tenants.end()) {
            it->second->last_access = std::chrono::steady_clock::now();
            return it->second.get();
        }
    }

    auto tenant = createTenant(tenant_id);

    std::lock_guard lock(m_mutex);
    auto [it, _] = m_tenants.emplace(tenant_id, std::move(tenant));
    it->second->last_access = std::chrono::steady_clock::now();
    return it->second.get();
}

Tenant* TenantManager::get(const std::string& tenant_id)
{
    std::lock_guard lock(m_mutex);
    auto it = m_tenants.find(tenant_id);
    if (it != m_tenants.end()) {
        it->second->last_access = std::chrono::steady_clock::now();
        return it->second.get();
    }
    return nullptr;
}

void TenantManager::purgeIdle(std::chrono::seconds max_idle)
{
    auto now = std::chrono::steady_clock::now();

    std::lock_guard lock(m_mutex);
    for (auto it = m_tenants.begin(); it != m_tenants.end();) {
        if (now - it->second->last_access > max_idle) {
            if (it->second->actor)
                it->second->actor->stop();
            SPDLOG_INFO("TenantManager: purged idle tenant {}", it->first);
            it = m_tenants.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::string> TenantManager::listTenants() const
{
    std::lock_guard lock(m_mutex);
    std::vector<std::string> result;
    for (const auto& [id, _] : m_tenants)
        result.push_back(id);
    return result;
}

void TenantManager::shutdown()
{
    std::lock_guard lock(m_mutex);
    for (auto& [id, tenant] : m_tenants) {
        if (tenant->actor)
            tenant->actor->stop();
        SPDLOG_INFO("TenantManager: stopped tenant {}", id);
    }
    m_tenants.clear();
}

std::unique_ptr<Tenant> TenantManager::createTenant(
    const std::string& tenant_id)
{
    auto tenant = std::make_unique<Tenant>();
    tenant->id = tenant_id;
    tenant->data_dir = m_base_data_dir + "/tenants/" + tenant_id;

    tenant->store = rook::adapters::store::makeJsonStore(tenant->data_dir);

    tenant->llm = rook::adapters::llm::makeMultiProviderAdapter();

    tenant->tool = std::make_unique<
        rook::adapters::composite::CompositeToolPort>();

    tenant->sync = std::make_unique<rook::sync::SyncEngine>(generateNodeId());

    tenant->actor = std::make_unique<rook::core::DomainActor>();

    tenant->actor->start(
        *tenant->llm,
        *tenant->tool,
        nullptr,
        tenant->store.get(),
        [](rook::domain::DomainEvent) {},
        nullptr,
        nullptr);

    tenant->adapter = std::make_unique<GrpcServiceAdapter>(
        *tenant->actor,
        *tenant->sync,
        *tenant->store,
        tenant->extensions.get());

    SPDLOG_INFO("TenantManager: created tenant {}", tenant_id);
    return tenant;
}

} // namespace rook::adapters::server
