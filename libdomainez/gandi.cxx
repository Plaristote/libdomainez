#include "gandi.hxx"
#include "helpers.hxx"
#include <libcurlez/curlez.hxx>
#include <cstdlib>
#include <sstream>

using namespace std;
using namespace DomainEz;

static const unsigned int page_size = 100;
static const unsigned int max_pages = 100; // Can't loop forever

////////////////////////////////////////////////////////////////////////////////
// JSON <-> neutral types
////////////////////////////////////////////////////////////////////////////////

static time_point load_date(Data data, const string& key)
{
  Data dates = data["dates"];

  if (dates.exists() && dates[key].exists())
    return parse_time(dates[key].defaults_to<string>(""));
  return parse_time(data[key].defaults_to<string>(""));
}

static bool load_autorenew(Data data)
{
  Data autorenew = data["autorenew"];
  bool result = false;

  if (autorenew.exists())
  {
    if (autorenew["enabled"].exists())
      autorenew = autorenew["enabled"];
    try { result = autorenew.defaults_to<bool>(false); }
    catch (const std::exception&) {}
  }
  return result;
}

static Organization load_organization(Data data)
{
  Organization org;

  org.id = data["id"].as<string>();
  org.name = data["name"].defaults_to<string>("");
  return org;
}

static Domain load_domain(Data data, const string& scope)
{
  Domain domain;

  domain.name = data["fqdn"].defaults_to<string>("");
  domain.tld = data["tld"].defaults_to<string>("");
  domain.owner = data["owner"].defaults_to<string>("");
  domain.organization_id = data["sharing_id"].defaults_to<string>(scope);
  domain.autorenew = load_autorenew(data);
  domain.created_at = load_date(data, "registry_created_at");
  domain.expires_at = load_date(data, "registry_ends_at");
  domain.renew_begins_at = load_date(data, "renew_begins_at");
  domain.updated_at = load_date(data, "updated_at");
  domain.status = data["status"].to_vector<string>();
  return domain;
}

static DnsRecord load_record(Data data, const string& domain)
{
  DnsRecord record;

  record.domain = domain;
  record.name = data["rrset_name"].defaults_to<string>("");
  record.type = data["rrset_type"].defaults_to<string>("");
  record.values = data["rrset_values"].to_vector<string>();
  record.ttl = data["rrset_ttl"].defaults_to<unsigned int>(0);
  return record;
}

static void write_contact(Data data, const Contact& contact)
{
  data["type"] = string(contact.company ? "company" : "individual");
  data["given"] = contact.first_name;
  data["family"] = contact.last_name;
  if (contact.company)
    data["orgname"] = contact.organization;
  data["email"] = contact.email;
  data["phone"] = contact.phone;
  data["streetaddr"] = contact.street;
  data["zip"] = contact.zip;
  data["city"] = contact.city;
  if (!contact.state.empty())
    data["state"] = contact.state;
  data["country"] = contact.country;
}

static string records_path(string_view domain)
{
  return "/livedns/domains/" + string(domain) + "/records";
}

static string record_path(string_view domain, string_view name, string_view type)
{
  return records_path(domain) + '/' + string(name) + '/' + string(type);
}

////////////////////////////////////////////////////////////////////////////////
// HTTP
////////////////////////////////////////////////////////////////////////////////

GandiProvider::GandiProvider(string_view token, string_view base_url) :
  token(token), base_url(base_url)
{
}

GandiProvider::Response GandiProvider::send(string_view method, const string& path, const string& sharing_id, const string& body, unsigned int page) const
{
  Response result;
  CurlReader query;
  ostringstream url;
  char separator = path.find('?') == string::npos ? '?' : '&';

  url << base_url << path;
  if (!sharing_id.empty())
  {
    url << separator << "sharing_id=" << sharing_id;
    separator = '&';
  }
  if (page > 0)
    url << separator << "page=" << page << "&per_page=" << page_size;
  query.header("Authorization", "Bearer " + token);
  query.header("Accept", "application/json");
  query.option(CURLOPT_CONNECTTIMEOUT, 5L);
  query.option(CURLOPT_TIMEOUT, 15L);
  query.method(string(method)).url(url.str());
  if (!body.empty())
  {
    query.header("Content-Type", "application/json");
    query.body(body); // `body` must outlive perform(): curl doesn't copy it
  }
  result.status = query.perform();
  result.body = string(query.response_body());
  if (result.status == 0)
    result.error = query.error();
  else if (!result.ok())
    result.error = result.body;
  else
    result.total = static_cast<unsigned int>(atoi(query.response_header("Total-Count").c_str()));
  return result;
}

template<typename T, typename LOADER>
Result<vector<T>> GandiProvider::fetch_all(const string& path, const string& sharing_id, LOADER load) const
{
  Result<vector<T>> result;

  for (unsigned int page = 1 ; page <= max_pages ; ++page)
  {
    Response response = send("GET", path, sharing_id, string(), page);
    size_t count = 0;

    static_cast<Status&>(result) = response;
    if (!response.ok())
      break ;
    try
    {
      DataTree tree;

      tree.from_json(response.body);
      for (Data entry : tree.as_data())
      {
        result.value.push_back(load(entry));
        ++count;
      }
    }
    catch (const std::exception& error)
    {
      result.status = 0;
      result.error = string("unexpected response: ") + error.what();
      break ;
    }
    if (count < page_size || (response.total > 0 && result.value.size() >= response.total))
      break ;
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// Organizations & domains
////////////////////////////////////////////////////////////////////////////////

Result<vector<Organization>> GandiProvider::organizations()
{
  return fetch_all<Organization>("/organization/organizations", string(), load_organization);
}

Result<vector<Domain>> GandiProvider::domains()
{
  Result<vector<Domain>> result;
  Result<vector<Organization>> orgs = organizations();
  vector<string> org_ids;
  Status denied;
  bool any_succeeded = false;

  if (orgs.ok())
  {
    for (const Organization& org : orgs.value)
      org_ids.push_back(org.id);
  }
  if (org_ids.empty())
    org_ids.push_back(string()); // can't list organizations: use the token's default one
  for (const string& org_id : org_ids)
  {
    auto page = fetch_all<Domain>("/domain/domains", org_id, [&org_id](Data data) {
      return load_domain(data, org_id);
    });

    if (page.ok())
    {
      any_succeeded = true;
      result.value.insert(result.value.end(), page.value.begin(), page.value.end());
    }
    else if (page.status == 403 || page.status == 404)
      denied = page; // the token has no domain access in this organization: skip it
    else
      return page;
  }
  if (!any_succeeded && denied.status != 0)
  {
    static_cast<Status&>(result) = denied;
    return result;
  }
  result.status = 200;
  domain_scopes.clear();
  for (const Domain& domain : result.value)
    domain_scopes[domain.name] = domain.organization_id;
  domain_scopes_loaded = true;
  return result;
}

string GandiProvider::sharing_id_for(string_view domain_name)
{
  string key(domain_name);
  auto it = domain_scopes.find(key);

  if (it == domain_scopes.end() && !domain_scopes_loaded)
  {
    domains(); // populates domain_scopes
    it = domain_scopes.find(key);
  }
  return it != domain_scopes.end() ? it->second : string();
}

Result<Domain> GandiProvider::domain(string_view domain_name)
{
  string scope = sharing_id_for(domain_name);
  Response response = send("GET", "/domain/domains/" + string(domain_name), scope);

  return parse_one<Domain>(response, response.body, [&scope](Data data) {
    return load_domain(data, scope);
  });
}

Result<Availability> GandiProvider::check_availability(string_view domain_name)
{
  Response response = send("GET", "/domain/check?name=" + string(domain_name) + "&processes=create", string());

  return parse_one<Availability>(response, response.body, [domain_name](Data data) {
    Availability result;

    result.name = string(domain_name);
    for (Data product : data["products"])
    {
      if (product["name"].defaults_to<string>("") != domain_name)
        continue ;
      result.status = product["status"].defaults_to<string>("undefined");
      result.available = result.status == "available";
      for (Data price : product["prices"])
      {
        result.price_before_taxes = price["price_before_taxes"].defaults_to<double>(0.0);
        result.price_after_taxes = price["price_after_taxes"].defaults_to<double>(0.0);
        result.currency = price["currency"].defaults_to<string>(data["currency"].defaults_to<string>("EUR"));
        break ;
      }
      break ;
    }
    return result;
  });
}

Status GandiProvider::register_domain(const Registration& registration)
{
  DataTree body;
  Response response;

  body["fqdn"] = registration.name;
  body["duration"] = static_cast<int>(registration.duration_years);
  write_contact(body["owner"], registration.owner);
  response = send("POST", "/domain/domains", registration.organization_id, body.to_json());
  if (response.ok())
    domain_scopes[registration.name] = registration.organization_id;
  return response;
}

Status GandiProvider::renew(string_view domain_name, unsigned int years)
{
  DataTree body;

  body["duration"] = static_cast<int>(years);
  return send("POST", "/domain/domains/" + string(domain_name) + "/renew", sharing_id_for(domain_name), body.to_json());
}

Status GandiProvider::transfer(const Transfer& transfer)
{
  DataTree body;

  body["fqdn"] = transfer.name;
  body["authinfo"] = transfer.auth_code;
  write_contact(body["owner"], transfer.owner);
  return send("POST", "/domain/transferin", transfer.organization_id, body.to_json());
}

Result<vector<string>> GandiProvider::nameservers(string_view domain_name)
{
  Response response = send("GET", "/livedns/domains/" + string(domain_name) + "/nameservers", sharing_id_for(domain_name));

  return parse_one<vector<string>>(response, response.body, [](Data data) {
    return data.to_vector<string>();
  });
}

Result<bool> GandiProvider::serves_dns(string_view domain_name)
{
  Response response = send("GET", "/domain/domains/" + string(domain_name) + "/livedns", sharing_id_for(domain_name));

  return parse_one<bool>(response, response.body, [](Data data) {
    string current = data["current"].defaults_to<string>("");

    if (current.empty())
      throw runtime_error("no name server configuration in the answer");
    return current == "livedns";
  });
}

////////////////////////////////////////////////////////////////////////////////
// DNS records (LiveDNS)
////////////////////////////////////////////////////////////////////////////////

Result<vector<DnsRecord>> GandiProvider::dns_records(string_view domain_name)
{
  string domain(domain_name);

  return fetch_all<DnsRecord>(records_path(domain), sharing_id_for(domain), [&domain](Data data) {
    return load_record(data, domain);
  });
}

Result<DnsRecord> GandiProvider::dns_record(string_view domain_name, string_view name, string_view type)
{
  string domain(domain_name);
  Response response = send("GET", record_path(domain, name, type), sharing_id_for(domain));

  return parse_one<DnsRecord>(response, response.body, [&domain](Data data) {
    return load_record(data, domain);
  });
}

Status GandiProvider::add_dns_record(const DnsRecord& record)
{
  DataTree body;

  body["rrset_name"] = record.name;
  body["rrset_type"] = record.type;
  body["rrset_ttl"] = record.ttl;
  body["rrset_values"].from_vector(record.values);
  return send("POST", records_path(record.domain), sharing_id_for(record.domain), body.to_json());
}

Status GandiProvider::update_dns_record(const DnsRecord& record)
{
  DataTree body;

  body["rrset_ttl"] = record.ttl;
  body["rrset_values"].from_vector(record.values);
  return send("PUT", record_path(record.domain, record.name, record.type), sharing_id_for(record.domain), body.to_json());
}

Status GandiProvider::delete_dns_record(const DnsRecord& record)
{
  return send("DELETE", record_path(record.domain, record.name, record.type), sharing_id_for(record.domain));
}
