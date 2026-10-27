#pragma once
#include "provider.hxx"
#include <map>
#include <optional>

namespace DomainEz
{
  // IONOS (1&1) hosting DNS API: https://developer.hosting.ionos.com/docs/dns
  //
  // `api_key` is the "<prefix>.<secret>" key created in the IONOS developer
  // portal. Beware: such a key can read and change the DNS of every domain of
  // the account.
  //
  // This API only covers DNS. A "domain" here is a DNS zone hosted at IONOS,
  // and domains() only fills name and tld. IONOS has no organizations:
  // organizations() is always empty and Domain::organization_id is left blank.
  //
  // IONOS stores one entry per value, while DomainEz::DnsRecord is a record
  // set: reading groups entries by name and type, writing creates, updates and
  // deletes entries as needed. For MX and SRV, IONOS keeps the priority in its
  // own field, whereas DnsRecord values are written the usual way
  // ("10 mail.example.com."). IONOS applies changes immediately.
  //
  // Not implemented (they return status 501): nameservers(),
  // check_availability(), register_domain(), renew() and transfer().
  class LIBDOMAINEZ_SYMEXPORT IonosProvider : public Provider
  {
  public:
    static constexpr std::string_view default_url = "https://api.hosting.ionos.com/dns/v1";

    explicit IonosProvider(std::string_view api_key, std::string_view base_url = default_url);

    std::string name() const override { return "ionos"; }

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
      std::string name;   // relative to the zone, "@" for the apex
      std::string type;
      std::string target; // for MX and SRV: "<priority> <content>"
      unsigned int ttl = 0;
    };

    Response send(std::string_view method, const std::string& path, const std::string& body = std::string()) const;
    Result<std::string> zone_id(std::string_view domain);
    Result<std::vector<Entry>> fetch_entries(std::string_view domain, std::string_view type, const std::optional<std::string>& name);
    Status create_entries(const DnsRecord&, const std::vector<std::string>& values);
    Status update_entry(const DnsRecord&, const Entry&);
    Status delete_entry(const DnsRecord&, const Entry&);

    std::string api_key;
    std::string base_url;
    std::map<std::string, std::string> zone_ids; // domain name -> zone id
  };
}
