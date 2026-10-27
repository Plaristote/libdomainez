#pragma once
#include "provider.hxx"
#include <optional>

namespace DomainEz
{
  // OVHcloud API (REST, signed requests).
  //
  // Credentials: an application key/secret and a consumer key, see
  // https://eu.api.ovh.com/createToken/ . The consumer key's access rules
  // must allow the routes used (GET /domain, GET/POST/PUT/DELETE
  // /domain/zone/*, ...).
  //
  // OVH has no organizations: organizations() is always empty, and
  // Domain::organization_id is left blank.
  //
  // OVH stores one DNS entry per value, while DomainEz::DnsRecord is a record
  // set. Reading groups the entries by name and type, writing creates, updates
  // and deletes entries as needed, then refreshes the zone (OVH only publishes
  // changes on refresh).
  //
  // Not implemented yet (they return status 501): check_availability(),
  // register_domain(), renew() and transfer(). OVH handles those through its
  // shopping cart API.
  class LIBDOMAINEZ_SYMEXPORT OvhProvider : public Provider
  {
  public:
    static constexpr std::string_view europe        = "https://eu.api.ovh.com/1.0";
    static constexpr std::string_view canada        = "https://ca.api.ovh.com/1.0";
    static constexpr std::string_view united_states = "https://api.us.ovhcloud.com/1.0";

    OvhProvider(std::string_view application_key, std::string_view application_secret, std::string_view consumer_key, std::string_view base_url = europe);

    std::string name() const override { return "ovh"; }

    Result<std::vector<Organization>> organizations() override;
    Result<std::vector<Domain>> domains() override;
    Result<Domain> domain(std::string_view name) override;
    Result<Availability> check_availability(std::string_view name) override;
    Status register_domain(const Registration&) override;
    Status renew(std::string_view name, unsigned int years = 1) override;
    Status transfer(const Transfer&) override;
    Result<std::vector<std::string>> nameservers(std::string_view domain) override;

    Result<std::vector<DnsRecord>> dns_records(std::string_view domain) override;
    Result<DnsRecord> dns_record(std::string_view domain, std::string_view name, std::string_view type) override;
    Status add_dns_record(const DnsRecord&) override;
    Status update_dns_record(const DnsRecord&) override;
    Status delete_dns_record(const DnsRecord&) override;

  private:
    struct Response : Status
    {
      std::string body;
    };

    struct Entry
    {
      std::string id;
      std::string name; // "@" for the apex
      std::string type;
      std::string target;
      unsigned int ttl = 0;
    };

    Response send(std::string_view method, const std::string& path, const std::string& body = std::string());
    void sync_time();
    Result<std::vector<Entry>> fetch_entries(std::string_view domain, std::string_view type, const std::optional<std::string>& name);
    Status create_entry(const DnsRecord&, const std::string& target);
    Status update_entry_ttl(const DnsRecord&, const Entry&);
    Status delete_entry(const DnsRecord&, const Entry&);
    Status refresh_zone(std::string_view domain);

    std::string application_key;
    std::string application_secret;
    std::string consumer_key;
    std::string base_url;
    long time_delta = 0; // server time - local time, in seconds
    bool time_synced = false;
  };
}
