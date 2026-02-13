/**
 * http_handler.cpp
 *
 * HTTP/1.1 handler implementation for the ChronosDB web admin interface.
 * Serves the React frontend build and provides REST API endpoints.
 *
 * @author ChronosDB Team
 */

#include "web/http_handler.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "ai/ai_manager.h"
#include "ai/ai_scheduler.h"
#include "ai/metrics_store.h"
#include "ai/ai_config.h"
#include "ai/learning/learning_engine.h"
#include "ai/immune/immune_system.h"
#include "ai/temporal/temporal_index_manager.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#endif

#include <sstream>
#include <fstream>
#include <filesystem>
#include <random>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>

namespace chronosdb {
namespace web {

// Helper: output a double value safe for JSON (inf/NaN -> 0)
static double safe_double(double v) {
    if (std::isinf(v) || std::isnan(v)) return 0.0;
    return v;
}

// ── Standalone JSON escape helper ──
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════
// HTTP Response Methods
// ═══════════════════════════════════════════════════════

void HttpResponse::SetJson(const std::string& json) {
    body = json;
    headers["Content-Type"] = "application/json; charset=utf-8";
}

void HttpResponse::SetHtml(const std::string& html) {
    body = html;
    headers["Content-Type"] = "text/html; charset=utf-8";
}

void HttpResponse::SetFile(const std::string& content, const std::string& content_type) {
    body = content;
    headers["Content-Type"] = content_type;
}

void HttpResponse::SetError(int code, const std::string& message) {
    status_code = code;
    status_text = (code == 404) ? "Not Found" :
                  (code == 401) ? "Unauthorized" :
                  (code == 400) ? "Bad Request" :
                  (code == 403) ? "Forbidden" :
                  (code == 500) ? "Internal Server Error" : "Error";
    SetJson("{\"success\":false,\"error\":\"" + json_escape(message) + "\"}");
}

void HttpResponse::SetCookie(const std::string& name, const std::string& value, int max_age) {
    std::string cookie = name + "=" + value + "; Path=/; HttpOnly; SameSite=Lax";
    if (max_age > 0) {
        cookie += "; Max-Age=" + std::to_string(max_age);
    } else if (max_age == 0) {
        cookie += "; Max-Age=0";
    }
    headers["Set-Cookie"] = cookie;
}

std::string HttpResponse::Serialize() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    for (const auto& [key, val] : headers) {
        oss << key << ": " << val << "\r\n";
    }
    oss << "\r\n";
    oss << body;
    return oss.str();
}

// ═══════════════════════════════════════════════════════
// HttpHandler Constructor
// ═══════════════════════════════════════════════════════

HttpHandler::HttpHandler(IBufferManager* bpm, Catalog* catalog,
                         AuthManager* auth_manager, DatabaseRegistry* registry,
                         LogManager* log_manager)
    : bpm_(bpm), catalog_(catalog), auth_manager_(auth_manager),
      registry_(registry), log_manager_(log_manager) {}

void HttpHandler::SetWebRoot(const std::string& path) {
    web_root_ = path;
}

// ═══════════════════════════════════════════════════════
// HTTP Detection & Parsing
// ═══════════════════════════════════════════════════════

bool HttpHandler::IsHttpRequest(const char* data, size_t len) {
    if (len < 3) return false;
    // Check for HTTP method prefixes
    return (len >= 3 && (
        (data[0] == 'G' && data[1] == 'E' && data[2] == 'T') ||  // GET
        (data[0] == 'P' && data[1] == 'O' && data[2] == 'S') ||  // POST
        (data[0] == 'P' && data[1] == 'U' && data[2] == 'T') ||  // PUT
        (data[0] == 'D' && data[1] == 'E' && data[2] == 'L') ||  // DELETE
        (data[0] == 'O' && data[1] == 'P' && data[2] == 'T') ||  // OPTIONS
        (data[0] == 'H' && data[1] == 'E' && data[2] == 'A')     // HEAD
    ));
}

bool HttpHandler::ReadHttpRequest(uintptr_t sock, HttpRequest& req) {
    socket_t s = (socket_t)sock;
    std::string raw;
    raw.reserve(8192);

    // Read headers (until \r\n\r\n)
    char buf[4096];
    size_t header_end = std::string::npos;

    while (header_end == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        raw.append(buf, n);
        header_end = raw.find("\r\n\r\n");
        if (raw.size() > 65536) return false; // Header too large
    }

    // Parse request line
    size_t first_line_end = raw.find("\r\n");
    std::string request_line = raw.substr(0, first_line_end);

    // Parse method
    size_t sp1 = request_line.find(' ');
    if (sp1 == std::string::npos) return false;
    std::string method_str = request_line.substr(0, sp1);

    if (method_str == "GET") req.method = HttpMethod::GET;
    else if (method_str == "POST") req.method = HttpMethod::POST;
    else if (method_str == "PUT") req.method = HttpMethod::PUT;
    else if (method_str == "DELETE") req.method = HttpMethod::DELETE_METHOD;
    else if (method_str == "OPTIONS") req.method = HttpMethod::OPTIONS;
    else if (method_str == "HEAD") req.method = HttpMethod::HEAD;
    else req.method = HttpMethod::UNKNOWN;

    // Parse path
    size_t sp2 = request_line.find(' ', sp1 + 1);
    std::string full_path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qs = full_path.find('?');
    if (qs != std::string::npos) {
        req.path = full_path.substr(0, qs);
        req.query_string = full_path.substr(qs + 1);
    } else {
        req.path = full_path;
    }

    // Parse headers
    size_t pos = first_line_end + 2;
    while (pos < header_end) {
        size_t line_end = raw.find("\r\n", pos);
        if (line_end == std::string::npos || line_end >= header_end) break;
        std::string line = raw.substr(pos, line_end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            // Trim whitespace
            while (!val.empty() && val[0] == ' ') val.erase(0, 1);
            // Lowercase key for case-insensitive lookup
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
            req.headers[lower_key] = val;
        }
        pos = line_end + 2;
    }

    // Parse cookies
    auto cookie_it = req.headers.find("cookie");
    if (cookie_it != req.headers.end()) {
        std::istringstream css(cookie_it->second);
        std::string pair;
        while (std::getline(css, pair, ';')) {
            while (!pair.empty() && pair[0] == ' ') pair.erase(0, 1);
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                req.cookies[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
        }
    }

    // Read body if Content-Length present
    auto cl_it = req.headers.find("content-length");
    if (cl_it != req.headers.end()) {
        size_t content_length = std::stoul(cl_it->second);
        size_t body_start = header_end + 4;
        size_t body_available = raw.size() - body_start;

        if (body_available < content_length) {
            size_t remaining = content_length - body_available;
            req.body = raw.substr(body_start);
            while (req.body.size() < content_length) {
                int n = recv(s, buf, std::min(sizeof(buf), remaining), 0);
                if (n <= 0) break;
                req.body.append(buf, n);
                remaining -= n;
            }
        } else {
            req.body = raw.substr(body_start, content_length);
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════
// Request Handling
// ═══════════════════════════════════════════════════════

void HttpHandler::HandleRequest(uintptr_t sock, const HttpRequest& req) {
    HttpResponse resp = Route(req);

    // CORS: echo the request Origin (not "*") so credentials/cookies work.
    // Using "*" with credentials: 'include' causes browsers to reject Set-Cookie.
    auto origin_it = req.headers.find("origin");
    if (origin_it != req.headers.end() && !origin_it->second.empty()) {
        resp.headers["Access-Control-Allow-Origin"] = origin_it->second;
        resp.headers["Access-Control-Allow-Credentials"] = "true";
    }
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";

    std::string serialized = resp.Serialize();

    socket_t s = (socket_t)sock;
    size_t total_sent = 0;
    while (total_sent < serialized.size()) {
        int sent = send(s, serialized.c_str() + total_sent,
                       (int)(serialized.size() - total_sent), 0);
        if (sent <= 0) break;
        total_sent += sent;
    }
}

HttpResponse HttpHandler::Route(const HttpRequest& req) {
    // CORS preflight
    if (req.method == HttpMethod::OPTIONS) {
        HttpResponse resp;
        resp.status_code = 204;
        resp.status_text = "No Content";
        return resp;
    }

    const std::string& p = req.path;

    // ── API Routes ──
    if (p == "/api/login" && req.method == HttpMethod::POST) return HandleLogin(req);
    if (p == "/api/logout" && req.method == HttpMethod::POST) return HandleLogout(req);
    if (p == "/api/me" && req.method == HttpMethod::GET) return HandleMe(req);
    if (p == "/api/databases" && req.method == HttpMethod::GET) return HandleGetDatabases(req);
    if (p == "/api/databases/use" && req.method == HttpMethod::POST) return HandleUseDatabase(req);
    if (p == "/api/databases/create" && req.method == HttpMethod::POST) return HandleCreateDatabase(req);
    if (p == "/api/tables" && req.method == HttpMethod::GET) return HandleGetTables(req);
    if (p == "/api/query" && req.method == HttpMethod::POST) return HandleQuery(req);
    if (p == "/api/users" && req.method == HttpMethod::GET) return HandleGetUsers(req);
    if (p == "/api/users" && req.method == HttpMethod::POST) return HandleCreateUser(req);
    if (p == "/api/status" && req.method == HttpMethod::GET) return HandleGetStatus(req);
    if (p == "/api/ai/status" && req.method == HttpMethod::GET) return HandleGetAIStatus(req);
    if (p == "/api/ai/anomalies" && req.method == HttpMethod::GET) return HandleGetAnomalies(req);
    if (p == "/api/ai/stats" && req.method == HttpMethod::GET) return HandleGetExecStats(req);
    if (p == "/api/ai/detailed" && req.method == HttpMethod::GET) return HandleGetAIDetailed(req);

    // Pattern routes: /api/tables/:name/schema, /api/tables/:name/data
    if (p.rfind("/api/tables/", 0) == 0 && p.size() > 12) {
        std::string rest = p.substr(12);
        size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string table_name = rest.substr(0, slash);
            std::string action = rest.substr(slash + 1);
            if (action == "schema" && req.method == HttpMethod::GET)
                return HandleGetTableSchema(req, table_name);
            if (action == "data" && req.method == HttpMethod::GET)
                return HandleGetTableData(req, table_name);
        }
    }

    // /api/databases/:name (DELETE)
    if (p.rfind("/api/databases/", 0) == 0 && p.size() > 15 &&
        p != "/api/databases/use" && p != "/api/databases/create" &&
        req.method == HttpMethod::DELETE_METHOD) {
        return HandleDropDatabase(req, p.substr(15));
    }

    // /api/users/:name (DELETE), /api/users/:name/role (PUT)
    if (p.rfind("/api/users/", 0) == 0 && p.size() > 11) {
        std::string rest = p.substr(11);
        size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string username = rest.substr(0, slash);
            std::string action = rest.substr(slash + 1);
            if (action == "role" && req.method == HttpMethod::PUT)
                return HandleChangeRole(req, username);
        } else if (req.method == HttpMethod::DELETE_METHOD) {
            return HandleDeleteUser(req, rest);
        }
    }

    // ── Static File Serving ──
    if (req.method == HttpMethod::GET) {
        if (p == "/" || p == "/index.html") {
            return ServeStaticFile("index.html");
        }
        // Try serving the exact file
        std::string file_path = p.substr(1); // Remove leading /
        HttpResponse file_resp = ServeStaticFile(file_path);
        if (file_resp.status_code == 200) return file_resp;

        // SPA fallback: serve index.html for non-API, non-file routes
        if (p.find("/api/") != 0) {
            return ServeStaticFile("index.html");
        }
    }

    HttpResponse resp;
    resp.SetError(404, "Not found");
    return resp;
}

// ═══════════════════════════════════════════════════════
// API Handlers
// ═══════════════════════════════════════════════════════

HttpResponse HttpHandler::HandleLogin(const HttpRequest& req) {
    HttpResponse resp;
    std::string username = ParseJsonField(req.body, "username");
    std::string password = ParseJsonField(req.body, "password");

    if (username.empty() || password.empty()) {
        resp.SetError(400, "Username and password required");
        return resp;
    }

    UserRole role;
    if (!auth_manager_->Authenticate(username, password, role)) {
        resp.SetError(401, "Authentication failed");
        return resp;
    }

    std::string session_id = CreateSession(username, password, role);
    std::string role_str;
    switch (role) {
        case UserRole::SUPERADMIN: role_str = "SUPERADMIN"; break;
        case UserRole::ADMIN: role_str = "ADMIN"; break;
        case UserRole::NORMAL: role_str = "NORMAL"; break;
        case UserRole::READONLY: role_str = "READONLY"; break;
        default: role_str = "DENIED"; break;
    }

    resp.SetJson("{\"success\":true,\"username\":\"" + JsonEscape(username) +
                 "\",\"role\":\"" + role_str +
                 "\",\"token\":\"" + session_id + "\"}");
    resp.SetCookie("chronos_session", session_id, 86400);
    return resp;
}

HttpResponse HttpHandler::HandleLogout(const HttpRequest& req) {
    HttpResponse resp;
    auto it = req.cookies.find("chronos_session");
    if (it != req.cookies.end()) {
        DestroySession(it->second);
    }
    resp.SetJson("{\"success\":true}");
    resp.SetCookie("chronos_session", "", 0);
    return resp;
}

HttpResponse HttpHandler::HandleMe(const HttpRequest& req) {
    HttpResponse resp;
    auto* session = GetSession(req);
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    std::string role_str;
    switch (session->context->role) {
        case UserRole::SUPERADMIN: role_str = "SUPERADMIN"; break;
        case UserRole::ADMIN: role_str = "ADMIN"; break;
        case UserRole::NORMAL: role_str = "NORMAL"; break;
        case UserRole::READONLY: role_str = "READONLY"; break;
        default: role_str = "DENIED"; break;
    }

    resp.SetJson("{\"success\":true,\"username\":\"" + JsonEscape(session->context->current_user) +
                 "\",\"role\":\"" + role_str +
                 "\",\"currentDb\":\"" + JsonEscape(session->context->current_db) + "\"}");
    return resp;
}

HttpResponse HttpHandler::HandleGetDatabases(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW DATABASES;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleUseDatabase(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    std::string database = ParseJsonField(req.body, "database");
    if (database.empty()) { resp.SetError(400, "Database name required"); return resp; }

    auto result = ExecuteSQL("USE " + database + ";", session);
    if (result.success) {
        session->context->current_db = database;
        session->context->role = auth_manager_->GetUserRole(session->context->current_user, database);
        if (auth_manager_->IsSuperAdmin(session->context->current_user)) {
            session->context->role = UserRole::SUPERADMIN;
        }
    }
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleCreateDatabase(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    std::string name = ParseJsonField(req.body, "name");
    if (name.empty()) { resp.SetError(400, "Database name required"); return resp; }
    auto result = ExecuteSQL("CREATE DATABASE " + name + ";", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleDropDatabase(const HttpRequest& req, const std::string& db_name) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("DROP DATABASE " + db_name + ";", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetTables(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW TABLE;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetTableSchema(const HttpRequest& req, const std::string& table) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("DESCRIBE " + table + ";", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetTableData(const HttpRequest& req, const std::string& table) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    // Fetch all rows (frontend handles pagination)
    auto result = ExecuteSQL("SELECT * FROM " + table + ";", session);

    // Safety cap to prevent massive JSON payloads
    static constexpr size_t MAX_TABLE_ROWS = 10000;
    bool truncated = false;
    size_t total_rows = 0;
    if (result.success && result.result_set) {
        total_rows = result.result_set->rows.size();
        if (total_rows > MAX_TABLE_ROWS) {
            result.result_set->rows.resize(MAX_TABLE_ROWS);
            truncated = true;
        }
    }

    std::string json = ResultToJson(result);
    if (json.size() > 2) {
        size_t close = json.rfind('}');
        if (close != std::string::npos) {
            std::string extra = ",\n  \"total_count\": " + std::to_string(total_rows);
            if (truncated) {
                extra += ",\n  \"truncated\": true";
            }
            json.insert(close, extra);
        }
    }
    resp.SetJson(json);
    return resp;
}

HttpResponse HttpHandler::HandleQuery(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    std::string sql = ParseJsonField(req.body, "sql");
    if (sql.empty()) { resp.SetError(400, "SQL query required"); return resp; }

    auto result = ExecuteSQL(sql, session);

    // Cap results at 1000 rows to prevent browser freeze
    static constexpr size_t MAX_WEB_ROWS = 1000;
    bool truncated = false;
    size_t total_rows = 0;
    if (result.success && result.result_set) {
        total_rows = result.result_set->rows.size();
        if (total_rows > MAX_WEB_ROWS) {
            result.result_set->rows.resize(MAX_WEB_ROWS);
            truncated = true;
        }
    }

    std::string json = ResultToJson(result);
    if (truncated && json.size() > 2) {
        size_t close = json.rfind('}');
        if (close != std::string::npos) {
            json.insert(close,
                ",\n  \"truncated\": true,\n  \"total_rows\": " +
                std::to_string(total_rows) +
                ",\n  \"max_rows\": " + std::to_string(MAX_WEB_ROWS));
        }
    }
    resp.SetJson(json);
    return resp;
}

HttpResponse HttpHandler::HandleGetUsers(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW USER;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleCreateUser(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    std::string username = ParseJsonField(req.body, "username");
    std::string password = ParseJsonField(req.body, "password");
    std::string role = ParseJsonField(req.body, "role");
    if (username.empty() || password.empty()) {
        resp.SetError(400, "Username and password required");
        return resp;
    }
    if (role.empty()) role = "READONLY";

    auto result = ExecuteSQL("CREATE USER '" + username + "' '" + password + "' " + role + ";", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleDeleteUser(const HttpRequest& req, const std::string& username) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("DELETE USER '" + username + "';", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleChangeRole(const HttpRequest& req, const std::string& username) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    std::string role = ParseJsonField(req.body, "role");
    std::string database = ParseJsonField(req.body, "database");
    if (role.empty()) { resp.SetError(400, "Role required"); return resp; }

    std::string sql = "ALTER USER '" + username + "' ROLE " + role;
    if (!database.empty()) sql += " IN " + database;
    sql += ";";
    auto result = ExecuteSQL(sql, session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetStatus(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW STATUS;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetAIStatus(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW AI STATUS;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetAnomalies(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW ANOMALIES;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetExecStats(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }
    auto result = ExecuteSQL("SHOW EXECUTION STATS;", session);
    resp.SetJson(ResultToJson(result));
    return resp;
}

HttpResponse HttpHandler::HandleGetAIDetailed(const HttpRequest& req) {
    auto* session = GetSession(req);
    HttpResponse resp;
    if (!session) { resp.SetError(401, "Not authenticated"); return resp; }

    auto& ai_mgr = ai::AIManager::Instance();
    std::ostringstream json;
    json << std::fixed;
    json << "{\n  \"initialized\": " << (ai_mgr.IsInitialized() ? "true" : "false");

    if (!ai_mgr.IsInitialized()) {
        json << "\n}";
        resp.SetJson(json.str());
        return resp;
    }

    // ── Metrics Store ──
    auto& metrics = ai::MetricsStore::Instance();
    json << ",\n  \"metrics_recorded\": " << metrics.GetTotalRecorded();

    // ── Scheduled Tasks ──
    auto tasks = ai::AIScheduler::Instance().GetScheduledTasks();
    json << ",\n  \"scheduled_tasks\": [";
    for (size_t i = 0; i < tasks.size(); i++) {
        if (i > 0) json << ",";
        json << "\n    {\"name\": \"" << JsonEscape(tasks[i].name)
             << "\", \"interval_ms\": " << tasks[i].interval_ms
             << ", \"run_count\": " << tasks[i].run_count
             << ", \"periodic\": " << (tasks[i].periodic ? "true" : "false") << "}";
    }
    json << "\n  ]";

    // ── Learning Engine ──
    auto* learning = ai_mgr.GetLearningEngine();
    json << ",\n  \"learning_engine\": {";
    json << "\n    \"active\": " << (learning ? "true" : "false");
    if (learning) {
        uint64_t total_q = learning->GetTotalQueriesObserved();
        json << ",\n    \"total_queries\": " << total_q;
        json << ",\n    \"min_samples\": " << ai::MIN_SAMPLES_BEFORE_LEARNING;
        json << ",\n    \"ready\": " << (total_q >= ai::MIN_SAMPLES_BEFORE_LEARNING ? "true" : "false");

        auto arms = learning->GetArmStats();
        json << ",\n    \"arms\": [";
        for (size_t i = 0; i < arms.size(); i++) {
            if (i > 0) json << ",";
            json << "\n      {\"strategy\": \""
                 << (arms[i].strategy == ai::ScanStrategy::SEQUENTIAL_SCAN ? "Sequential Scan" : "Index Scan")
                 << "\", \"pulls\": " << arms[i].total_pulls
                 << ", \"avg_reward\": " << std::setprecision(4) << safe_double(arms[i].average_reward)
                 << ", \"ucb_score\": " << std::setprecision(4) << safe_double(arms[i].ucb_score) << "}";
        }
        json << "\n    ]";
        json << ",\n    \"summary\": \"" << JsonEscape(learning->GetSummary()) << "\"";
    }
    json << "\n  }";

    // ── Immune System ──
    auto* immune = ai_mgr.GetImmuneSystem();
    json << ",\n  \"immune_system\": {";
    json << "\n    \"active\": " << (immune ? "true" : "false");
    if (immune) {
        json << ",\n    \"total_anomalies\": " << immune->GetTotalAnomalies();
        json << ",\n    \"check_interval_ms\": " << ai::IMMUNE_CHECK_INTERVAL_MS;

        auto blocked_tables = immune->GetBlockedTables();
        json << ",\n    \"blocked_tables\": [";
        for (size_t i = 0; i < blocked_tables.size(); i++) {
            if (i > 0) json << ", ";
            json << "\"" << JsonEscape(blocked_tables[i]) << "\"";
        }
        json << "]";

        auto blocked_users = immune->GetBlockedUsers();
        json << ",\n    \"blocked_users\": [";
        for (size_t i = 0; i < blocked_users.size(); i++) {
            if (i > 0) json << ", ";
            json << "\"" << JsonEscape(blocked_users[i]) << "\"";
        }
        json << "]";

        auto monitored = immune->GetMonitoredTables();
        json << ",\n    \"monitored_tables\": " << monitored.size();

        json << ",\n    \"thresholds\": {\"low\": " << std::setprecision(1) << ai::ZSCORE_LOW_THRESHOLD
             << ", \"medium\": " << ai::ZSCORE_MEDIUM_THRESHOLD
             << ", \"high\": " << ai::ZSCORE_HIGH_THRESHOLD << "}";

        auto anomalies = immune->GetRecentAnomalies(20);
        json << ",\n    \"recent_anomalies\": [";
        for (size_t i = 0; i < anomalies.size(); i++) {
            if (i > 0) json << ",";
            std::string sev;
            switch (anomalies[i].severity) {
                case ai::AnomalySeverity::LOW:    sev = "LOW"; break;
                case ai::AnomalySeverity::MEDIUM: sev = "MEDIUM"; break;
                case ai::AnomalySeverity::HIGH:   sev = "HIGH"; break;
                default:                          sev = "NONE"; break;
            }
            json << "\n      {\"table\": \"" << JsonEscape(anomalies[i].table_name)
                 << "\", \"user\": \"" << JsonEscape(anomalies[i].user)
                 << "\", \"severity\": \"" << sev
                 << "\", \"z_score\": " << std::setprecision(2) << safe_double(anomalies[i].z_score)
                 << ", \"current_rate\": " << std::setprecision(2) << safe_double(anomalies[i].current_rate)
                 << ", \"mean_rate\": " << std::setprecision(2) << safe_double(anomalies[i].mean_rate)
                 << ", \"timestamp_us\": " << anomalies[i].timestamp_us
                 << ", \"description\": \"" << JsonEscape(anomalies[i].description) << "\"}";
        }
        json << "\n    ]";
        json << ",\n    \"summary\": \"" << JsonEscape(immune->GetSummary()) << "\"";
    }
    json << "\n  }";

    // ── Temporal Index Manager ──
    auto* temporal = ai_mgr.GetTemporalIndexManager();
    json << ",\n  \"temporal_index\": {";
    json << "\n    \"active\": " << (temporal ? "true" : "false");
    if (temporal) {
        json << ",\n    \"total_accesses\": " << temporal->GetTotalAccessCount();
        json << ",\n    \"total_snapshots\": " << temporal->GetTotalSnapshotsTriggered();
        json << ",\n    \"analysis_interval_ms\": " << ai::TEMPORAL_ANALYSIS_INTERVAL_MS;

        auto hotspots = temporal->GetCurrentHotspots();
        json << ",\n    \"hotspots\": [";
        for (size_t i = 0; i < hotspots.size(); i++) {
            if (i > 0) json << ",";
            json << "\n      {\"center_us\": " << hotspots[i].center_timestamp_us
                 << ", \"range_start_us\": " << hotspots[i].range_start_us
                 << ", \"range_end_us\": " << hotspots[i].range_end_us
                 << ", \"access_count\": " << hotspots[i].access_count
                 << ", \"density\": " << std::setprecision(2) << safe_double(hotspots[i].density) << "}";
        }
        json << "\n    ]";
        json << ",\n    \"summary\": \"" << JsonEscape(temporal->GetSummary()) << "\"";
    }
    json << "\n  }";

    json << "\n}";
    resp.SetJson(json.str());
    return resp;
}

// ═══════════════════════════════════════════════════════
// Static File Serving
// ═══════════════════════════════════════════════════════

HttpResponse HttpHandler::ServeStaticFile(const std::string& relative_path) {
    HttpResponse resp;

    if (web_root_.empty()) {
        // Serve embedded fallback page if no web root is set
        if (relative_path == "index.html") {
            resp.SetHtml(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ChronosDB Admin</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#0f1117;color:#e4e6ef;display:flex;align-items:center;justify-content:center;min-height:100vh}
.c{text-align:center;max-width:500px;padding:2rem}
h1{font-size:2rem;margin-bottom:.5rem;color:#6366f1}
p{color:#9ca0b0;margin:.5rem 0}
code{background:#1e2130;padding:.2em .5em;border-radius:4px;font-size:.85em;color:#6366f1}
.steps{text-align:left;margin-top:2rem;background:#161822;padding:1.5rem;border-radius:8px;border:1px solid #2a2d3e}
.steps li{margin:.5rem 0;color:#9ca0b0}
</style>
</head>
<body>
<div class="c">
<h1>ChronosDB Web Admin</h1>
<p>The web interface needs to be built first.</p>
<div class="steps">
<p style="color:#e4e6ef;font-weight:600;margin-bottom:.75rem">Setup Instructions:</p>
<ol>
<li>Open terminal in <code>web-admin/</code></li>
<li>Run <code>cd client && npm install</code></li>
<li>Run <code>npm run build</code></li>
<li>Restart ChronosDB server</li>
</ol>
</div>
<p style="margin-top:1.5rem;font-size:.8rem;color:#6b6f82">
For development mode: <code>npm run dev</code> from <code>web-admin/</code>
</p>
</div>
</body>
</html>)HTML");
            return resp;
        }
        resp.SetError(404, "Not found");
        return resp;
    }

    namespace fs = std::filesystem;
    fs::path file_path = fs::path(web_root_) / relative_path;

    // Security: prevent path traversal
    std::string canonical;
    try {
        if (!fs::exists(file_path)) {
            resp.SetError(404, "Not found");
            return resp;
        }
        canonical = fs::canonical(file_path).string();
        std::string root_canonical = fs::canonical(web_root_).string();
        if (canonical.find(root_canonical) != 0) {
            resp.SetError(403, "Forbidden");
            return resp;
        }
    } catch (...) {
        resp.SetError(404, "Not found");
        return resp;
    }

    // Read file
    std::ifstream file(canonical, std::ios::binary);
    if (!file.is_open()) {
        resp.SetError(404, "Not found");
        return resp;
    }

    std::ostringstream content;
    content << file.rdbuf();
    std::string content_type = GetContentType(canonical);
    resp.SetFile(content.str(), content_type);

    // Cache static assets (hashed filenames = safe to cache forever)
    if (relative_path.find("assets/") == 0) {
        resp.headers["Cache-Control"] = "public, max-age=31536000, immutable";
    } else {
        // HTML: never cache (ensures fresh JS/CSS references after rebuild)
        resp.headers["Cache-Control"] = "no-cache, no-store, must-revalidate";
    }

    return resp;
}

std::string HttpHandler::GetContentType(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    return "application/octet-stream";
}

// ═══════════════════════════════════════════════════════
// Session Management
// ═══════════════════════════════════════════════════════

WebSession* HttpHandler::GetSession(const HttpRequest& req) {
    std::string session_id;

    // 1. Try cookie first
    auto cookie_it = req.cookies.find("chronos_session");
    if (cookie_it != req.cookies.end()) {
        session_id = cookie_it->second;
    }

    // 2. Fallback: Authorization: Bearer <token> header
    if (session_id.empty()) {
        auto auth_it = req.headers.find("authorization");
        if (auth_it != req.headers.end()) {
            const std::string& val = auth_it->second;
            if (val.size() > 7 && val.compare(0, 7, "Bearer ") == 0) {
                session_id = val.substr(7);
            }
        }
    }

    if (session_id.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(session_id);
    if (sit == sessions_.end()) {
        return nullptr;
    }

    sit->second->last_access = std::chrono::steady_clock::now();
    return sit->second.get();
}

std::string HttpHandler::CreateSession(const std::string& username, const std::string& password, UserRole role) {
    std::string session_id = GenerateSessionId();

    auto session = std::make_unique<WebSession>();
    session->session_id = session_id;
    session->context = std::make_shared<SessionContext>();
    session->context->current_user = username;
    session->context->is_authenticated = true;
    session->context->role = role;
    session->password = password;
    session->last_access = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[session_id] = std::move(session);
    return session_id;
}

void HttpHandler::DestroySession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(session_id);
}

std::string HttpHandler::GenerateSessionId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char hex[] = "0123456789abcdef";

    std::string id;
    id.reserve(32);
    for (int i = 0; i < 32; ++i) {
        id += hex[dis(gen)];
    }
    return id;
}

// ═══════════════════════════════════════════════════════
// JSON Helpers
// ═══════════════════════════════════════════════════════

std::string HttpHandler::JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string HttpHandler::ParseJsonField(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        pos++;
        size_t end = pos;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') end++; // Skip escaped char
            end++;
        }
        return json.substr(pos, end - pos);
    }

    // Non-string value (number, bool, null)
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') end++;
    std::string val = json.substr(pos, end - pos);
    // Trim whitespace
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\n')) val.pop_back();
    return val;
}

std::string HttpHandler::ResultToJson(const ExecutionResult& result) {
    // Use existing JsonProtocol for consistent output
    JsonProtocol proto;
    return proto.Serialize(result);
}

// ═══════════════════════════════════════════════════════
// SQL Execution
// ═══════════════════════════════════════════════════════

ExecutionResult HttpHandler::ExecuteSQL(const std::string& sql, WebSession* session) {
    try {
        // Resolve the correct BPM and Catalog for the session's current database.
        // Without this, DDL/DML executors use the default catalog (chronosdb)
        // instead of the user's selected database.
        IBufferManager* bpm = bpm_;
        Catalog* catalog = catalog_;
        if (registry_ && !session->context->current_db.empty()) {
            if (auto entry = registry_->Get(session->context->current_db)) {
                if (entry->bpm) bpm = entry->bpm.get();
                if (entry->catalog) catalog = entry->catalog.get();
            }
            // Fallback to external registrations
            if (bpm == bpm_) {
                IBufferManager* ext_bpm = registry_->ExternalBpm(session->context->current_db);
                if (ext_bpm) bpm = ext_bpm;
            }
            if (catalog == catalog_) {
                Catalog* ext_cat = registry_->ExternalCatalog(session->context->current_db);
                if (ext_cat) catalog = ext_cat;
            }
        }

        auto engine = std::make_unique<ExecutionEngine>(
            bpm, catalog, auth_manager_, registry_, log_manager_,
            false  // manage_ai=false: don't touch AI singleton (server's main engine owns it)
        );

        Lexer lexer(sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();

        if (!stmt) {
            return ExecutionResult::Error("Failed to parse query");
        }

        // Refresh role
        if (!session->context->current_db.empty()) {
            session->context->role = auth_manager_->GetUserRole(
                session->context->current_user, session->context->current_db);
            if (auth_manager_->IsSuperAdmin(session->context->current_user)) {
                session->context->role = UserRole::SUPERADMIN;
            }
        }

        return engine->Execute(stmt.get(), session->context.get());
    } catch (const std::exception& e) {
        return ExecutionResult::Error(std::string("SYSTEM ERROR: ") + e.what());
    }
}

} // namespace web
} // namespace chronosdb
