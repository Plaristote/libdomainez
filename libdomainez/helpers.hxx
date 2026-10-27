#pragma once
#include "types.hxx"
#include <crails/datatree.hpp>
#include <ctime>
#include <exception>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace DomainEz
{
  // Accepts "2026-01-20T00:00:00Z" (Gandi) and "2026-01-20" (OVH).
  inline time_point parse_time(const std::string& str)
  {
    if (!str.empty())
    {
      for (const char* format : {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%d"})
      {
        std::tm tm = {};
        std::istringstream stream(str);

        stream >> std::get_time(&tm, format);
        if (!stream.fail())
          return std::chrono::system_clock::from_time_t(timegm(&tm));
      }
    }
    return {};
  }

  inline Status failure(int status, const std::string& error)
  {
    Status result;

    result.status = status;
    result.error = error;
    return result;
  }

  inline Status not_implemented(const std::string& provider)
  {
    return failure(501, provider + ": not implemented");
  }

  inline std::string tld_of(const std::string& domain)
  {
    size_t dot = domain.find('.');

    return dot == std::string::npos ? std::string() : domain.substr(dot + 1);
  }

  // For providers storing one entry per value: groups entries (anything with
  // name, type, target and ttl) into record sets. The ttl of the first entry wins.
  template<typename ENTRY>
  std::vector<DnsRecord> group_entries(std::string_view domain, const std::vector<ENTRY>& entries)
  {
    std::vector<DnsRecord> records;
    std::map<std::pair<std::string, std::string>, size_t> index; // (name, type) -> position in records

    for (const ENTRY& entry : entries)
    {
      auto key = std::make_pair(entry.name, entry.type);
      auto it = index.find(key);

      if (it == index.end())
      {
        DnsRecord record;

        record.domain = std::string(domain);
        record.name = entry.name;
        record.type = entry.type;
        record.ttl = entry.ttl;
        it = index.emplace(key, records.size()).first;
        records.push_back(record);
      }
      records[it->second].values.push_back(entry.target);
    }
    return records;
  }

  template<typename T, typename LOADER>
  Result<T> parse_one(const Status& status, const std::string& body, LOADER load)
  {
    Result<T> result;

    static_cast<Status&>(result) = status;
    if (status.ok())
    {
      try
      {
        DataTree tree;

        tree.from_json(body);
        result.value = load(tree.as_data());
      }
      catch (const std::exception& error)
      {
        result.status = 0;
        result.error = std::string("unexpected response: ") + error.what();
      }
    }
    return result;
  }
}
