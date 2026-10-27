#pragma once
#include "provider.hxx"
#include <map>

namespace DomainEz
{
  // Gandi scopes everything to an organization, selected with `sharing_id`.
  // Domains shared with you by other users live in *their* organization, so:
  //  - domains() queries every organization the token can see,
  //  - per-domain calls (DNS records, renew...) automatically use the
  //    organization that owns the domain (looked up once, then cached).
  //
  // The API token must have been granted access to those organizations
  // when it was created in the Gandi admin interface.
  class LIBDOMAINEZ_SYMEXPORT GandiProvider : public Provider
  {
  public:
    static constexpr std::string_view default_url = "https://api.gandi.net/v5";
    static constexpr std::string_view test_url = "https://api.sandbox.gandi.net/v5";

    explicit GandiProvider(std::string_view token, std::string_view base_url = default_url);

    std::string name() const override { return "gandi"; }

    Result<std::vector<Organization>> organizations() override;
    Result<std::vector<Domain>> domains() override;
    Result<Domain> domain(std::string_view name) override;
    Result<Availability> check_availability(std::string_view name) override;
    Status register_domain(const Registration&) override;
    Status renew(std::string_view name, unsigned int years = 1) override;
    Status transfer(const Transfer&) override;
    Result<std::vector<std::string>> nameservers(std::string_view domain) override;

    Result<bool> serves_dns(std::string_view domain) override;
    Result<std::vector<DnsRecord>> dns_records(std::string_view domain) override;
    Result<DnsRecord> dns_record(std::string_view domain, std::string_view name, std::string_view type) override;
    Status add_dns_record(const DnsRecord&) override;
    Status update_dns_record(const DnsRecord&) override;
    Status delete_dns_record(const DnsRecord&) override;

  private:
    struct Response : Status
    {
      std::string body;
      unsigned int total = 0; // Total-Count header, when paginated
    };

    Response send(std::string_view method, const std::string& path, const std::string& sharing_id, const std::string& body = std::string(), unsigned int page = 0) const;

    template<typename T, typename LOADER>
    Result<std::vector<T>> fetch_all(const std::string& path, const std::string& sharing_id, LOADER load) const;

    std::string sharing_id_for(std::string_view domain);

    std::string token;
    std::string base_url;
    std::map<std::string, std::string> domain_scopes; // domain name -> organization id
    bool domain_scopes_loaded = false;
  };
}
