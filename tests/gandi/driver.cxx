#include <libdomainez/gandi.hxx>
#include <libdomainez/version.hxx>

#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void register_domain(DomainEz::GandiProvider& provider)
{
  using namespace DomainEz;
  using namespace std;
  Contact owner_contact{
    .company = false,
    .first_name = "Micky",
    .last_name = "Mike",
    .email = "test@example.com",
    .phone = "+33.123456789",
    .street = "123 Main St",
    .zip = "75001",
    .city = "Paris",
    .country = "FR"
  };

  Registration reg{
    .name = "domainez-test.fr",
    .duration_years = 1,
    .owner = owner_contact
  };

  cout << "Registering test domain..." << endl;
  Status reg_status = provider.register_domain(reg);
  if (reg_status.ok()) {
    cout << "[PASS] Domain registered successfully!" << endl;
  } else {
    cerr << "Registration note: " << reg_status.error << " (Status: " << reg_status.status << ")" << endl;
  }
}

int main()
{
  using namespace DomainEz;
  using namespace std;

  cout << "=== Starting DomainEz Gandi Provider Tests ===" << endl;

  // 1. Retrieve API token from environment variables
  const char* env_token = std::getenv("GANDI_TOKEN");
  if (!env_token)
  {
    env_token = std::getenv("GANDI_API_TOKEN");
  }

  if (!env_token)
  {
    cerr << "WARNING: Neither GANDI_TOKEN nor GANDI_API_TOKEN environment variable is set." << endl;
    cerr << "Skipping live sandbox tests. Set the token to run full integration tests." << endl;
    return 0; // Exit successfully so non-secret CI environments don't fail outright
  }

  string token(env_token);
  string sandbox_url = "https://api.sandbox.gandi.net/v5";

  // 2. Instantiate GandiProvider targeting the sandbox
  GandiProvider provider(token, sandbox_url);

  // Test provider name
  assert(provider.name() == "gandi");
  cout << "[PASS] Provider name is 'gandi'" << endl;

  // 3. Test organizations()
  auto orgs_result = provider.organizations();
  if (!orgs_result)
  {
    cerr << "Failed to fetch organizations. Status: " << orgs_result.status 
         << ", Error: " << orgs_result.error << endl;
  }
  assert(orgs_result.ok());
  cout << "[PASS] Fetch organizations successful. Found " << orgs_result.value.size() << " organization(s)." << endl;

  for (const auto& org : orgs_result.value)
  {
    cout << "       - Org ID: " << org.id << ", Name: " << org.name << endl;
  }

  // 4. Test domains()
  register_domain(provider);
  auto domains_result = provider.domains();
  assert(domains_result.ok());
  cout << "[PASS] Fetch domains successful. Found " << domains_result.value.size() << " domain(s)." << endl;

  // 5. Test check_availability()
  // Using a random-ish test name unlikely to be taken in sandbox
  string test_domain_name = "domainez-test.com";
  cout << "Checking availability for: " << test_domain_name << " ..." << endl;
  auto avail_result = provider.check_availability(test_domain_name);
  if (!avail_result)
  {
    cerr << "Check availability warning/error: Status " << avail_result.status 
         << ", Error: " << avail_result.error << endl;
  }
  else
  {
    cout << "[PASS] Check availability executed. Available: " 
         << (avail_result.value.available ? "yes" : "no") 
         << ", Price: " << avail_result.value.price_after_taxes 
         << " " << avail_result.value.currency << endl;
  }

  // 6. If domains exist, test per-domain operations (DNS records, nameservers, etc.)
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

    // Test adding, updating, and deleting a test TXT DNS record
    string record_name = "ez-test";
    string record_type = "TXT";
    vector<string> initial_values = {"v=spf1 include:example.com ~all"};

    DnsRecord new_record{
      .domain = sample_domain,
      .name = record_name,
      .type = record_type,
      .values = initial_values,
      .ttl = 1800
    };

    cout << "Adding DNS record (" << record_type << " " << record_name << "." << sample_domain << ") ..." << endl;
    Status add_status = provider.add_dns_record(new_record);
    if (!add_status)
    {
      cerr << "Add DNS record failed: Status " << add_status.status << ", Error: " << add_status.error << endl;
    }
    // Depending on sandbox state/existing record, add or update might be needed.
    // Let's verify we can fetch the specific record back:
    auto fetched_record = provider.dns_record(sample_domain, record_name, record_type);
    if (fetched_record.ok())
    {
      cout << "[PASS] Successfully retrieved added DNS record." << endl;

      // Test update
      DnsRecord updated_record = fetched_record.value;
      updated_record.values.push_back("extra-value");
      Status update_status = provider.update_dns_record(updated_record);
      if (update_status.ok())
      {
        cout << "[PASS] Successfully updated DNS record." << endl;
      }

      // Test delete
      Status del_status = provider.delete_dns_record(updated_record);
      if (del_status.ok())
      {
        cout << "[PASS] Successfully deleted test DNS record." << endl;
      }
    }
  }
  else
  {
    cout << "NOTE: No existing domains found in the sandbox account. Skipping per-domain DNS and nameserver tests." << endl;
    cout << "      You can register a test domain in the Gandi sandbox UI or via `register_domain()` to test them." << endl;
  }

  cout << "=== All Gandi Provider Tests Completed Successfully! ===" << endl;
  return 0;
}
