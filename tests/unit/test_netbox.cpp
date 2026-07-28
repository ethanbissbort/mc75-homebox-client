/*
 * test_netbox.cpp  --  Models::Device and HBX::NbClient
 * ------------------------------------------------------
 * Covers the NetBox backend without a NetBox anywhere in sight:
 *   - Device::FromJson against trimmed but realistic NetBox payloads, including
 *     the shapes that are easy to get wrong (nulls everywhere, an unracked
 *     device, a half-U position, a non-ASCII site name, and the 3.x
 *     `device_role` / 4.x `role` rename),
 *   - the Device -> AssetSummary projection the detail screen renders,
 *   - URL construction, which is where a scanned label meets percent-encoding,
 *   - the move / status PATCH bodies and the queue payloads that carry the same
 *     work offline, plus the replay parsers that read them back.
 *
 * Nothing here opens a socket: every client path exercised either fails before
 * the transport (no session, no base URL) or is a pure static helper.
 *
 * Host build: TCHAR == char, TEXT("x") == "x". C++03 only.
 */
#include "test_framework.hpp"

#include "InventoryBackend.hpp"
#include "NbClient.hpp"
#include "Models/AssetSummary.hpp"
#include "Models/Device.hpp"
#include "Models/JsonLite.hpp"
#include "StrUtil.hpp"

using namespace HBX;

/* ------------------------------------------------------------------------ */
/* SAMPLE PAYLOADS                                                           */
/* Trimmed from the DeviceSerializer field list: the keys this client reads,  */
/* plus enough of the ones it ignores that the parser has to walk past them.  */
/* ------------------------------------------------------------------------ */

/* A NetBox 4.x device, racked, with everything populated. */
static const TCHAR* const kRackedDevice =
    TEXT("{")
        TEXT("\"id\":231,")
        TEXT("\"url\":\"https://netbox.example.com/api/dcim/devices/231/\",")
        TEXT("\"display\":\"sw-akr-01\",")
        TEXT("\"name\":\"sw-akr-01\",")
        TEXT("\"device_type\":{\"id\":12,\"display\":\"Catalyst 9300-48P\",")
            TEXT("\"manufacturer\":{\"id\":3,\"name\":\"Cisco\",\"slug\":\"cisco\",\"description\":\"\"},")
            TEXT("\"model\":\"Catalyst 9300-48P\",\"slug\":\"cat9300-48p\",\"device_count\":41},")
        TEXT("\"role\":{\"id\":5,\"name\":\"Access Switch\",\"slug\":\"access-switch\"},")
        TEXT("\"tenant\":null,")
        TEXT("\"platform\":null,")
        TEXT("\"serial\":\"FDO2447L0XY\",")
        TEXT("\"asset_tag\":\"ACME-004821\",")
        TEXT("\"site\":{\"id\":2,\"name\":\"DM-Akron\",\"slug\":\"dm-akron\"},")
        TEXT("\"location\":{\"id\":9,\"name\":\"Comms Room 1\",\"slug\":\"comms-room-1\",\"_depth\":1},")
        TEXT("\"rack\":{\"id\":14,\"name\":\"R-201\",\"device_count\":18},")
        TEXT("\"position\":42.0,")
        TEXT("\"face\":{\"value\":\"front\",\"label\":\"Front\"},")
        TEXT("\"status\":{\"value\":\"active\",\"label\":\"Active\"},")
        TEXT("\"airflow\":null,")
        TEXT("\"primary_ip\":{\"id\":883,\"family\":4,\"address\":\"10.24.8.11/24\",")
            TEXT("\"display\":\"10.24.8.11/24\"},")
        TEXT("\"primary_ip6\":null,")
        TEXT("\"description\":\"\",")
        TEXT("\"tags\":[],")
        TEXT("\"custom_fields\":{},")
        TEXT("\"created\":\"2023-06-14T00:00:00Z\"")
    TEXT("}");

/*
 * The same serializer for a device sitting on a shelf. Note the asymmetry that
 * costs a day if it is not handled: rack and position are null, but a cleared
 * face is an empty string.
 */
static const TCHAR* const kUnrackedDevice =
    TEXT("{")
        TEXT("\"id\":412,")
        TEXT("\"name\":\"spare-01\",")
        TEXT("\"device_type\":{\"id\":8,\"manufacturer\":{\"id\":2,\"name\":\"Juniper\"},")
            TEXT("\"model\":\"EX3300-48T\"},")
        TEXT("\"role\":{\"id\":4,\"name\":\"Access Switch\"},")
        TEXT("\"serial\":\"\",")
        TEXT("\"asset_tag\":\"HL-000431\",")
        TEXT("\"site\":{\"id\":1,\"name\":\"HomeLab\"},")
        TEXT("\"location\":null,")
        TEXT("\"rack\":null,")
        TEXT("\"position\":null,")
        TEXT("\"face\":\"\",")
        TEXT("\"status\":{\"value\":\"inventory\",\"label\":\"Inventory\"},")
        TEXT("\"primary_ip\":null")
    TEXT("}");

/* Everything nullable, actually null. Only the id survives. */
static const TCHAR* const kNullHeavyDevice =
    TEXT("{")
        TEXT("\"id\":7,")
        TEXT("\"name\":null,")
        TEXT("\"device_type\":null,")
        TEXT("\"role\":null,")
        TEXT("\"serial\":null,")
        TEXT("\"asset_tag\":null,")
        TEXT("\"site\":null,")
        TEXT("\"location\":null,")
        TEXT("\"rack\":null,")
        TEXT("\"position\":null,")
        TEXT("\"face\":null,")
        TEXT("\"status\":null,")
        TEXT("\"primary_ip\":null")
    TEXT("}");

/*
 * NetBox 3.x: the role arrives under `device_role`, the position is a half-U
 * slot, and the site name is raw UTF-8 -- NetBox leaves DRF's UNICODE_JSON on,
 * so responses carry the bytes rather than \uXXXX escapes.
 */
static const TCHAR* const kLegacyDevice =
    TEXT("{")
        TEXT("\"id\":88,")
        TEXT("\"name\":\"pdu-b\",")
        TEXT("\"device_role\":{\"id\":4,\"name\":\"PDU\",\"slug\":\"pdu\"},")
        TEXT("\"device_type\":{\"id\":21,\"manufacturer\":{\"id\":9,\"name\":\"APC\"},")
            TEXT("\"model\":\"AP8853\"},")
        TEXT("\"serial\":\"5A1234X56789\",")
        TEXT("\"asset_tag\":null,")
        TEXT("\"site\":{\"id\":5,\"name\":\"K\xc3\xb6ln-S\xc3\xbc\x64\",\"slug\":\"koeln-sued\"},")
        TEXT("\"location\":{\"id\":6,\"name\":\"Etage 2\"},")
        TEXT("\"rack\":{\"id\":11,\"name\":\"R-04\"},")
        TEXT("\"position\":42.5,")
        TEXT("\"face\":{\"value\":\"rear\",\"label\":\"Rear\"},")
        TEXT("\"status\":{\"value\":\"staged\",\"label\":\"Staged\"}")
    TEXT("}");

/* ------------------------------------------------------------------------ */
/* Device::FromJson                                                          */
/* ------------------------------------------------------------------------ */
TEST_CASE("Device::FromJson reads a fully populated racked device")
{
    Models::Device d;
    CHECK(d.FromJson(kRackedDevice));
    CHECK(d.IsValid());

    /* The id is a JSON number on the wire and text everywhere in this app. */
    CHECK_EQ_STR(d.GetId(), TEXT("231"));
    CHECK_EQ_STR(d.GetName(), TEXT("sw-akr-01"));
    CHECK_EQ_STR(d.GetSerial(), TEXT("FDO2447L0XY"));
    CHECK_EQ_STR(d.GetAssetTag(), TEXT("ACME-004821"));

    /* device_type.model, and device_type.manufacturer.name three levels down. */
    CHECK_EQ_STR(d.GetDeviceTypeModel(), TEXT("Catalyst 9300-48P"));
    CHECK_EQ_STR(d.GetManufacturer(), TEXT("Cisco"));
    CHECK_EQ_STR(d.GetDeviceRole(), TEXT("Access Switch"));

    /* Each placement level carries both the name to show and the id to PATCH. */
    CHECK_EQ_STR(d.GetSiteName(), TEXT("DM-Akron"));
    CHECK_EQ_STR(d.GetSiteId(), TEXT("2"));
    CHECK_EQ_STR(d.GetLocationName(), TEXT("Comms Room 1"));
    CHECK_EQ_STR(d.GetLocationId(), TEXT("9"));
    CHECK_EQ_STR(d.GetRackName(), TEXT("R-201"));
    CHECK_EQ_STR(d.GetRackId(), TEXT("14"));

    CHECK(d.HasPosition());
    TCHAR position[Models::Device::POSITION_MAX];
    CHECK(d.GetPositionText(position, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(position, TEXT("42"));

    /* status and face are {"value","label"} objects on read. */
    CHECK_EQ_STR(d.GetFace(), TEXT("front"));
    CHECK_EQ_STR(d.GetStatus(), TEXT("active"));
    CHECK_EQ_STR(d.GetStatusLabel(), TEXT("Active"));

    CHECK_EQ_STR(d.GetPrimaryIp(), TEXT("10.24.8.11/24"));
}

TEST_CASE("Device::FromJson handles an unracked device")
{
    Models::Device d;
    CHECK(d.FromJson(kUnrackedDevice));

    CHECK_EQ_STR(d.GetId(), TEXT("412"));
    CHECK_EQ_STR(d.GetAssetTag(), TEXT("HL-000431"));
    CHECK_EQ_STR(d.GetSiteName(), TEXT("HomeLab"));

    /* location and rack are null, so both halves stay empty rather than stale. */
    CHECK_EQ_STR(d.GetLocationName(), TEXT(""));
    CHECK_EQ_STR(d.GetLocationId(), TEXT(""));
    CHECK_EQ_STR(d.GetRackName(), TEXT(""));
    CHECK_EQ_STR(d.GetRackId(), TEXT(""));

    /*
     * A null position is not position zero. Without the separate flag, U0 would
     * appear on screen for every device on a shelf.
     */
    CHECK_FALSE(d.HasPosition());
    TCHAR position[Models::Device::POSITION_MAX];
    CHECK(d.GetPositionText(position, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(position, TEXT(""));

    /* A cleared face is "" where rack and position are null. */
    CHECK_EQ_STR(d.GetFace(), TEXT(""));

    CHECK_EQ_STR(d.GetSerial(), TEXT(""));
    CHECK_EQ_STR(d.GetPrimaryIp(), TEXT(""));
    CHECK_EQ_STR(d.GetStatus(), TEXT("inventory"));
    CHECK_EQ_STR(d.GetStatusLabel(), TEXT("Inventory"));
}

TEST_CASE("Device::FromJson survives a device that is null everywhere")
{
    Models::Device d;

    /* The id alone is enough to act on, so the parse succeeds. */
    CHECK(d.FromJson(kNullHeavyDevice));
    CHECK(d.IsValid());
    CHECK_EQ_STR(d.GetId(), TEXT("7"));

    CHECK_EQ_STR(d.GetName(), TEXT(""));
    CHECK_EQ_STR(d.GetSerial(), TEXT(""));
    CHECK_EQ_STR(d.GetAssetTag(), TEXT(""));
    CHECK_EQ_STR(d.GetDeviceTypeModel(), TEXT(""));
    CHECK_EQ_STR(d.GetManufacturer(), TEXT(""));
    CHECK_EQ_STR(d.GetDeviceRole(), TEXT(""));
    CHECK_EQ_STR(d.GetSiteName(), TEXT(""));
    CHECK_EQ_STR(d.GetFace(), TEXT(""));
    CHECK_EQ_STR(d.GetStatus(), TEXT(""));
    CHECK_FALSE(d.HasPosition());
}

TEST_CASE("Device::FromJson reads NetBox 3.x device_role, a half-U slot and UTF-8")
{
    Models::Device d;
    CHECK(d.FromJson(kLegacyDevice));

    /* `device_role` was renamed `role` in 4.0; one parse path covers both. */
    CHECK_EQ_STR(d.GetDeviceRole(), TEXT("PDU"));

    /* Raw UTF-8 out of the payload, unchanged. */
    CHECK_EQ_STR(d.GetSiteName(), TEXT("K\xc3\xb6ln-S\xc3\xbc\x64"));

    /*
     * position is a DecimalField because racks take half-U slots. Reading it
     * with GetInt would truncate to 42 and report success.
     */
    CHECK(d.HasPosition());
    TCHAR position[Models::Device::POSITION_MAX];
    CHECK(d.GetPositionText(position, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(position, TEXT("42.5"));

    CHECK_EQ_STR(d.GetManufacturer(), TEXT("APC"));
    CHECK_EQ_STR(d.GetAssetTag(), TEXT(""));
    CHECK_EQ_STR(d.GetFace(), TEXT("rear"));
}

TEST_CASE("Device::FromJson prefers role over device_role when both are present")
{
    /* NetBox 3.6 and 3.7 ship both, with `role` authoritative. */
    Models::Device d;
    CHECK(d.FromJson(
        TEXT("{\"id\":5,\"role\":{\"id\":1,\"name\":\"Core Switch\"},")
        TEXT("\"device_role\":{\"id\":1,\"name\":\"Deprecated Copy\"}}")));

    CHECK_EQ_STR(d.GetDeviceRole(), TEXT("Core Switch"));
}

TEST_CASE("Device::FromJson accepts a bare choice string and an integer position")
{
    /*
     * status and face are read back as objects but written as bare strings, so
     * both shapes have to parse -- otherwise a payload echoed from a write path
     * reads as an unknown status.
     */
    Models::Device d;
    CHECK(d.FromJson(
        TEXT("{\"id\":9,\"status\":\"planned\",\"face\":\"front\",\"position\":12}")));

    CHECK_EQ_STR(d.GetStatus(), TEXT("planned"));
    CHECK_EQ_STR(d.GetStatusLabel(), TEXT(""));
    CHECK_EQ_STR(d.GetFace(), TEXT("front"));

    CHECK(d.HasPosition());
    TCHAR position[Models::Device::POSITION_MAX];
    CHECK(d.GetPositionText(position, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(position, TEXT("12"));
}

TEST_CASE("Device::FromJson refuses a device with no id")
{
    Models::Device d;

    /* Displayable but not actionable: every write PATCHes /devices/<id>/. */
    CHECK_FALSE(d.FromJson(TEXT("{\"name\":\"sw-akr-01\",\"serial\":\"ABC\"}")));
    CHECK_FALSE(d.IsValid());

    CHECK_FALSE(d.FromJson(TEXT("not json at all")));
    CHECK_FALSE(d.FromJson(NULL));
}

TEST_CASE("Device::FromJson clears the previous device before reading the next")
{
    Models::Device d;
    CHECK(d.FromJson(kRackedDevice));
    CHECK_EQ_STR(d.GetRackName(), TEXT("R-201"));

    /* A rack-less device must not inherit the last one's rack. */
    CHECK(d.FromJson(kUnrackedDevice));
    CHECK_EQ_STR(d.GetRackName(), TEXT(""));
    CHECK_FALSE(d.HasPosition());
}

TEST_CASE("Device::FormatPosition renders one decimal place and drops a trailing zero")
{
    TCHAR text[Models::Device::POSITION_MAX];

    CHECK(Models::Device::FormatPosition(0.0, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT("0"));

    CHECK(Models::Device::FormatPosition(12.0, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT("12"));

    CHECK(Models::Device::FormatPosition(42.5, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT("42.5"));

    CHECK(Models::Device::FormatPosition(999.9, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT("999.9"));

    CHECK(Models::Device::FormatPosition(-1.5, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT("-1.5"));

    /* Beyond max_digits=4 it is not a rack unit, and the cast would overflow. */
    CHECK_FALSE(Models::Device::FormatPosition(1.0e9, text, Models::Device::POSITION_MAX));
    CHECK_EQ_STR(text, TEXT(""));
}

/* ------------------------------------------------------------------------ */
/* Device -> AssetSummary                                                    */
/* ------------------------------------------------------------------------ */
TEST_CASE("SummarizeDevice projects a racked device onto the detail screen")
{
    Models::Device d;
    CHECK(d.FromJson(kRackedDevice));

    Models::AssetSummary s;
    NbClient::SummarizeDevice(&d, &s);

    CHECK_EQ_STR(s.GetSource(), TEXT("NetBox"));
    CHECK_EQ_STR(s.GetId(), TEXT("231"));
    CHECK_EQ_STR(s.GetTitle(), TEXT("sw-akr-01"));
    CHECK_EQ_STR(s.GetSubtitle(), TEXT("Cisco Catalyst 9300-48P"));

    /* The label, not the machine value: this line is read, not sent. */
    CHECK_EQ_STR(s.GetStatus(), TEXT("Active"));
    CHECK_EQ_STR(s.GetCode(), TEXT("ACME-004821"));

    CHECK_EQ_INT(s.GetFieldCount(), 8);
    CHECK_EQ_STR(s.GetLabel(0), TEXT("Asset Tag"));
    CHECK_EQ_STR(s.GetValue(0), TEXT("ACME-004821"));
    CHECK_EQ_STR(s.FindValue(TEXT("Serial")), TEXT("FDO2447L0XY"));
    CHECK_EQ_STR(s.FindValue(TEXT("Site")), TEXT("DM-Akron"));
    CHECK_EQ_STR(s.FindValue(TEXT("Location")), TEXT("Comms Room 1"));
    CHECK_EQ_STR(s.FindValue(TEXT("Rack")), TEXT("R-201"));

    /* Position and face are one physical fact, so they share one row. */
    CHECK_EQ_STR(s.FindValue(TEXT("Position")), TEXT("U42 front"));

    CHECK_EQ_STR(s.FindValue(TEXT("Role")), TEXT("Access Switch"));
    CHECK_EQ_STR(s.FindValue(TEXT("Primary IP")), TEXT("10.24.8.11/24"));
}

TEST_CASE("SummarizeDevice drops the rows an unracked device has nothing for")
{
    Models::Device d;
    CHECK(d.FromJson(kUnrackedDevice));

    Models::AssetSummary s;
    NbClient::SummarizeDevice(&d, &s);

    /*
     * Asset Tag, Site, Role -- and nothing else. Blank rows for serial,
     * location, rack, position and IP would push what is left off a screen that
     * shows about seven.
     */
    CHECK_EQ_INT(s.GetFieldCount(), 3);
    CHECK_EQ_STR(s.GetLabel(0), TEXT("Asset Tag"));
    CHECK_EQ_STR(s.GetLabel(1), TEXT("Site"));
    CHECK_EQ_STR(s.GetLabel(2), TEXT("Role"));

    CHECK(s.FindValue(TEXT("Rack")) == NULL);
    CHECK(s.FindValue(TEXT("Position")) == NULL);
    CHECK(s.FindValue(TEXT("Serial")) == NULL);

    CHECK_EQ_STR(s.GetStatus(), TEXT("Inventory"));
    CHECK_EQ_STR(s.GetSubtitle(), TEXT("Juniper EX3300-48T"));
}

TEST_CASE("SummarizeDevice names an unnamed device by whatever else identifies it")
{
    /* NetBox allows a null name, which is normal for gear just received. */
    Models::Device d;
    CHECK(d.FromJson(TEXT("{\"id\":3,\"name\":null,\"asset_tag\":\"HL-9\",\"serial\":\"SN9\"}")));

    Models::AssetSummary s;
    NbClient::SummarizeDevice(&d, &s);
    CHECK_EQ_STR(s.GetTitle(), TEXT("HL-9"));

    /* No tag either: fall through to the serial rather than a blank header. */
    CHECK(d.FromJson(TEXT("{\"id\":3,\"serial\":\"SN9\"}")));
    NbClient::SummarizeDevice(&d, &s);
    CHECK_EQ_STR(s.GetTitle(), TEXT("SN9"));
    CHECK_EQ_STR(s.GetCode(), TEXT("SN9"));
}

TEST_CASE("SummarizeDevice falls back to the machine status and half a subtitle")
{
    Models::Device d;
    d.SetId(TEXT("1"));
    d.SetStatus(TEXT("decommissioning"));
    d.SetDeviceTypeModel(TEXT("EX3300-48T"));

    Models::AssetSummary s;
    NbClient::SummarizeDevice(&d, &s);

    /* A status line reading "decommissioning" beats one reading nothing. */
    CHECK_EQ_STR(s.GetStatus(), TEXT("decommissioning"));

    /* No manufacturer, so no leading space in front of the model. */
    CHECK_EQ_STR(s.GetSubtitle(), TEXT("EX3300-48T"));
}

TEST_CASE("SummarizeDevice tolerates null arguments")
{
    Models::AssetSummary s;
    s.AddField(TEXT("Stale"), TEXT("row"));

    /* A null device clears the summary rather than leaving the last asset up. */
    NbClient::SummarizeDevice(NULL, &s);
    CHECK_EQ_INT(s.GetFieldCount(), 0);
    CHECK_EQ_STR(s.GetTitle(), TEXT(""));

    Models::Device d;
    NbClient::SummarizeDevice(&d, NULL); /* must not crash */
}

/* ------------------------------------------------------------------------ */
/* LOCAL CLASSIFICATION                                                      */
/* Answered before any network call, which is the point of it.               */
/* ------------------------------------------------------------------------ */
TEST_CASE("ClassifyCode recognises what a scan cannot be a device")
{
    TCHAR id[Models::Device::ID_MAX];

    CHECK_EQ_INT((int)NbClient::ClassifyCode(NULL, id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_EMPTY);
    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT(""), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_EMPTY);
    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("   "), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_EMPTY);

    /* A GS1-128 carton label: two blocking round trips saved per misfire. */
    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("(00)003123456789012345"), id,
                                             Models::Device::ID_MAX),
                 (int)NbClient::CODE_CARTON);
}

TEST_CASE("ClassifyCode extracts a NetBox primary key from a label token or URL")
{
    TCHAR id[Models::Device::ID_MAX];

    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("NBDEV:412"), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_DEVICE_ID);
    CHECK_EQ_STR(id, TEXT("412"));

    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("https://netbox.lan/dcim/devices/231/"),
                                             id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_DEVICE_ID);
    CHECK_EQ_STR(id, TEXT("231"));
}

TEST_CASE("ClassifyCode falls back to a lookup for anything else")
{
    TCHAR id[Models::Device::ID_MAX];

    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("ACME-004821"), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_OPAQUE);
    CHECK_EQ_STR(id, TEXT(""));

    /* A rack label is a destination, not a device; the move screen owns it. */
    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("NBRACK:12"), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_OPAQUE);

    /* A malformed token looks up normally rather than addressing device 0. */
    CHECK_EQ_INT((int)NbClient::ClassifyCode(TEXT("NBDEV:abc"), id, Models::Device::ID_MAX),
                 (int)NbClient::CODE_OPAQUE);
    CHECK_EQ_STR(id, TEXT(""));
}

/* ------------------------------------------------------------------------ */
/* URL CONSTRUCTION                                                          */
/* ------------------------------------------------------------------------ */
TEST_CASE("BuildLookupPath encodes only the scanned value")
{
    Str::Buffer path;

    /*
     * A Code 128 label can carry any of these. Unencoded, '#' truncates the
     * request at the server, '&' injects a second query parameter, and '+'
     * decodes as a space.
     */
    CHECK(NbClient::BuildLookupPath(TEXT("asset_tag__ie"), TEXT("ACME/48 21+A#1"), &path));
    CHECK_EQ_STR(path.Get(),
                 TEXT("/api/dcim/devices/?format=json&exclude=config_context&limit=5")
                 TEXT("&asset_tag__ie=ACME%2F48%2021%2BA%231"));
}

TEST_CASE("BuildLookupPath always carries the trailing slash and config_context exclusion")
{
    Str::Buffer path;

    CHECK(NbClient::BuildLookupPath(TEXT("q"), TEXT("sw-akr-01"), &path));
    CHECK_EQ_STR(path.Get(),
                 TEXT("/api/dcim/devices/?format=json&exclude=config_context&limit=5")
                 TEXT("&q=sw-akr-01"));

    /* The collection path ends in '/' before the query; NetBox 301s otherwise. */
    CHECK(wcsncmp(path.Get(), TEXT("/api/dcim/devices/?"), 19) == 0);
}

TEST_CASE("BuildLookupPath refuses codes that could never match a device")
{
    Str::Buffer path;

    CHECK_FALSE(NbClient::BuildLookupPath(TEXT("q"), NULL, &path));
    CHECK_FALSE(NbClient::BuildLookupPath(TEXT("q"), TEXT(""), &path));
    CHECK_FALSE(NbClient::BuildLookupPath(NULL, TEXT("ABC"), &path));
    CHECK_FALSE(NbClient::BuildLookupPath(TEXT("q"), TEXT("ABC"), NULL));

    /* NetBox caps name at 64 and serial / asset_tag at 50. */
    TCHAR huge[NbClient::MAX_CODE_CHARS + 8];
    int i = 0;
    for (; i < NbClient::MAX_CODE_CHARS + 4; i++) {
        huge[i] = (TCHAR)'A';
    }
    huge[i] = 0;
    CHECK_FALSE(NbClient::BuildLookupPath(TEXT("q"), huge, &path));
}

TEST_CASE("BuildDevicePath addresses one device with the mandatory trailing slash")
{
    Str::Buffer path;

    CHECK(NbClient::BuildDevicePath(TEXT("231"), &path));
    CHECK_EQ_STR(path.Get(),
                 TEXT("/api/dcim/devices/231/?format=json&exclude=config_context"));

    CHECK_FALSE(NbClient::BuildDevicePath(TEXT(""), &path));
    CHECK_FALSE(NbClient::BuildDevicePath(NULL, &path));
    CHECK_FALSE(NbClient::BuildDevicePath(TEXT("231"), NULL));
}

/* ------------------------------------------------------------------------ */
/* MOVE / STATUS BODIES                                                      */
/* ------------------------------------------------------------------------ */
TEST_CASE("BuildMoveBody sends the whole positional set in one body")
{
    Str::Buffer body;

    CHECK(NbClient::BuildMoveBody(TEXT("3"), TEXT("12"), TEXT("7"), TEXT("42.5"),
                                  TEXT("front"), &body));

    /* Bare integers for the relations, a bare decimal for the position. */
    CHECK_EQ_STR(body.Get(),
                 TEXT("{\"site\":3,\"location\":12,\"rack\":7,\"position\":42.5,\"face\":\"front\"}"));
}

TEST_CASE("BuildMoveBody clears a field with null, and a face with an empty string")
{
    Str::Buffer body;

    /* Unracking: NULL omits, "" clears. */
    CHECK(NbClient::BuildMoveBody(NULL, NULL, TEXT(""), TEXT(""), TEXT(""), &body));
    CHECK_EQ_STR(body.Get(), TEXT("{\"rack\":null,\"position\":null,\"face\":\"\"}"));

    /* One field on its own is a legal partial move. */
    CHECK(NbClient::BuildMoveBody(NULL, NULL, NULL, TEXT("12"), NULL, &body));
    CHECK_EQ_STR(body.Get(), TEXT("{\"position\":12}"));
}

TEST_CASE("BuildMoveBody refuses a rack without the site and location it belongs to")
{
    Str::Buffer body;

    /*
     * NetBox validates the rack against the location and the location against
     * the site, so this would be a 400 -- surfacing from the queue long after
     * the operator left the rack.
     */
    CHECK_FALSE(NbClient::BuildMoveBody(NULL, NULL, TEXT("7"), NULL, NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(TEXT("3"), NULL, TEXT("7"), NULL, NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(NULL, TEXT("12"), TEXT("7"), NULL, NULL, &body));

    /* Clearing either of them counts as supplying it. */
    CHECK(NbClient::BuildMoveBody(TEXT("3"), TEXT(""), TEXT("7"), NULL, NULL, &body));
    CHECK_EQ_STR(body.Get(), TEXT("{\"site\":3,\"location\":null,\"rack\":7}"));
}

TEST_CASE("BuildMoveBody refuses values that are not numbers")
{
    Str::Buffer body;

    /* Relations and position go in unquoted, so a non-number breaks the body. */
    CHECK_FALSE(NbClient::BuildMoveBody(TEXT("R-201"), NULL, NULL, NULL, NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(NULL, NULL, NULL, TEXT("top"), NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(NULL, NULL, NULL, TEXT("1.2.3"), NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(TEXT("3\",\"x\":1"), NULL, NULL, NULL, NULL, &body));

    /* Nothing supplied is not an empty PATCH, it is a caller error. */
    CHECK_FALSE(NbClient::BuildMoveBody(NULL, NULL, NULL, NULL, NULL, &body));
    CHECK_FALSE(NbClient::BuildMoveBody(TEXT("3"), NULL, NULL, NULL, NULL, NULL));
}

/* ------------------------------------------------------------------------ */
/* QUEUE PAYLOADS                                                            */
/* ------------------------------------------------------------------------ */
TEST_CASE("BuildStatusPayload writes the delimited form Replay reads back")
{
    Str::Buffer payload;

    CHECK(NbClient::BuildStatusPayload(TEXT("231"), TEXT("decommissioning"), &payload));
    CHECK_EQ_STR(payload.Get(), TEXT("STATUS:231:decommissioning"));

    CHECK_FALSE(NbClient::BuildStatusPayload(NULL, TEXT("active"), &payload));
    CHECK_FALSE(NbClient::BuildStatusPayload(TEXT(""), TEXT("active"), &payload));
    CHECK_FALSE(NbClient::BuildStatusPayload(TEXT("231"), TEXT(""), &payload));

    /* An id carrying the delimiter could not be parsed back out. */
    CHECK_FALSE(NbClient::BuildStatusPayload(TEXT("2:31"), TEXT("active"), &payload));

    /* Neither half is escaped, and a newline splits the journal record in two. */
    CHECK_FALSE(NbClient::BuildStatusPayload(TEXT("231"), TEXT("act\nive"), &payload));
    CHECK_FALSE(NbClient::BuildStatusPayload(TEXT("23\r1"), TEXT("active"), &payload));
}

TEST_CASE("BuildMovePayload round-trips every field through JSON")
{
    Str::Buffer payload;
    CHECK(NbClient::BuildMovePayload(TEXT("231"), TEXT("3"), TEXT("12"), TEXT("7"),
                                     TEXT("42.5"), TEXT("front"), &payload));
    CHECK_EQ_STR(payload.Get(),
                 TEXT("MOVE:{\"id\":\"231\",\"site\":\"3\",\"location\":\"12\",")
                 TEXT("\"rack\":\"7\",\"position\":\"42.5\",\"face\":\"front\"}"));

    /* The queued document is what the replay parser has to be able to read. */
    Models::JsonLite parser;
    CHECK(parser.Parse(payload.Get() + 5));

    TCHAR value[Models::Device::ID_MAX];
    CHECK(parser.GetString(TEXT("id"), value, Models::Device::ID_MAX));
    CHECK_EQ_STR(value, TEXT("231"));
    CHECK(parser.GetString(TEXT("rack"), value, Models::Device::ID_MAX));
    CHECK_EQ_STR(value, TEXT("7"));
    CHECK(parser.GetString(TEXT("position"), value, Models::Device::ID_MAX));
    CHECK_EQ_STR(value, TEXT("42.5"));
}

TEST_CASE("BuildMovePayload keeps absence and emptiness apart")
{
    Str::Buffer payload;

    /*
     * An unrack. The empty values have to survive: dropping them because they
     * are empty would turn "clear the rack" into "change nothing".
     */
    CHECK(NbClient::BuildMovePayload(TEXT("231"), NULL, NULL, TEXT(""), TEXT(""),
                                     TEXT(""), &payload));
    CHECK_EQ_STR(payload.Get(),
                 TEXT("MOVE:{\"id\":\"231\",\"rack\":\"\",\"position\":\"\",\"face\":\"\"}"));

    Models::JsonLite parser;
    CHECK(parser.Parse(payload.Get() + 5));
    CHECK_FALSE(parser.HasKey(TEXT("site")));
    CHECK_FALSE(parser.HasKey(TEXT("location")));
    CHECK(parser.HasKey(TEXT("rack")));
}

TEST_CASE("BuildMovePayload refuses at the rack what replay could never apply")
{
    Str::Buffer payload;

    /* Validated with the same code that builds the PATCH hours later. */
    CHECK_FALSE(NbClient::BuildMovePayload(TEXT("231"), NULL, NULL, TEXT("7"), NULL,
                                           NULL, &payload));
    CHECK_FALSE(NbClient::BuildMovePayload(TEXT("231"), TEXT("R-201"), NULL, NULL, NULL,
                                           NULL, &payload));
    CHECK_FALSE(NbClient::BuildMovePayload(TEXT("231"), NULL, NULL, NULL, NULL, NULL,
                                           &payload));
    CHECK_FALSE(NbClient::BuildMovePayload(NULL, TEXT("3"), NULL, NULL, NULL, NULL,
                                           &payload));
}

/* ------------------------------------------------------------------------ */
/* QUEUE REPLAY                                                              */
/* Replay receives the type with its backend prefix already stripped. There   */
/* is no server here, so a well-formed payload gets as far as the transport   */
/* and comes back RETRY -- which is the same outcome as being offline, and    */
/* deliberately never SKIPPED.                                               */
/* ------------------------------------------------------------------------ */
TEST_CASE("Replay skips transaction types this backend does not own")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));

    /* A HomeBox entry is not a NetBox failure; counting it as one would pin
     * the sync status at "failed" for as long as it sits in the queue. */
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCAN:1")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"), TEXT("UPDATE:{}")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(TEXT(""), TEXT("")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(NULL, TEXT("STATUS:1:active")), (int)REPLAY_SKIPPED);

    /* The type is matched exactly; a prefix that was not stripped is not ours. */
    CHECK_EQ_INT((int)c.Replay(TEXT("nb.DEVICE_MOVE"), TEXT("MOVE:{}")), (int)REPLAY_SKIPPED);
}

TEST_CASE("Replay leaves an unsendable status change queued")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));
    c.SetAuth(TEXT("Token"), TEXT("0123456789abcdef"));

    Str::Buffer payload;
    CHECK(NbClient::BuildStatusPayload(TEXT("231"), TEXT("decommissioning"), &payload));

    /* Well formed, but there is no session, so nothing reached the server. */
    CHECK_EQ_INT((int)c.Replay(NbClient::GetStatusTransactionType(), payload.Get()),
                 (int)REPLAY_RETRY);
}

TEST_CASE("Replay rejects malformed status payloads without dropping them")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));

    /*
     * A malformed payload will never succeed, but it stays RETRY rather than
     * SKIPPED: the entry remains visible in the queue view where the operator
     * can inspect and remove it, instead of vanishing with their work in it.
     */
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("NONSENSE")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), NULL), (int)REPLAY_RETRY);

    /* No separator, so no status value. */
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("STATUS:231")), (int)REPLAY_RETRY);
    /* Empty halves. */
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("STATUS::active")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("STATUS:231:")), (int)REPLAY_RETRY);
}

TEST_CASE("Replay reads back the move payload the queue writer produced")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));
    c.SetAuth(TEXT("Token"), TEXT("0123456789abcdef"));

    Str::Buffer payload;
    CHECK(NbClient::BuildMovePayload(TEXT("231"), TEXT("3"), TEXT("12"), TEXT("7"),
                                     TEXT("42.5"), TEXT("front"), &payload));

    /* Parsed, rebuilt into a PATCH, and only then defeated by having no session. */
    CHECK_EQ_INT((int)c.Replay(NbClient::GetMoveTransactionType(), payload.Get()),
                 (int)REPLAY_RETRY);

    /* An unrack survives the same trip. */
    CHECK(NbClient::BuildMovePayload(TEXT("231"), NULL, NULL, TEXT(""), TEXT(""),
                                     TEXT(""), &payload));
    CHECK_EQ_INT((int)c.Replay(NbClient::GetMoveTransactionType(), payload.Get()),
                 (int)REPLAY_RETRY);
}

TEST_CASE("Replay rejects malformed move payloads without dropping them")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));

    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("NONSENSE")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("MOVE:")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("MOVE:{oops")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), NULL), (int)REPLAY_RETRY);

    /* No id: nothing to address. */
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("MOVE:{\"rack\":\"7\"}")),
                 (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("MOVE:{\"id\":\"\"}")),
                 (int)REPLAY_RETRY);

    /*
     * A field this parser cannot read is refused rather than guessed at. A
     * relation carried as an object would otherwise fall through to the null
     * case and unrack the device -- the opposite of what the entry says.
     */
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"),
                               TEXT("MOVE:{\"id\":\"231\",\"rack\":{\"id\":7}}")),
                 (int)REPLAY_RETRY);
}

/* ------------------------------------------------------------------------ */
/* IDENTITY AND CAPABILITIES                                                 */
/* ------------------------------------------------------------------------ */
TEST_CASE("NbClient reports a stable kind and display name")
{
    NbClient c;
    CHECK_EQ_STR(c.GetKind(), TEXT("nb"));
    CHECK_EQ_STR(c.GetDisplayName(), TEXT("NetBox"));
}

TEST_CASE("NbClient instance id defaults to nb and round-trips")
{
    NbClient c;
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("nb"));

    c.SetInstanceId(TEXT("nb-prod"));
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("nb-prod"));

    /* An empty tag would make every record this client queues unroutable. */
    c.SetInstanceId(TEXT(""));
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("nb"));
    c.SetInstanceId(TEXT("nb-lab"));
    c.SetInstanceId(NULL);
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("nb"));

    TCHAR longId[80];
    for (int i = 0; i < 79; i++) {
        longId[i] = (TCHAR)'n';
    }
    longId[79] = 0;
    c.SetInstanceId(longId);
    CHECK_EQ_INT(Str::Length(c.GetInstanceId()), NbClient::INSTANCE_ID_MAX - 1);
}

TEST_CASE("NbClient reports a status concept and a session that cannot be renewed")
{
    NbClient c;

    CHECK(c.SupportsStatus());
    CHECK_EQ_INT(c.GetStatusChoiceCount(), 7);
    CHECK_EQ_STR(c.GetStatusChoice(0), TEXT("inventory"));
    CHECK_EQ_STR(c.GetStatusChoice(6), TEXT("decommissioning"));
    CHECK(c.GetStatusChoice(7) == NULL);
    CHECK(c.GetStatusChoice(-1) == NULL);

    /* A NetBox API token is static configuration: a 401 will not improve. */
    CHECK_FALSE(c.SessionIsRenewable());
}

TEST_CASE("NbClient carries both NetBox token generations")
{
    NbClient c;

    /* v1 by default: "Token <40 hex chars>". */
    CHECK_EQ_STR(c.GetAuthScheme(), TEXT("Token"));

    /* v2, from NetBox 4.5: "Bearer nbt_<key>.<secret>". */
    c.SetAuth(TEXT("Bearer"), TEXT("nbt_4F9DAouzURLb.zjebxBPzICiPbWz0Wtx0"));
    CHECK_EQ_STR(c.GetAuthScheme(), TEXT("Bearer"));

    /* An empty scheme is a configuration slip, not a request for no scheme. */
    c.SetAuth(TEXT(""), TEXT("abc"));
    CHECK_EQ_STR(c.GetAuthScheme(), TEXT("Token"));
}

TEST_CASE("NbClient trims a trailing slash off the base URL")
{
    NbClient c;

    /*
     * Every path this client builds starts with '/', so a base URL that ends
     * with one would produce "//api/dcim/devices/" -- a 404 that reads on
     * screen exactly like a missing device.
     */
    c.SetBaseUrl(TEXT("http://netbox.lan/"));
    CHECK_EQ_STR(c.GetBaseUrl(), TEXT("http://netbox.lan"));

    c.SetBaseUrl(TEXT("http://netbox.lan:8000///"));
    CHECK_EQ_STR(c.GetBaseUrl(), TEXT("http://netbox.lan:8000"));

    c.SetBaseUrl(TEXT("http://netbox.lan"));
    CHECK_EQ_STR(c.GetBaseUrl(), TEXT("http://netbox.lan"));

    c.SetBaseUrl(NULL);
    CHECK(c.GetBaseUrl() == NULL);
}

TEST_CASE("NbClient will not authenticate or write without a token")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));

    /* Nothing to present; this is a configuration problem, not a network one. */
    CHECK_FALSE(c.Authenticate());
    CHECK_FALSE(c.IsAuthenticated());
    CHECK_EQ_STR(c.GetServerVersion(), TEXT(""));

    /* An empty token would send a bare "Token " and collect 401s all shift. */
    c.SetAuth(TEXT("Token"), TEXT(""));
    CHECK_FALSE(c.Authenticate());

    CHECK_FALSE(c.SetDeviceStatus(TEXT("231"), TEXT("active")));
    CHECK_FALSE(c.MoveDevice(TEXT("231"), TEXT("3"), TEXT("12"), TEXT("7"),
                             TEXT("42"), TEXT("front")));
}

TEST_CASE("NbClient has no matches until a lookup succeeds")
{
    NbClient c;
    c.SetBaseUrl(TEXT("http://netbox.lan"));

    Models::AssetSummary summary;
    summary.SetTitle(TEXT("untouched"));

    CHECK_EQ_INT(c.GetMatchCount(), 0);
    CHECK_EQ_INT(c.GetTotalMatchCount(), 0);
    CHECK_FALSE(c.GetMatch(0, &summary));
    CHECK_FALSE(c.GetMatch(-1, &summary));
    CHECK(c.GetMatchedDevice(0) == NULL);

    /* No session, so the lookup fails without disturbing the caller's summary. */
    CHECK_FALSE(c.LookupByCode(TEXT("ACME-004821"), &summary));
    CHECK_EQ_INT(c.GetMatchCount(), 0);
    CHECK_EQ_STR(summary.GetTitle(), TEXT("untouched"));

    CHECK_FALSE(c.LookupByCode(NULL, &summary));
    CHECK_FALSE(c.LookupByCode(TEXT("ACME-004821"), NULL));
}

TEST_CASE("NbClient is fully usable through an InventoryBackend pointer")
{
    InventoryBackend* b = new NbClient();

    b->SetBaseUrl(TEXT("http://netbox.lan"));
    CHECK_EQ_STR(b->GetBaseUrl(), TEXT("http://netbox.lan"));
    b->SetRequestTimeout(5000);

    CHECK_EQ_STR(b->GetKind(), TEXT("nb"));
    CHECK_EQ_STR(b->GetDisplayName(), TEXT("NetBox"));
    CHECK_EQ_STR(b->GetInstanceId(), TEXT("nb"));
    CHECK_FALSE(b->IsAuthenticated());
    CHECK_EQ_INT(b->GetLastStatusCode(), 0);
    CHECK_FALSE(b->SessionIsRenewable());
    CHECK(b->SupportsStatus());
    CHECK_EQ_INT(b->GetStatusChoiceCount(), 7);

    /* No token has been configured, so there is nothing to verify. */
    CHECK_FALSE(b->Authenticate());

    /* Virtual destructor: deleting through the base must run ~NbClient. */
    delete b;
}
