#include "ovh.hxx"
#include "helpers.hxx"
#include <libcurlez/curlez.hxx>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

using namespace std;
using namespace DomainEz;

////////////////////////////////////////////////////////////////////////////////
// SHA-1, needed to sign requests ("$1$" + hex(SHA1(...)))
////////////////////////////////////////////////////////////////////////////////

static uint32_t rotate_left(uint32_t value, int bits)
{
  return (value << bits) | (value >> (32 - bits));
}

static string sha1_hex(const string& input)
{
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  string message = input;
  uint64_t bit_length = static_cast<uint64_t>(input.size()) * 8;
  ostringstream result;

  message += static_cast<char>(0x80);
  while (message.size() % 64 != 56)
    message += '\0';
  for (int shift = 56 ; shift >= 0 ; shift -= 8)
    message += static_cast<char>((bit_length >> shift) & 0xFF);
  for (size_t offset = 0 ; offset < message.size() ; offset += 64)
  {
    uint32_t w[80];
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

    for (int i = 0 ; i < 16 ; ++i)
    {
      const unsigned char* p = reinterpret_cast<const unsigned char*>(message.data()) + offset + i * 4;

      w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    for (int i = 16 ; i < 80 ; ++i)
      w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0 ; i < 80 ; ++i)
    {
      uint32_t f, k, temp;

      if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
      temp = rotate_left(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rotate_left(b, 30); b = a; a = temp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (uint32_t word : h)
    result << hex << setw(8) << setfill('0') << word;
  return result.str();
}

////////////////////////////////////////////////////////////////////////////////
// Helpers
////////////////////////////////////////////////////////////////////////////////

// OVH uses an empty subDomain for the apex, DomainEz uses "@".
static string to_ovh_name(const string& name)
{
  return name == "@" ? string() : name;
}

static string from_ovh_name(const string& name)
{
  return name.empty() ? string("@") : name;
}

static string zone_path(string_view domain)
{
  return "/domain/zone/" + string(domain);
}

////////////////////////////////////////////////////////////////////////////////
// HTTP
////////////////////////////////////////////////////////////////////////////////

OvhProvider::OvhProvider(string_view application_key, string_view application_secret, string_view consumer_key, string_view base_url) :
  application_key(application_key),
  application_secret(application_secret),
  consumer_key(consumer_key),
  base_url(base_url)
{
}

// Signatures embed a timestamp that OVH checks against its own clock.
void OvhProvider::sync_time()
{
  CurlReader query;

  query.option(CURLOPT_CONNECTTIMEOUT, 5L);
  query.option(CURLOPT_TIMEOUT, 15L);
  query.method("GET").url(base_url + "/auth/time");
  if (query.perform() == 200)
  {
    long server_time = atol(string(query.response_body()).c_str());

    time_delta = server_time - static_cast<long>(time(nullptr));
    time_synced = true;
  }
}

OvhProvider::Response OvhProvider::send(string_view method, const string& path, const string& body)
{
  Response result;
  CurlReader query;
  string url = base_url + path;

  if (!time_synced)
    sync_time();
  {
    string timestamp = to_string(static_cast<long>(time(nullptr)) + time_delta);
    string signature = "$1$" + sha1_hex(
      application_secret + '+' + consumer_key + '+' + string(method) + '+' + url + '+' + body + '+' + timestamp
    );

    query.header("X-Ovh-Application", application_key);
    query.header("X-Ovh-Consumer", consumer_key);
    query.header("X-Ovh-Timestamp", timestamp);
    query.header("X-Ovh-Signature", signature);
  }
  query.header("Accept", "application/json");
  query.option(CURLOPT_CONNECTTIMEOUT, 5L);
  query.option(CURLOPT_TIMEOUT, 15L);
  query.method(string(method)).url(url);
  if (!body.empty())
  {
    query.header("Content-Type", "application/json");
    query.body(body); // `body` must outlive perform(): curl doesn't copy it
  }
  else if (method == "POST" || method == "PUT")
    query.header("Content-Length", "0");
  result.status = query.perform();
  result.body = string(query.response_body());
  if (result.status == 0)
    result.error = query.error();
  else if (!result.ok())
    result.error = result.body;
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// Organizations & domains
////////////////////////////////////////////////////////////////////////////////

Result<vector<Organization>> OvhProvider::organizations()
{
  Result<vector<Organization>> result;

  result.status = 200; // no such notion at OVH
  return result;
}

Result<vector<Domain>> OvhProvider::domains()
{
  Response response = send("GET", "/domain");

  return parse_one<vector<Domain>>(response, response.body, [](Data data) {
    vector<Domain> domains;

    for (Data entry : data)
    {
      Domain domain;

      domain.name = entry.as<string>();
      domain.tld = tld_of(domain.name);
      domains.push_back(domain);
    }
    return domains;
  });
}

Result<Domain> OvhProvider::domain(string_view domain_name)
{
  string name(domain_name);
  Response response = send("GET", "/domain/" + name);
  Result<Domain> result = parse_one<Domain>(response, response.body, [&name](Data data) {
    Domain domain;

    domain.name = data["domain"].defaults_to<string>(name);
    domain.tld = tld_of(domain.name);
    domain.owner = data["whoisOwner"].defaults_to<string>("");
    return domain;
  });

  if (result.ok())
  {
    // Dates and renewal live in the service information. Best effort: a token
    // that can't read them still gets the basic details.
    Response service = send("GET", "/domain/" + name + "/serviceInfos");

    if (service.ok())
    {
      try
      {
        DataTree tree;

        tree.from_json(service.body);
        Data data = tree.as_data();
        result.value.created_at = parse_time(data["creation"].defaults_to<string>(""));
        result.value.expires_at = parse_time(data["expiration"].defaults_to<string>(""));
        if (data["status"].exists())
          result.value.status.push_back(data["status"].as<string>());
        if (data["renew"].exists())
          result.value.autorenew = data["renew"]["automatic"].defaults_to<bool>(false);
      }
      catch (const std::exception&) {}
    }
  }
  return result;
}

Result<Availability> OvhProvider::check_availability(string_view)
{
  Result<Availability> result;

  static_cast<Status&>(result) = not_implemented("ovh");
  return result;
}

Status OvhProvider::register_domain(const Registration&) { return not_implemented("ovh"); }
Status OvhProvider::renew(string_view, unsigned int)     { return not_implemented("ovh"); }
Status OvhProvider::transfer(const Transfer&)            { return not_implemented("ovh"); }

Result<vector<string>> OvhProvider::nameservers(string_view domain_name)
{
  Response response = send("GET", zone_path(domain_name));

  return parse_one<vector<string>>(response, response.body, [](Data data) {
    return data["nameServers"].to_vector<string>();
  });
}

Result<bool> OvhProvider::serves_dns(string_view domain_name)
{
  Response response = send("GET", "/domain/" + string(domain_name));
 
  return parse_one<bool>(response, response.body, [](Data data) {
    string type = data["nameServerType"].defaults_to<string>("");
 
    if (type == "hosted" || type == "anycast")
      return true;
    if (type == "external")
      return false;
    throw runtime_error("unknown nameServerType: " + type);
  });
}

////////////////////////////////////////////////////////////////////////////////
// DNS records
////////////////////////////////////////////////////////////////////////////////

// Lists the zone entries matching `type` (empty: any) and `name` (nullopt: any).
// OVH only returns entry ids from the list route, so this costs one request
// per entry.
Result<vector<OvhProvider::Entry>> OvhProvider::fetch_entries(string_view domain, string_view type, const optional<string>& name)
{
  Result<vector<Entry>> result;
  string path = zone_path(domain) + "/record";
  string query;
  Response list;
  vector<string> ids;

  if (!type.empty())
    query = "?fieldType=" + string(type);
  if (name && *name != "@" && !name->empty())
    query += (query.empty() ? "?" : "&") + string("subDomain=") + *name;
  list = send("GET", path + query);
  static_cast<Status&>(result) = list;
  if (!list.ok())
    return result;
  try
  {
    DataTree tree;

    tree.from_json(list.body);
    for (Data id : tree.as_data())
      ids.push_back(id.as<string>());
  }
  catch (const std::exception& error)
  {
    result.status = 0;
    result.error = string("unexpected response: ") + error.what();
    return result;
  }
  for (const string& id : ids)
  {
    Response response = send("GET", path + '/' + id);
    Result<Entry> entry = parse_one<Entry>(response, response.body, [&id](Data data) {
      Entry entry;

      entry.id = id;
      entry.name = from_ovh_name(data["subDomain"].defaults_to<string>(""));
      entry.type = data["fieldType"].defaults_to<string>("");
      entry.target = data["target"].defaults_to<string>("");
      entry.ttl = data["ttl"].defaults_to<unsigned int>(0);
      return entry;
    });

    if (!entry.ok())
    {
      static_cast<Status&>(result) = entry;
      result.value.clear();
      return result;
    }
    if (name && entry.value.name != from_ovh_name(to_ovh_name(*name)))
      continue ; // the apex can't be filtered server-side
    result.value.push_back(entry.value);
  }
  return result;
}

Result<vector<DnsRecord>> OvhProvider::dns_records(string_view domain_name)
{
  Result<vector<DnsRecord>> result;
  Result<vector<Entry>> entries = fetch_entries(domain_name, string_view(), nullopt);

  static_cast<Status&>(result) = entries;
  if (entries.ok())
    result.value = group_entries(domain_name, entries.value);
  return result;
}

Result<DnsRecord> OvhProvider::dns_record(string_view domain_name, string_view name, string_view type)
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
  result.value.domain = string(domain_name);
  result.value.name = string(name);
  result.value.type = string(type);
  result.value.ttl = entries.value.front().ttl;
  for (const Entry& entry : entries.value)
    result.value.values.push_back(entry.target);
  return result;
}

Status OvhProvider::create_entry(const DnsRecord& record, const string& target)
{
  DataTree body;

  body["fieldType"] = record.type;
  if (to_ovh_name(record.name) != "")
    body["subDomain"] = to_ovh_name(record.name);
  body["target"] = target;
  body["ttl"] = record.ttl;
  return send("POST", zone_path(record.domain) + "/record", body.to_json());
}

Status OvhProvider::update_entry_ttl(const DnsRecord& record, const Entry& entry)
{
  DataTree body;

  body["target"] = entry.target;
  body["ttl"] = record.ttl;
  return send("PUT", zone_path(record.domain) + "/record/" + entry.id, body.to_json());
}

Status OvhProvider::delete_entry(const DnsRecord& record, const Entry& entry)
{
  return send("DELETE", zone_path(record.domain) + "/record/" + entry.id);
}

Status OvhProvider::refresh_zone(string_view domain)
{
  return send("POST", zone_path(domain) + "/refresh");
}

Status OvhProvider::add_dns_record(const DnsRecord& record)
{
  Result<vector<Entry>> existing = fetch_entries(record.domain, record.type, record.name);

  if (!existing.ok())
    return existing;
  if (!existing.value.empty())
    return failure(409, "record already exists");
  for (const string& value : record.values)
  {
    Status status = create_entry(record, value);

    if (!status.ok())
      return status;
  }
  return refresh_zone(record.domain);
}

// Brings the zone entries in line with record.values: entries whose target
// is no longer wanted are deleted, missing ones are created, the others only
// get the new ttl. Stops at the first failure, without refreshing the zone.
Status OvhProvider::update_dns_record(const DnsRecord& record)
{
  Result<vector<Entry>> existing = fetch_entries(record.domain, record.type, record.name);

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
      status = update_entry_ttl(record, entry);
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
    {
      Status status = create_entry(record, value);

      if (!status.ok())
        return status;
    }
  }
  return refresh_zone(record.domain);
}

Status OvhProvider::delete_dns_record(const DnsRecord& record)
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
  return refresh_zone(record.domain);
}
