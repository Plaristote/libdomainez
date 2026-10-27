#pragma once
#include "export.hxx"
#include "types.hxx"
#include <string_view>

namespace DomainEz
{
  class LIBDOMAINEZ_SYMEXPORT Provider
  {
  public:
    virtual ~Provider() = default;

    virtual std::string name() const = 0;

    virtual Result<std::vector<Organization>> organizations() = 0;

    virtual Result<std::vector<Domain>> domains() = 0;
    virtual Result<Domain> domain(std::string_view name) = 0;
    virtual Result<Availability> check_availability(std::string_view name) = 0;
    virtual Status register_domain(const Registration&) = 0;
    virtual Status renew(std::string_view name, unsigned int years = 1) = 0;
    virtual Status transfer(const Transfer&) = 0;
    virtual Result<std::vector<std::string>> nameservers(std::string_view domain) = 0;

    virtual Result<std::vector<DnsRecord>> dns_records(std::string_view domain) = 0;
    virtual Result<DnsRecord> dns_record(std::string_view domain, std::string_view name, std::string_view type) = 0;
    virtual Status add_dns_record(const DnsRecord&) = 0;
    virtual Status update_dns_record(const DnsRecord&) = 0;
    virtual Status delete_dns_record(const DnsRecord&) = 0;
    virtual Status set_dns_record(const DnsRecord& record);
  };
}
