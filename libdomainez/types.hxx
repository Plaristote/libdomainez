#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace DomainEz
{
  typedef std::chrono::system_clock::time_point time_point;

  struct Status
  {
    int status = 0;
    std::string error;

    bool ok() const { return status >= 200 && status < 300; }
    bool is_missing() const { return status == 404; }
    explicit operator bool() const { return ok(); }
  };

  template<typename T>
  struct Result : Status
  {
    T value{};
  };

  struct Organization
  {
    std::string id;
    std::string name;
  };

  struct Domain
  {
    std::string name;
    std::string tld;
    std::string owner;
    std::string organization_id;
    bool autorenew = false;
    time_point created_at;
    time_point expires_at;
    time_point renew_begins_at;
    time_point updated_at;
    std::vector<std::string> status;
  };

  struct DnsRecord
  {
    std::string domain;
    std::string name;   // "@" for the apex
    std::string type;
    std::vector<std::string> values;
    unsigned int ttl = 3600;
  };

  struct Availability
  {
    std::string name;
    std::string status;   // provider's raw status, for display
    bool available = false;
    double price_before_taxes = 0.0;
    double price_after_taxes = 0.0;
    std::string currency;
    operator bool() const { return available; }
  };

  struct Contact
  {
    bool company = false;
    std::string first_name;
    std::string last_name;
    std::string organization; // company name, when `company` is true
    std::string email;
    std::string phone;        // international format, e.g. "+33.123456789"
    std::string street;
    std::string zip;
    std::string city;
    std::string state;
    std::string country;      // ISO 3166-1 alpha-2
  };

  struct Registration
  {
    std::string name;
    unsigned int duration_years = 1;
    std::string organization_id;
    Contact owner;
  };

  struct Transfer
  {
    std::string name;
    std::string auth_code;
    std::string organization_id;
    Contact owner;
  };
}
