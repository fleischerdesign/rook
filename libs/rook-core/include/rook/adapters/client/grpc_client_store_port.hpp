#pragma once

#include <memory>
#include <grpcpp/channel.h>
#include "rook/ports/store_port.hpp"

namespace rook::adapters::client {

class GrpcClientStorePort : public rook::ports::StorePort {
public:
    explicit GrpcClientStorePort(std::shared_ptr<grpc::Channel> channel);

    void saveChat(const rook::ports::ChatRecord& record) override;
    std::optional<rook::ports::ChatRecord> loadChat(std::string_view id) override;
    std::vector<rook::ports::ChatRecord> listChats() override;
    void deleteChat(std::string_view id) override;
    std::string loadConfig() override;
    void saveConfig(std::string_view json) override;

private:
    std::shared_ptr<grpc::Channel> m_channel;
};

} // namespace rook::adapters::client
