#include "ionos.hxx"
#include "helpers.hxx"
#include <libcurlez/curlez.hxx>
#include <algorithm>
#include <cstdlib>

using namespace std;
using namespace DomainEz;

////////////////////////////////////////////////////////////////////////////////
// Helpers
////////////////////////////////////////////////////////////////////////////////

// IONOS record names are fully qualified, DomainEz names are relative to the zone.
static string to_ionos_name(const string& name, const string& zone)
{
  return (name.empty() || name == "@") ? zone : name + '.' + zone;
}

static string from_ionos_name(const string& fqdn, const string& zone)
{
  string suffix = '.' + zone;

  if (fqdn == zone)
    return "@";
  if (fqdn.size() > suffix.size() && fqdn.compare(fqdn.size() - suffix.size(), suffix.size(), suffix) == 0)
    return fqdn.substr(0, fqdn.size() - suffix.size());
  return fqdn;
}

static bool has_priority(const string& type)
{
  return type == "MX" || type == "SRV";
}

// "10 mail.example.com." -> priority 10, content "mail.example.com." (MX and SRV only)
struct RecordValue
{
  string content;
  optional<int> priority;
};

static RecordValue split_value(const string& type, const string& value)
{
  RecordValue result;
  size_t space = value.find(' ');

  result.content = value;
  if (has_priority(type) && space != string::npos)
  {
    char* end = nullptr;
    long priority = strtol(value.c_str(), &end, 10);

    if (end == value.c_str() + space)
    {
      result.priority = static_cast<int>(priority);
      result.content = value.substr(space + 1);
    }
  }
  return result;
}

static void write_value(Data item, const DnsRecord& record, const string& value)
{
  RecordValue split = split_value(record.type, value);

  item["name"] = to_ionos_name(record.name, record.domain);
  item["type"] = record.type;
  item["content"] = split.content;
  item["ttl"] = record.ttl;
  if (split.priority)
    item["prio"] = *split.priority;
}

////////////////////////////////////////////////////////////////////////////////
// HTTP
////////////////////////////////////////////////////////////////////////////////

IonosProvider::IonosProvider(string_view api_key, string_view base_url) :
  api_key(api_key), base_url(base_url)
{
}

IonosProvider::Response IonosProvider::send(string_view method, const string& path, const string& body) const
{
  Response result;
  CurlReader query;

  query.header("X-API-Key", api_key);
  query.header("Accept", "application/json");
  query.option(CURLOPT_CONNECTTIMEOUT, 5L);
  query.option(CURLOPT_TIMEOUT, 15L);
  query.method(string(method)).url(base_url + path);
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
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// Domains (zones)
////////////////////////////////////////////////////////////////////////////////

Result<vector<Organization>> IonosProvider::organizations()
{
  Result<vector<Organization>> result;

  result.status = 200; // no such notion at IONOS
  return result;
}

Result<vector<Domain>> IonosProvider::domains()
{
  Response response = send("GET", "/zones");
  map<string, string> ids;
  Result<vector<Domain>> result = parse_one<vector<Domain>>(response, response.body, [&ids](Data data) {
    vector<Domain> domains;

    for (Data zone : data)
    {
      Domain domain;

      domain.name = zone["name"].as<string>();
      domain.tld = tld_of(domain.name);
      ids[domain.name] = zone["id"].as<string>();
      domains.push_back(domain);
    }
    return domains;
  });

  if (result.ok())
    zone_ids = ids;
  return result;
}

Result<string> IonosProvider::zone_id(string_view domain_name)
{
  Result<string> result;
  string key(domain_name);
  auto it = zone_ids.find(key);

  if (it == zone_ids.end())
  {
    Result<vector<Domain>> listing = domains(); // refreshes zone_ids

    if (!listing.ok())
    {
      static_cast<Status&>(result) = listing;
      return result;
    }
    it = zone_ids.find(key);
  }
  if (it == zone_ids.end())
  {
    result.status = 404;
    result.error = "zone not found: " + key;
    return result;
  }
  result.status = 200;
  result.value = it->second;
  return result;
}

Result<Domain> IonosProvider::domain(string_view domain_name)
{
  Result<Domain> result;
  Result<string> zone = zone_id(domain_name);

  static_cast<Status&>(result) = zone;
  if (zone.ok())
  {
    result.value.name = string(domain_name);
    result.value.tld = tld_of(result.value.name);
  }
  return result;
}

Result<Availability> IonosProvider::check_availability(string_view)
{
  Result<Availability> result;

  static_cast<Status&>(result) = not_implemented("ionos");
  return result;
}

Status IonosProvider::register_domain(const Registration&) { return not_implemented("ionos"); }
Status IonosProvider::renew(string_view, unsigned int)     { return not_implemented("ionos"); }
Status IonosProvider::transfer(const Transfer&)            { return not_implemented("ionos"); }

Result<vector<string>> IonosProvider::nameservers(string_view)
{
  Result<vector<string>> result;

  static_cast<Status&>(result) = not_implemented("ionos");
  return result;
}

Result<bool> IonosProvider::serves_dns(string_view)
{
  Result<bool> result;
 
  static_cast<Status&>(result) = not_implemented("ionos");
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// DNS records
////////////////////////////////////////////////////////////////////////////////

// Lists the records matching `type` (empty: any) and `name` (nullopt: any).
// One request, IONOS returns the records along with the zone.
Result<vector<IonosProvider::Entry>> IonosProvider::fetch_entries(string_view domain, string_view type, const optional<string>& name)
{
  Result<vector<Entry>> result;
  Result<string> zone = zone_id(domain);
  string zone_name(domain);
  string query;
  Response response;
  string wanted_name;

  static_cast<Status&>(result) = zone;
  if (!zone.ok())
    return result;
  if (name)
    wanted_name = (name->empty() || *name == "@") ? string("@") : *name;
  if (!type.empty())
    query = "?recordType=" + string(type);
  if (name)
    query += (query.empty() ? "?" : "&") + string("recordName=") + to_ionos_name(wanted_name, zone_name);
  response = send("GET", "/zones/" + zone.value + query);
  return parse_one<vector<Entry>>(response, response.body, [&](Data data) {
    vector<Entry> entries;

    for (Data record : data["records"])
    {
      Entry entry;
      string content = record["content"].defaults_to<string>("");

      entry.id = record["id"].as<string>();
      entry.name = from_ionos_name(record["name"].defaults_to<string>(""), zone_name);
      entry.type = record["type"].defaults_to<string>("");
      entry.ttl = record["ttl"].defaults_to<unsigned int>(0);
      entry.target = content;
      if (has_priority(entry.type) && record["prio"].exists())
        entry.target = record["prio"].as<string>() + ' ' + content;
      // The query filters should already do this, but they're cheap to double-check.
      if ((name && entry.name != wanted_name) || (!type.empty() && entry.type != type))
        continue ;
      entries.push_back(entry);
    }
    return entries;
  });
}

Result<vector<DnsRecord>> IonosProvider::dns_records(string_view domain_name)
{
  Result<vector<DnsRecord>> result;
  Result<vector<Entry>> entries = fetch_entries(domain_name, string_view(), nullopt);

  static_cast<Status&>(result) = entries;
  if (entries.ok())
    result.value = group_entries(domain_name, entries.value);
  return result;
}

Result<DnsRecord> IonosProvider::dns_record(string_view domain_name, string_view name, string_view type)
{
  Result<DnsRecord> result;
  Result<vector<Entry>> entries = fetch_entries(domain_name, type, string(name));

  static_cast<Status&>(result) = entries;
  if (!entries.ok())
    return result;
  if (entries.value.empty())
  {
    result.status = 404;
    result.error = "record not found";
    return result;
  }
  result.value = group_entries(domain_name, entries.value).front();
  return result;
}

// Creates several records in one request (the API takes a JSON array).
Status IonosProvider::create_entries(const DnsRecord& record, const vector<string>& values)
{
  Result<string> zone = zone_id(record.domain);
  string body = "[";

  if (!zone.ok())
    return zone;
  for (const string& value : values)
  {
    DataTree item;

    write_value(item.as_data(), record, value);
    body += (body.size() > 1 ? "," : "") + item.to_json();
  }
  body += ']';
  return send("POST", "/zones/" + zone.value + "/records", body);
}

Status IonosProvider::update_entry(const DnsRecord& record, const Entry& entry)
{
  Result<string> zone = zone_id(record.domain);
  DataTree body;

  if (!zone.ok())
    return zone;
  write_value(body.as_data(), record, entry.target);
  return send("PUT", "/zones/" + zone.value + "/records/" + entry.id, body.to_json());
}

Status IonosProvider::delete_entry(const DnsRecord& record, const Entry& entry)
{
  Result<string> zone = zone_id(record.domain);

  if (!zone.ok())
    return zone;
  return send("DELETE", "/zones/" + zone.value + "/records/" + entry.id);
}

Status IonosProvider::add_dns_record(const DnsRecord& record)
{
  Result<vector<Entry>> existing = fetch_entries(record.domain, record.type, record.name);

  if (!existing.ok())
    return existing;
  if (!existing.value.empty())
    return failure(409, "record already exists");
  return create_entries(record, record.values);
}

// Brings the zone in line with record.values: entries whose value is no longer
// wanted are deleted, missing ones are created, the others only get the new ttl.
// Stops at the first failure.
//
// Never uses PUT /zones/{id}: that route replaces every record of the zone.
Status IonosProvider::update_dns_record(const DnsRecord& record)
{
  Result<vector<Entry>> existing = fetch_entries(record.domain, record.type, record.name);
  vector<string> missing;

  if (!existing.ok())
    return existing;
  if (existing.value.empty())
    return failure(404, "record not found");
  for (const Entry& entry : existing.value)
  {
    bool wanted = find(record.values.begin(), record.values.end(), entry.target) != record.values.end();
    Status status;

    if (!wanted)
      status = delete_entry(record, entry);
    else if (entry.ttl != record.ttl)
      status = update_entry(record, entry);
    else
      continue ;
    if (!status.ok())
      return status;
  }
  for (const string& value : record.values)
  {
    bool exists = any_of(existing.value.begin(), existing.value.end(), [&value](const Entry& entry) {
      return entry.target == value;
    });

    if (!exists)
      missing.push_back(value);
  }
  if (missing.empty())
  {
    Status done;

    done.status = 200;
    return done;
  }
  return create_entries(record, missing);
}

Status IonosProvider::delete_dns_record(const DnsRecord& record)
{
  Result<vector<Entry>> existing = fetch_entries(record.domain, record.type, record.name);

  if (!existing.ok())
    return existing;
  if (existing.value.empty())
    return failure(404, "record not found");
  for (const Entry& entry : existing.value)
  {
    Status status = delete_entry(record, entry);

    if (!status.ok())
      return status;
  }
  Status done;

  done.status = 200;
  return done;
}
