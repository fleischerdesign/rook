#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>
#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace rook::adapters::audio {

namespace {

struct Ctx {
    std::FILE* file = nullptr;
    curl_off_t total = 0;
    curl_off_t downloaded = 0;
    std::atomic<bool>* abort = nullptr;
};

int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t, curl_off_t) {
    auto* ctx = static_cast<Ctx*>(clientp);
    if (ctx->abort && ctx->abort->load(std::memory_order_acquire))
        return 1;
    ctx->total = dltotal;
    ctx->downloaded = dlnow;
    return 0;
}

std::size_t writeCallback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<Ctx*>(userdata);
    if (!ctx->file) return 0;
    return std::fwrite(ptr, size, nmemb, ctx->file);
}

} // anonymous namespace

void downloadFile(std::string_view url, std::string_view dest_path,
                  ProgressFn on_progress, DoneFn on_done) {
    std::thread([url = std::string(url), dest = std::string(dest_path),
                 on_progress, on_done]() mutable {
        auto tmp = dest + ".part";

        std::filesystem::create_directories(
            std::filesystem::path(tmp).parent_path());

        auto* file = std::fopen(tmp.c_str(), "wb");
        if (!file) {
            SPDLOG_ERROR("ModelDownloader: cannot open {}", tmp);
            on_done(false);
            return;
        }

        auto* handle = curl_easy_init();
        if (!handle) {
            SPDLOG_ERROR("ModelDownloader: curl_easy_init failed");
            std::fclose(file);
            std::filesystem::remove(tmp);
            on_done(false);
            return;
        }

        std::atomic<bool> abort{false};

        Ctx ctx;
        ctx.file = file;
        ctx.abort = &abort;

        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 3600L);

        SPDLOG_INFO("ModelDownloader: downloading {} → {}", url, tmp);

        auto res = curl_easy_perform(handle);

        long http_status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_status);

        curl_easy_cleanup(handle);
        std::fclose(file);

        if (res != CURLE_OK) {
            SPDLOG_ERROR("ModelDownloader: download failed: {} (HTTP {})",
                         curl_easy_strerror(res), http_status);
            std::filesystem::remove(tmp);
            on_done(false);
            return;
        }

        std::filesystem::rename(tmp, dest);
        SPDLOG_INFO("ModelDownloader: downloaded {} successfully", dest);

        if (on_progress)
            on_progress(1.0f);
        on_done(true);
    }).detach();
}

} // namespace rook::adapters::audio
