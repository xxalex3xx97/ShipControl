// FILE: sc_hosts.hpp
// WHAT: per-owner host callbacks and speed_k (API 5). of() returns a COPY (zero ScHost when the owner set none).
// WHY: two mods (mining, fleet) use ShipControl together; a copy cannot be overwritten by a later set() while in use.
// DEPENDS ON (exact): sc_api.h, <map>, <string>
#pragma once

#include "sc_api.h"
#include <map>
#include <string>

class ScHosts
{
public:
    ScHost of(const std::string& owner) const
    {
        auto it = m_host.find(owner);
        return it == m_host.end() ? ScHost{} : it->second;
    }
    void set(const std::string& owner, const ScHost& host) { m_host[owner] = host; }
    double speed_k(const std::string& owner, double def) const
    {
        auto it = m_k.find(owner);
        return it == m_k.end() ? def : it->second;
    }
    void set_speed_k(const std::string& owner, double k) { m_k[owner] = k; }

private:
    std::map<std::string, ScHost> m_host;
    std::map<std::string, double> m_k;
};
