// SPDX-License-Identifier: Apache-2.0
#include "cfd/config.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace cfd {

namespace {
std::string trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && issp(static_cast<unsigned char>(s.back())))  s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && issp(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
}  // namespace

bool Config::load_from_file(const std::string& path, Config& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }

    std::string line;
    unsigned lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            err = "line " + std::to_string(lineno) + ": missing '='";
            return false;
        }
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if      (k == "tunnel_id")         out.tunnel_id = v;
        else if (k == "account_tag")       out.account_tag = v;
        else if (k == "tunnel_secret_b64") out.tunnel_secret_b64 = v;
        else if (k == "edge_host")         out.edge_host = v;
        else if (k == "edge_port") {
            // from_chars never throws -- a bad config now produces an error,
            // not std::terminate (see docs/ISSUES.md ISSUE-005).
            unsigned port = 0;
            auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), port);
            if (ec != std::errc{} || p != v.data() + v.size() ||
                port == 0 || port > 65535) {
                err = "bad edge_port: " + v;
                return false;
            }
            out.edge_port = static_cast<std::uint16_t>(port);
        }
        else if (k == "tun_name")          out.tun_name = v;
        else if (k == "tun_local") {
            auto c = net::Cidr::parse(v);
            if (!c) { err = "bad tun_local: " + v; return false; }
            out.tun_local = *c;
        } else if (k == "route") {
            auto c = net::Cidr::parse(v);
            if (!c) { err = "bad route: " + v; return false; }
            out.routed_cidrs.push_back(*c);
        } else if (k == "ca_bundle_path")     out.ca_bundle_path = v;
          else if (k == "client_cert_path")   out.client_cert_path = v;
          else if (k == "client_key_path")    out.client_key_path = v;
        else {
            err = "unknown key: " + k;
            return false;
        }
    }
    return true;
}

}  // namespace cfd
