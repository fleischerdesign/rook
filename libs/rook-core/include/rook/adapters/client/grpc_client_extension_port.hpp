#pragma once

#include <memory>
#include <grpcpp/channel.h>
#include "rook/ports/extension_port.hpp"

namespace rook::adapters::client {

class GrpcClientExtensionPort : public rook::ports::ExtensionPort {
public:
    explicit GrpcClientExtensionPort(std::shared_ptr<grpc::Channel> channel);

    std::vector<rook::adapters::extension::InstalledExtension>
    listInstalled() override;

    bool install(std::string_view github_url) override;
    bool uninstall(std::string_view name) override;
    bool update(std::string_view name) override;

private:
    std::shared_ptr<grpc::Channel> m_channel;
};

} // namespace rook::adapters::client
