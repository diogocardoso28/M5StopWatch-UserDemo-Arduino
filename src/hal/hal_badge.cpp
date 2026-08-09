/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <assets/assets.h>
#include <esp_http_server.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <generated/embedded_assets.h>
#include <mooncake_log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr std::string_view tag = "HAL-Badge";
constexpr const char* badgeDir = "/spiflash/badge";
constexpr const char* activeSlotFile = "/spiflash/badge/active_slot.txt";
constexpr std::size_t badgeSlotCount = 6;
constexpr std::size_t maxUploadBytes = 2 * 1024 * 1024;

std::string slotMetaPath(std::size_t slot)
{
    char path[96] = {};
    std::snprintf(path, sizeof(path), "%s/slot_%u.meta", badgeDir, static_cast<unsigned>(slot));
    return path;
}

std::string slotImagePath(std::size_t slot, std::string_view extension)
{
    char path[96] = {};
    std::snprintf(path, sizeof(path), "%s/slot_%u.%.*s", badgeDir, static_cast<unsigned>(slot),
                  static_cast<int>(extension.size()), extension.data());
    return path;
}

bool writeTextFile(const std::string& path, std::string_view text)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const bool closed = std::fclose(file) == 0;
    return written == text.size() && closed;
}

std::string readTextFile(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    char buffer[32] = {};
    const std::size_t size = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    std::string value(buffer, size);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

bool ensureBadgeDir()
{
    if (::mkdir(badgeDir, 0775) != 0 && errno != EEXIST) {
        mclog::tagError(tag, "failed to create {}, errno={}", badgeDir, errno);
        return false;
    }
    return true;
}

std::string normalizeExtension(std::string fileName, std::string contentType)
{
    auto normalize = [](std::string& text) {
        for (char& character : text) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
    };

    normalize(fileName);
    normalize(contentType);

    const auto dot = fileName.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string extension = fileName.substr(dot + 1);
        if (extension == "png" || extension == "jpg" || extension == "jpeg") {
            return extension == "jpeg" ? "jpg" : extension;
        }
    }
    if (contentType.find("png") != std::string::npos) {
        return "png";
    }
    if (contentType.find("jpeg") != std::string::npos || contentType.find("jpg") != std::string::npos) {
        return "jpg";
    }
    return {};
}

std::string readSlotExtension(std::size_t slot)
{
    const std::string meta = readTextFile(slotMetaPath(slot));
    if (meta == "jpg" || meta == "png") {
        return meta;
    }
    for (const char* extension : {"png", "jpg", "jpeg"}) {
        if (::access(slotImagePath(slot, extension).c_str(), R_OK) == 0) {
            return std::strcmp(extension, "jpeg") == 0 ? "jpg" : extension;
        }
    }
    return {};
}

bool slotAvailable(std::size_t slot)
{
    return slot < badgeSlotCount && !readSlotExtension(slot).empty();
}

std::size_t readActiveSlot()
{
    const std::string value = readTextFile(activeSlotFile);
    if (value.empty()) {
        return 0;
    }
    char* end = nullptr;
    const unsigned long slot = std::strtoul(value.c_str(), &end, 10);
    return end != value.c_str() && slot < badgeSlotCount ? static_cast<std::size_t>(slot) : 0;
}

bool persistActiveSlot(std::size_t slot)
{
    return slot < badgeSlotCount && writeTextFile(activeSlotFile, std::to_string(slot));
}

std::size_t findFirstAvailableSlot()
{
    std::array<bool, badgeSlotCount> available{};
    for (std::size_t slot = 0; slot < badgeSlotCount; ++slot) {
        available[slot] = slotAvailable(slot);
    }
    return stopwatch_core::findAvailableBadgeSlot(available, badgeSlotCount - 1, 1);
}

std::size_t findAvailableSlot(std::size_t current, int direction)
{
    std::array<bool, badgeSlotCount> available{};
    for (std::size_t slot = 0; slot < badgeSlotCount; ++slot) {
        available[slot] = slotAvailable(slot);
    }
    return stopwatch_core::findAvailableBadgeSlot(available, current, direction);
}

bool loadSlot(lv_obj_t* image, std::size_t slot)
{
    if (image == nullptr || slot >= badgeSlotCount) {
        return false;
    }
    const std::string extension = readSlotExtension(slot);
    if (extension.empty()) {
        return false;
    }
    const std::string path = slotImagePath(slot, extension);
    if (::access(path.c_str(), R_OK) != 0) {
        return false;
    }

    const std::string lvglPath = "A:" + path;
    lv_image_set_src(image, lvglPath.c_str());
    persistActiveSlot(slot);
    return true;
}

bool deleteSlot(std::size_t slot, std::string& message)
{
    if (slot >= badgeSlotCount) {
        message = "invalid badge slot";
        return false;
    }
    bool deleted = false;
    for (const char* extension : {"jpg", "jpeg", "png"}) {
        if (::unlink(slotImagePath(slot, extension).c_str()) == 0) {
            deleted = true;
        }
    }
    deleted = ::unlink(slotMetaPath(slot).c_str()) == 0 || deleted;
    if (!deleted) {
        message = "badge image not found";
        return false;
    }
    if (readActiveSlot() == slot) {
        const std::size_t fallback = findFirstAvailableSlot();
        if (fallback < badgeSlotCount) {
            persistActiveSlot(fallback);
        } else {
            ::unlink(activeSlotFile);
        }
    }
    message = "badge image deleted";
    return true;
}

bool replaceUploadedJpeg(std::size_t slot, const std::string& tempPath, std::string& message)
{
    const std::string finalPath = slotImagePath(slot, "jpg");
    if (::unlink(finalPath.c_str()) != 0 && errno != ENOENT) {
        ::unlink(tempPath.c_str());
        message = "failed to replace existing image";
        return false;
    }
    if (::rename(tempPath.c_str(), finalPath.c_str()) != 0) {
        ::unlink(tempPath.c_str());
        message = "failed to finalize image";
        return false;
    }
    ::unlink(slotImagePath(slot, "png").c_str());

    if (!writeTextFile(slotMetaPath(slot), "jpg")) {
        message = "failed to store image metadata";
        return false;
    }
    if (!persistActiveSlot(slot)) {
        message = "failed to store active slot";
        return false;
    }
    message = "upload success";
    return true;
}

const char* contentTypeForExtension(std::string_view extension)
{
    return extension == "png" ? "image/png" : "image/jpeg";
}

class BadgeApSession {
public:
    explicit BadgeApSession(std::function<void(std::string_view)> onLog) : _onLog(std::move(onLog)) {}

    bool run()
    {
        _ssid = makeSsid();
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false);
        const IPAddress apIp(192, 168, 4, 1);
        const IPAddress gateway(192, 168, 4, 1);
        const IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIp, gateway, subnet);
        if (!WiFi.softAP(_ssid.c_str(), nullptr, 1, false, 4)) {
            log("Failed to start Wi-Fi access point");
            WiFi.mode(WIFI_OFF);
            return false;
        }
        _dns.start(53, "*", apIp);
        if (!startServer()) {
            stop();
            return false;
        }

        log("Connect to Wi-Fi: " + _ssid + "\nAnd open page:\nhttp://192.168.4.1");
        while (!_closeRequested.load()) {
            _dns.processNextRequest();
            GetHAL().feedTheDog();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        stop();
        log("Exited edit mode");
        return true;
    }

private:
    static BadgeApSession* from(httpd_req_t* request)
    {
        return static_cast<BadgeApSession*>(request->user_ctx);
    }

    void log(const std::string& message) const
    {
        mclog::tagInfo(tag, "{}", message);
        if (_onLog) {
            _onLog(message);
        }
    }

    static std::string makeSsid()
    {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        char ssid[32] = {};
        std::snprintf(ssid, sizeof(ssid), "M5StopWatch-%02X%02X", mac[4], mac[5]);
        return ssid;
    }

    static bool parseSlot(httpd_req_t* request, std::size_t& slot)
    {
        char query[64] = {};
        if (httpd_req_get_url_query_len(request) <= 0 ||
            httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
            return false;
        }
        char value[16] = {};
        if (httpd_query_key_value(query, "slot", value, sizeof(value)) != ESP_OK) {
            return false;
        }
        slot = static_cast<std::size_t>(std::strtoul(value, nullptr, 10));
        return true;
    }

    static void sendJson(httpd_req_t* request, const std::string& body)
    {
        httpd_resp_set_type(request, "application/json");
        httpd_resp_send(request, body.c_str(), body.size());
    }

    static std::string messageJson(std::string_view message)
    {
        return "{\"status\":\"ok\",\"message\":\"" + std::string(message) + "\"}";
    }

    bool startServer()
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 24;
        config.recv_wait_timeout = 15;
        config.send_wait_timeout = 15;
        config.uri_match_fn = httpd_uri_match_wildcard;
        if (httpd_start(&_server, &config) != ESP_OK) {
            log("Failed to start Badge web server");
            return false;
        }

        const std::array<httpd_uri_t, 7> routes = {{
            {.uri = "/", .method = HTTP_GET, .handler = handleIndex, .user_ctx = this},
            {.uri = "/badge/state", .method = HTTP_GET, .handler = handleState, .user_ctx = this},
            {.uri = "/badge/image", .method = HTTP_GET, .handler = handleImage, .user_ctx = this},
            {.uri = "/badge/image", .method = HTTP_DELETE, .handler = handleDelete, .user_ctx = this},
            {.uri = "/badge/active", .method = HTTP_POST, .handler = handleSetActive, .user_ctx = this},
            {.uri = "/upload", .method = HTTP_POST, .handler = handleUpload, .user_ctx = this},
            {.uri = "/close", .method = HTTP_POST, .handler = handleClose, .user_ctx = this},
        }};
        for (const auto& route : routes) {
            if (httpd_register_uri_handler(_server, &route) != ESP_OK) {
                log("Failed to register Badge web route");
                return false;
            }
        }

        static constexpr std::array<const char*, 10> captivePortalUrls = {
            "/hotspot-detect.html",      "/generate_204*", "/mobile/status.php",
            "/check_network_status.txt", "/ncsi.txt",      "/fwlink/",
            "/connectivity-check.html",  "/success.txt",   "/portal.html",
            "/library/test/success.html",
        };
        httpd_uri_t captiveRoute = {
            .uri = nullptr,
            .method = HTTP_GET,
            .handler = handleCaptive,
            .user_ctx = this,
        };
        for (const char* url : captivePortalUrls) {
            captiveRoute.uri = url;
            if (httpd_register_uri_handler(_server, &captiveRoute) != ESP_OK) {
                log("Failed to register Badge web route");
                return false;
            }
        }
        return true;
    }

    void stop()
    {
        if (_server != nullptr) {
            httpd_stop(_server);
            _server = nullptr;
        }
        _dns.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    static esp_err_t handleIndex(httpd_req_t* request)
    {
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        httpd_resp_send(request, reinterpret_cast<const char*>(embedded_assets::badgeConfigHtml),
                        embedded_assets::badgeConfigHtmlSize);
        return ESP_OK;
    }

    static esp_err_t handleState(httpd_req_t* request)
    {
        auto* self = from(request);
        const std::size_t active = readActiveSlot();
        std::string json = "{\"apSsid\":\"" + self->_ssid +
                           "\",\"apUrl\":\"http://192.168.4.1\",\"slotCount\":" +
                           std::to_string(badgeSlotCount) + ",\"activeSlot\":" + std::to_string(active) +
                           ",\"slots\":[";
        for (std::size_t slot = 0; slot < badgeSlotCount; ++slot) {
            if (slot != 0) {
                json += ',';
            }
            const bool hasImage = slotAvailable(slot);
            json += "{\"slot\":" + std::to_string(slot) + ",\"hasImage\":" +
                    (hasImage ? "true" : "false") + ",\"isActive\":" +
                    (slot == active ? "true" : "false") + ",\"imageUrl\":\"/badge/image?slot=" +
                    std::to_string(slot) + "\"}";
        }
        json += "]}";
        sendJson(request, json);
        return ESP_OK;
    }

    static esp_err_t handleImage(httpd_req_t* request)
    {
        std::size_t slot = 0;
        if (!parseSlot(request, slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing slot");
            return ESP_FAIL;
        }
        if (slot >= badgeSlotCount) {
            httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "invalid badge slot");
            return ESP_FAIL;
        }
        std::string extension = readSlotExtension(slot);
        if (extension.empty()) {
            httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "badge image not found");
            return ESP_FAIL;
        }
        const std::string path = slotImagePath(slot, extension);
        FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "failed to read image file");
            return ESP_FAIL;
        }

        httpd_resp_set_type(request, contentTypeForExtension(extension));
        std::array<char, 4096> buffer{};
        esp_err_t result = ESP_OK;
        while (const std::size_t size = std::fread(buffer.data(), 1, buffer.size(), file)) {
            if (httpd_resp_send_chunk(request, buffer.data(), size) != ESP_OK) {
                result = ESP_FAIL;
                break;
            }
        }
        std::fclose(file);
        if (result == ESP_OK) {
            httpd_resp_send_chunk(request, nullptr, 0);
        }
        return result;
    }

    static esp_err_t handleSetActive(httpd_req_t* request)
    {
        std::size_t slot = 0;
        if (!parseSlot(request, slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing slot");
            return ESP_FAIL;
        }
        if (slot >= badgeSlotCount) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid badge slot");
            return ESP_FAIL;
        }
        if (!slotAvailable(slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "badge image not found");
            return ESP_FAIL;
        }
        if (!persistActiveSlot(slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "failed to update active slot");
            return ESP_FAIL;
        }
        sendJson(request, messageJson("active slot updated"));
        return ESP_OK;
    }

    static esp_err_t handleDelete(httpd_req_t* request)
    {
        std::size_t slot = 0;
        std::string message;
        if (!parseSlot(request, slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing slot");
            return ESP_FAIL;
        }
        if (!deleteSlot(slot, message)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, message.empty() ? "delete failed" : message.c_str());
            return ESP_FAIL;
        }
        sendJson(request, messageJson(message));
        return ESP_OK;
    }

    static esp_err_t handleUpload(httpd_req_t* request)
    {
        if (request->content_len <= 0 || static_cast<std::size_t>(request->content_len) > maxUploadBytes) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid upload size");
            return ESP_FAIL;
        }

        std::size_t slot = 0;
        if (!parseSlot(request, slot)) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing slot");
            return ESP_FAIL;
        }
        if (slot >= badgeSlotCount) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid badge slot");
            return ESP_FAIL;
        }

        std::string fileName;
        const int headerLength = httpd_req_get_hdr_value_len(request, "X-File-Name");
        if (headerLength > 0) {
            fileName.resize(static_cast<std::size_t>(headerLength) + 1);
            if (httpd_req_get_hdr_value_str(request, "X-File-Name", fileName.data(), headerLength + 1) == ESP_OK) {
                fileName.resize(static_cast<std::size_t>(headerLength));
            } else {
                fileName.clear();
            }
        }
        if (fileName.empty()) {
            fileName = "badge.jpg";
        }

        char contentType[64] = {};
        httpd_req_get_hdr_value_str(request, "Content-Type", contentType, sizeof(contentType));
        if (normalizeExtension(fileName, contentType) != "jpg") {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "only jpg images are supported");
            return ESP_FAIL;
        }
        if (!ensureBadgeDir()) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "failed to create badge directory");
            return ESP_FAIL;
        }

        const std::string finalPath = slotImagePath(slot, "jpg");
        const std::string tempPath = finalPath + ".tmp";
        ::unlink(tempPath.c_str());
        FILE* file = std::fopen(tempPath.c_str(), "wb");
        if (file == nullptr) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "failed to store image");
            return ESP_FAIL;
        }

        std::array<uint8_t, 4096> buffer{};
        std::size_t total = 0;
        bool failed = false;
        while (total < static_cast<std::size_t>(request->content_len)) {
            const std::size_t remaining = static_cast<std::size_t>(request->content_len) - total;
            const int received = httpd_req_recv(request, reinterpret_cast<char*>(buffer.data()),
                                                std::min(buffer.size(), remaining));
            if (received <= 0) {
                mclog::tagWarn(tag, "badge upload interrupted after {} of {} bytes, recv={}", total,
                               request->content_len, received);
                std::fclose(file);
                ::unlink(tempPath.c_str());
                httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read upload body");
                return ESP_FAIL;
            }
            if (std::fwrite(buffer.data(), 1, received, file) != static_cast<std::size_t>(received)) {
                failed = true;
                break;
            }
            total += static_cast<std::size_t>(received);
        }
        failed = std::fclose(file) != 0 || failed;
        if (failed || total != static_cast<std::size_t>(request->content_len)) {
            ::unlink(tempPath.c_str());
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "failed to store image");
            return ESP_FAIL;
        }

        std::string message;
        if (!replaceUploadedJpeg(slot, tempPath, message)) {
            ::unlink(tempPath.c_str());
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, message.empty() ? "save failed" : message.c_str());
            return ESP_FAIL;
        }
        sendJson(request, messageJson(message));
        return ESP_OK;
    }

    static esp_err_t handleClose(httpd_req_t* request)
    {
        auto* self = from(request);
        self->_closeRequested.store(true);
        httpd_resp_sendstr(request, "closing");
        return ESP_OK;
    }

    static esp_err_t handleCaptive(httpd_req_t* request)
    {
        auto* self = from(request);
        const std::string url = "http://192.168.4.1/?_=" + std::to_string(esp_timer_get_time());
        self->log("Received captive portal probe, redirecting to upload page");
        httpd_resp_set_type(request, "text/html");
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", url.c_str());
        httpd_resp_set_hdr(request, "Connection", "close");
        httpd_resp_send(request, nullptr, 0);
        return ESP_OK;
    }

    std::function<void(std::string_view)> _onLog;
    std::string _ssid;
    DNSServer _dns;
    httpd_handle_t _server = nullptr;
    std::atomic<bool> _closeRequested{false};
};

}  // namespace

bool Hal::loadBadgeImage(lv_obj_t* image)
{
    if (loadSlot(image, readActiveSlot())) {
        return true;
    }
    const std::size_t fallback = findFirstAvailableSlot();
    if (fallback < badgeSlotCount && loadSlot(image, fallback)) {
        return true;
    }
    if (image != nullptr) {
        lv_image_set_src(image, &icon_badge);
    }
    return false;
}

bool Hal::loadNextBadgeImage(lv_obj_t* image)
{
    const std::size_t slot = findAvailableSlot(readActiveSlot(), 1);
    if (slot < badgeSlotCount) {
        return loadSlot(image, slot);
    }
    if (image != nullptr) {
        lv_image_set_src(image, &icon_badge);
    }
    return false;
}

bool Hal::loadPreviousBadgeImage(lv_obj_t* image)
{
    const std::size_t slot = findAvailableSlot(readActiveSlot(), -1);
    if (slot < badgeSlotCount) {
        return loadSlot(image, slot);
    }
    if (image != nullptr) {
        lv_image_set_src(image, &icon_badge);
    }
    return false;
}

void Hal::startBadgeEditModeViaAp(std::function<void(std::string_view)> onLog)
{
    if (!ensureBadgeDir()) {
        if (onLog) {
            onLog("Failed to create badge storage directory");
        }
        return;
    }
    BadgeApSession(std::move(onLog)).run();
}
