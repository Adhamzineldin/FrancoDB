/**
 * http_handler.h
 *
 * Lightweight HTTP/1.1 handler embedded in ChronosDB server.
 * Detects HTTP requests on the same port as the database protocol,
 * serves the React web admin build, and provides REST API endpoints.
 *
 * @author ChronosDB Team
 */

#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <cstdint>

#include "execution/execution_engine.h"
#include "common/auth_manager.h"
#include "network/session_context.h"
#include "network/database_registry.h"
#include "network/protocol.h"

namespace chronosdb {
namespace web {

// ── HTTP Types ──────────────────────────────────────────

enum class HttpMethod { GET, POST, PUT, DELETE_METHOD, OPTIONS, HEAD, UNKNOWN };

struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> cookies;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    void SetJson(const std::string& json);
    void SetHtml(const std::string& html);
    void SetFile(const std::string& content, const std::string& content_type);
    void SetError(int code, const std::string& message);
    void SetCookie(const std::string& name, const std::string& value, int max_age = 86400);
    std::string Serialize() const;
};

// ── Web Session ─────────────────────────────────────────

struct WebSession {
    std::string session_id;
    std::shared_ptr<SessionContext> context;
    std::string password;  // Needed to reconnect
    std::chrono::steady_clock::time_point last_access;
};

// ── HTTP Handler ────────────────────────────────────────

class HttpHandler {
public:
    HttpHandler(IBufferManager* bpm, Catalog* catalog,
                AuthManager* auth_manager, DatabaseRegistry* registry,
                LogManager* log_manager);

    /// Check if raw bytes look like an HTTP request.
    static bool IsHttpRequest(const char* data, size_t len);

    /// Read a full HTTP request from a socket. Returns false on error.
    static bool ReadHttpRequest(uintptr_t sock, HttpRequest& req);

    /// Handle an HTTP request and write the response to the socket.
    void HandleRequest(uintptr_t sock, const HttpRequest& req);

    /// Set the directory containing the React build (index.html, assets/).
    void SetWebRoot(const std::string& path);

private:
    // ── API route handlers ──
    HttpResponse Route(const HttpRequest& req);

    HttpResponse HandleLogin(const HttpRequest& req);
    HttpResponse HandleLogout(const HttpRequest& req);
    HttpResponse HandleMe(const HttpRequest& req);
    HttpResponse HandleGetDatabases(const HttpRequest& req);
    HttpResponse HandleUseDatabase(const HttpRequest& req);
    HttpResponse HandleCreateDatabase(const HttpRequest& req);
    HttpResponse HandleDropDatabase(const HttpRequest& req, const std::string& db_name);
    HttpResponse HandleGetTables(const HttpRequest& req);
    HttpResponse HandleGetTableSchema(const HttpRequest& req, const std::string& table);
    HttpResponse HandleGetTableData(const HttpRequest& req, const std::string& table);
    HttpResponse HandleQuery(const HttpRequest& req);
    HttpResponse HandleGetUsers(const HttpRequest& req);
    HttpResponse HandleCreateUser(const HttpRequest& req);
    HttpResponse HandleDeleteUser(const HttpRequest& req, const std::string& username);
    HttpResponse HandleChangeRole(const HttpRequest& req, const std::string& username);
    HttpResponse HandleGetStatus(const HttpRequest& req);
    HttpResponse HandleGetAIStatus(const HttpRequest& req);
    HttpResponse HandleGetAnomalies(const HttpRequest& req);
    HttpResponse HandleGetExecStats(const HttpRequest& req);

    // ── Static file serving ──
    HttpResponse ServeStaticFile(const std::string& path);
    std::string GetContentType(const std::string& path);

    // ── Session management ──
    WebSession* GetSession(const HttpRequest& req);
    std::string CreateSession(const std::string& username, const std::string& password, UserRole role);
    void DestroySession(const std::string& session_id);
    std::string GenerateSessionId();

    // ── JSON helpers ──
    static std::string JsonEscape(const std::string& s);
    static std::string ParseJsonField(const std::string& json, const std::string& field);
    static std::string ResultToJson(const ExecutionResult& result);

    // ── Query execution via existing engine ──
    ExecutionResult ExecuteSQL(const std::string& sql, WebSession* session);

    // ── Components ──
    IBufferManager* bpm_;
    Catalog* catalog_;
    AuthManager* auth_manager_;
    DatabaseRegistry* registry_;
    LogManager* log_manager_;

    std::string web_root_;

    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::unique_ptr<WebSession>> sessions_;
};

} // namespace web
} // namespace chronosdb
