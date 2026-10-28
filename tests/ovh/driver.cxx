#include <libdomainez/ovh.hxx>
#include <libdomainez/version.hxx>

#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main()
{
  using namespace DomainEz;
  using namespace std;

  cout << "=== Starting DomainEz OVH Provider Tests ===" << endl;

  const char* api_key = std::getenv("OVH_API_KEY");
  const char* api_secret = std::getenv("OVH_API_SECRET");
  const char* consumer_key = std::getenv("OVH_CONSUMER_KEY");
  
  if (!api_key || !api_secret || !consumer_key)
  {
    cerr << "WARNING: missing OVH_API_KEY, OVH_API_SECRET and/or OVH_CONSUMER_KEY" << endl;
    cerr << "Skipping live tests. Set the token to run the full integration tests." << endl;
    return 0;
  }

  OvhProvider provider(api_key, api_secret, consumer_key, OvhProvider::europe);

  assert(provider.name() == "ovh");
  cout << "[PASS] Provider name is 'ovh'" << endl;

  auto domains_result = provider.domains();
  assert(domains_result.ok());
  cout << "[PASS] Fetch domains successful. Found " << domains_result.value.size() << " domain(s)." << endl;

  if (!domains_result.value.empty())
  {
    const auto& sample_domain = domains_result.value.front().name;
    cout << "Testing per-domain operations on: " << sample_domain << endl;

    // Test domain details
    auto single_domain = provider.domain(sample_domain);
    assert(single_domain.ok());
    cout << "[PASS] Fetched single domain details for " << sample_domain << endl;

    // Test nameservers
    auto ns_result = provider.nameservers(sample_domain);
    assert(ns_result.ok());
    cout << "[PASS] Fetched nameservers successfully (" << ns_result.value.size() << " found)." << endl;

    // Test DNS records listing
    auto dns_list = provider.dns_records(sample_domain);
    assert(dns_list.ok());
    cout << "[PASS] Fetched DNS records successfully (" << dns_list.value.size() << " record sets found)." << endl;
  }
  else
  {
    cout << "NOTE: No existing domains found in the account. Skipping per-domain DNS and nameserver tests." << endl;
  }

  return 0;
}
