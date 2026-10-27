#include "provider.hxx"

using namespace DomainEz;
using namespace std;

Status Provider::set_dns_record(const DnsRecord& record)
{
  Status status = update_dns_record(record);

  return status.is_missing() ? add_dns_record(record) : status;
}
