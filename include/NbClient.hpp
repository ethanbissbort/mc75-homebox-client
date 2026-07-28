#ifndef NBCLIENT_HPP
#define NBCLIENT_HPP

#include <windows.h>
#include "HttpClient.hpp"
#include "InventoryBackend.hpp"
#include "StrUtil.hpp"
#include "Models/AssetSummary.hpp"
#include "Models/Device.hpp"

namespace HBX {

/**
 * NetBox DCIM client.
 *
 * The second InventoryBackend implementation. It covers exactly the three
 * things a handheld can usefully do against NetBox -- find a device from a
 * scanned label, change its status, and move it -- and deliberately does not
 * cover creating one: a NetBox device requires four mandatory foreign keys
 * (device_type, role, site, status), which is a desk job with the web UI open,
 * not a job for a 240x320 screen and a numeric keypad.
 *
 * SCOPE OF THE WRITE PATH
 * Every write is a PATCH of the two or three keys that changed. A PUT would be
 * a full object representation, so every field the handheld never loaded --
 * tenant, platform, comments, custom fields, config template -- would be sent
 * as absent and blanked.
 *
 * BASE URL
 * m_baseUrl is the NetBox server root, e.g. "http://netbox.lan", with no
 * "/api" suffix; this client appends the full "/api/dcim/..." path itself. A
 * trailing '/' is tolerated and stripped, because NetBox answers 301 for a URL
 * whose slashes are wrong and this transport does not follow redirects.
 *
 * TRANSPORT NOTE
 * This device offers SSL 3.0 / TLS 1.0 with RC4 or 3DES and carries a root
 * store from around 2009, which no current NetBox reverse proxy will negotiate.
 * The supported deployment is plain HTTP on a segmented LAN. SetIgnoreCertificateErrors
 * exists for the narrow case of a private certificate on a trusted segment and
 * is off unless the operator opts in.
 *
 * QUEUE TRANSACTIONS
 * Offline work is queued under two types, which SyncEngine tags with this
 * client's instance id ("nb-prod.DEVICE_MOVE"):
 *
 *   DEVICE_STATUS   "STATUS:<deviceId>:<statusValue>"
 *   DEVICE_MOVE     "MOVE:<json>", the JSON being
 *                   {"id":"123","site":"3","location":"12","rack":"7",
 *                    "position":"42.5","face":"front"} with absent keys omitted
 *
 * A move is JSON rather than a delimited string because it carries six optional
 * fields, and it is one atomic PATCH because NetBox enforces consistency across
 * site / location / rack / position / face. A move split into parts is rejected
 * with a 400 that, replayed from a queue hours after the operator left the
 * rack, nobody can act on.
 */
class NbClient : public InventoryBackend {
public:
    enum {
        /** Cap on the operator-assigned instance id; see SetInstanceId. */
        INSTANCE_ID_MAX = 32,

        /** "Token" or "Bearer"; see SetAuth. */
        AUTH_SCHEME_MAX = 16,

        /** NetBox reports "4.6.0"-shaped versions from /api/status/. */
        VERSION_MAX = 24,

        /**
         * Devices retained from one lookup. NetBox does not enforce uniqueness
         * on a serial, so a scan can legitimately match several devices and the
         * operator has to pick. Five is what the disambiguation list shows
         * without scrolling on a 240x320 screen; asking the server for more
         * only spends radio time on rows nobody will reach.
         */
        MAX_MATCHES = 5,

        /**
         * Longest scanned code worth sending. NetBox caps name at 64 and both
         * serial and asset_tag at 50, so a longer code cannot match any device
         * -- rejecting it locally saves two round trips on a link where each
         * one freezes the screen.
         */
        MAX_CODE_CHARS = 128,

        /** Percent-encoded form of the above, with room for non-ASCII. */
        MAX_ENCODED_CHARS = 512
    };

    /**
     * What a scanned label turned out to be, decided with no network traffic.
     * Classifying first is what keeps the common case to one round trip and
     * makes the hopeless cases cost none at all.
     */
    enum CodeKind {
        CODE_EMPTY,      // nothing usable
        CODE_CARTON,     // a GS1-128 shipping label; identifies a carton, not a device
        CODE_DEVICE_ID,  // a NetBox primary key, from an "NBDEV:<n>" token or a device URL
        CODE_OPAQUE      // an asset tag, serial, or name -- has to be looked up
    };

    NbClient();
    virtual ~NbClient();

    // ---- Backend identity ------------------------------------------------

    /** Always TEXT("nb"). */
    virtual const TCHAR* GetKind() const;

    /** Always TEXT("NetBox"). */
    virtual const TCHAR* GetDisplayName() const;

    /** Operator-assigned instance id; TEXT("nb") until SetInstanceId says otherwise. */
    virtual const TCHAR* GetInstanceId() const;

    /**
     * Sets the id queue records are tagged with. A NULL or empty id restores
     * the default TEXT("nb"). The instance matters, not just the kind: asset
     * tags are unique per NetBox instance rather than globally, so a queued
     * move replayed against a different instance would silently move whatever
     * device happens to hold that tag there -- a corruption that looks like a
     * success. Ids longer than INSTANCE_ID_MAX are truncated, which is
     * self-consistent because the same value tags a record and routes it back.
     */
    void SetInstanceId(const TCHAR* instanceId);

    // ---- Configuration ---------------------------------------------------

    virtual void SetBaseUrl(const TCHAR* baseUrl);
    virtual const TCHAR* GetBaseUrl() const;
    virtual void SetRequestTimeout(DWORD timeoutMs);

    /** See HttpClient::SetIgnoreCertificateErrors. Off by default. */
    void SetIgnoreCertificateErrors(bool ignore);

    /**
     * Records the Authorization header this client sends, as
     * "<scheme> <token>".
     *
     * Keeping the scheme configurable rather than hard-coding "Token " is what
     * makes both NetBox token generations work from the same build: v1 tokens
     * are "Token <40 hex chars>", and the v2 tokens introduced in NetBox 4.5
     * are "Bearer nbt_<key>.<secret>". v1 is deprecated in 4.6 and removed in
     * 5.0, so that migration is a line in hb_conf.json rather than a rebuild
     * of an application whose toolchain no longer exists.
     *
     * A NULL or empty scheme falls back to "Token". A NULL or empty token ends
     * the session, because a client that believes in an empty token sends a
     * bare "Token " and collects a 401 on every request forever.
     */
    void SetAuth(const TCHAR* scheme, const TCHAR* token);

    /** The configured scheme, e.g. TEXT("Token"). Never NULL. */
    const TCHAR* GetAuthScheme() const;

    // ---- Session ---------------------------------------------------------

    /**
     * Verifies the configured token against /api/status/ and records the NetBox
     * version the server reports.
     *
     * There is nothing to exchange -- a NetBox API token is static
     * configuration -- so this is a probe, not a handshake. It is still worth
     * one cheap round trip at startup: it is the difference between telling the
     * operator "the token is wrong" once and letting every scan fail with an
     * unexplained miss.
     */
    virtual bool Authenticate();

    virtual bool IsAuthenticated() const;

    /**
     * Always false. A NetBox API token is provisioned by an administrator and
     * never rotates on its own, so a 401 means it is wrong or has been revoked.
     * Re-authenticating and retrying would burn an extra round trip per scan
     * and end in the same 401.
     */
    virtual bool SessionIsRenewable() const;

    /**
     * HTTP status of the most recent request that reached the server, or 0 when
     * the last one never got a reply.
     *
     * A 401 additionally drops the session: DRF returns it when a token is
     * present but rejected. A 403 does not, because NetBox uses 403 both for
     * "no credentials" and for "this token may not do that" -- a read-only
     * token or an allowed_ips restriction produces 403 on writes while reads
     * keep working, and dropping the session there would break the reads too.
     */
    virtual int GetLastStatusCode() const;

    /** NetBox version reported by the last Authenticate(), or an empty string. */
    const TCHAR* GetServerVersion() const;

    // ---- Lookup ----------------------------------------------------------

    /**
     * Resolves a scanned label to a device.
     *
     * Resolution order, chosen to keep the common case to a single round trip
     * because every call blocks the UI thread on the radio link:
     *
     *   1. Local classification. A carton label or an empty code is refused
     *      with no traffic at all; an "NBDEV:<n>" token or a device URL goes
     *      straight to the detail endpoint.
     *   2. ?asset_tag__ie=<code> -- an exact, case-insensitive match on the tag
     *      the site printed. The `__ie` suffix is mandatory: NetBox declares
     *      `serial` with iexact but leaves `asset_tag` on the default
     *      case-sensitive `exact`, so "?asset_tag=acme-4821" does not match
     *      "ACME-4821" and a decoder that differs in case silently finds
     *      nothing.
     *   3. ?q=<code> -- one request covering name, serial and asset_tag. Used
     *      as a cheap OR of three exact lookups: `q` is a substring search, so
     *      the results are filtered locally down to those that match the
     *      scanned code exactly (case-insensitively) before any of them counts.
     *      Without that filter, scanning "01" would resolve to "sw-core-01".
     *
     * `out` is written only when exactly one device matched. More than one is
     * not an error and must not be auto-resolved -- inspect GetMatchCount and
     * GetMatch to build the disambiguation list.
     */
    virtual bool LookupByCode(const TCHAR* code, Models::AssetSummary* out);

    /** Devices retained by the last lookup, 0..MAX_MATCHES. */
    virtual int GetMatchCount() const;

    virtual bool GetMatch(int index, Models::AssetSummary* out);

    /**
     * Devices the server said matched, which can exceed GetMatchCount() when
     * more turned up than MAX_MATCHES. Kept separate rather than folded into
     * GetMatchCount so that GetMatch stays addressable over its whole range,
     * while the UI can still say "5 of 12" instead of pretending there were
     * five.
     */
    int GetTotalMatchCount() const;

    /** The parsed device behind match `index`, or NULL. Valid until the next lookup. */
    const Models::Device* GetMatchedDevice(int index) const;

    // ---- Capabilities ----------------------------------------------------

    /** True: a NetBox device has a status field the operator can change. */
    virtual bool SupportsStatus() const;

    /**
     * The seven values DeviceStatusChoices ships, in the order a receiving
     * workflow walks them.
     *
     * NetBox lets a deployment extend this set through the FIELD_CHOICES
     * configuration parameter, and this client has no way to see those without
     * an OPTIONS request the transport does not implement. So the list is the
     * picker's contents, not a validator: SetDeviceStatus deliberately does not
     * check against it, and a site-defined value obtained some other way is
     * still sent through.
     */
    virtual int GetStatusChoiceCount() const;
    virtual const TCHAR* GetStatusChoice(int index) const;

    // ---- Writes ----------------------------------------------------------

    /**
     * PATCHes {"status":"<statusValue>"}.
     *
     * The bare string is not a mistake: `status` is read back as a
     * {"value","label"} object but written as the value alone, and sending the
     * object shape you just received fails validation.
     */
    bool SetDeviceStatus(const TCHAR* deviceId, const TCHAR* statusValue);

    /**
     * Moves a device in one atomic PATCH carrying the complete positional set.
     *
     * NetBox validates site / location / rack / position / face against each
     * other -- a rack must belong to the location, which must belong to the
     * site -- so a move sent as separate requests is rejected part way through
     * with a 400 and leaves the record half moved.
     *
     * Each argument has three states:
     *   NULL  -- omit the key; the server leaves that field as it is.
     *   ""    -- clear the field. Foreign keys and position are sent as JSON
     *            null; `face` is sent as an empty string, because its
     *            serializer is allow_blank with a '' default and rejects null.
     *            Unracking a device is therefore rack="" and position="".
     *   value -- set it. Ids are sent as bare integers and position as a bare
     *            number, which is the form every NetBox version accepts.
     *
     * Supplying a rack requires supplying site and location as well (either may
     * be "" to clear it): a rack id that contradicts the device's current site
     * is exactly the 400 the atomic form exists to avoid.
     *
     * Returns false without sending anything when nothing was supplied, when
     * an id or position is not a number, or when the rack rule is broken.
     */
    bool MoveDevice(const TCHAR* deviceId, const TCHAR* siteId, const TCHAR* locationId,
                    const TCHAR* rackId, const TCHAR* position, const TCHAR* face);

    // ---- Queue replay ----------------------------------------------------

    /**
     * Replays one queued NetBox transaction. `type` has already had the backend
     * prefix stripped, so it is the bare "DEVICE_STATUS" or "DEVICE_MOVE".
     *
     * REPLAY_SENT only when the server accepted the write. Anything else --
     * offline, timeout, a rejection, or a payload that will not parse --
     * returns REPLAY_RETRY, which leaves the entry queued. A malformed payload
     * will never succeed, but discarding an operator's queued work silently is
     * worse than leaving it visible in the queue view where it can be inspected
     * and removed on purpose. REPLAY_SKIPPED is reserved for a transaction type
     * this backend does not implement at all.
     */
    virtual ReplayResult Replay(const TCHAR* type, const TCHAR* data);

    /** Queue transaction type for a status change; pairs with BuildStatusPayload. */
    static const TCHAR* GetStatusTransactionType();

    /** Queue transaction type for a move; pairs with BuildMovePayload. */
    static const TCHAR* GetMoveTransactionType();

    // ---- Pure helpers ----------------------------------------------------
    //
    // Public and static because they depend on nothing but their arguments.
    // The interesting NetBox behaviour lives here -- percent-encoding a
    // scanned label into a query, the omit / null / value distinction in a move
    // body, and the projection onto the detail screen -- and this is what lets
    // the host tests cover it with no server anywhere in sight.

    /**
     * Projects a device onto the backend-neutral summary the detail screen
     * renders. Rows whose value is empty are skipped: an unracked device would
     * otherwise spend three of seven visible rows on blanks.
     */
    static void SummarizeDevice(const Models::Device* device, Models::AssetSummary* out);

    /**
     * Decides what a scanned label is without touching the network. When the
     * answer is CODE_DEVICE_ID, `deviceIdOut` receives the primary key.
     */
    static CodeKind ClassifyCode(const TCHAR* code, TCHAR* deviceIdOut, int deviceIdCap);

    /**
     * Builds "/api/dcim/devices/?...&<filterKey>=<encoded code>".
     *
     * The path structure is literal and only the value is percent-encoded: a
     * scanned label routinely contains '/', '+', '#' and spaces, and
     * Str::UrlEncode escapes '/' as %2F, so it is safe for exactly one query
     * value or one path segment and never for a whole URL.
     */
    static bool BuildLookupPath(const TCHAR* filterKey, const TCHAR* code, Str::Buffer* out);

    /** Builds "/api/dcim/devices/<id>/?...", trailing slash included. */
    static bool BuildDevicePath(const TCHAR* deviceId, Str::Buffer* out);

    /** Builds the PATCH body for a move; see MoveDevice for the argument states. */
    static bool BuildMoveBody(const TCHAR* siteId, const TCHAR* locationId,
                              const TCHAR* rackId, const TCHAR* position,
                              const TCHAR* face, Str::Buffer* out);

    /** Builds the queue payload "STATUS:<deviceId>:<statusValue>". */
    static bool BuildStatusPayload(const TCHAR* deviceId, const TCHAR* statusValue,
                                   Str::Buffer* out);

    /** Builds the queue payload "MOVE:<json>"; absent (NULL) fields are omitted. */
    static bool BuildMovePayload(const TCHAR* deviceId, const TCHAR* siteId,
                                 const TCHAR* locationId, const TCHAR* rackId,
                                 const TCHAR* position, const TCHAR* face,
                                 Str::Buffer* out);

private:
    HttpClient* m_httpClient;
    TCHAR* m_baseUrl;
    TCHAR* m_token;

    TCHAR m_instanceId[INSTANCE_ID_MAX];
    TCHAR m_authScheme[AUTH_SCHEME_MAX];
    TCHAR m_serverVersion[VERSION_MAX];

    bool m_authenticated;
    int m_lastStatusCode;

    /**
     * Devices from the last lookup, kept as Devices rather than summaries: a
     * Device is half the size of an AssetSummary, and the disambiguation list
     * needs the rack and site fields the summary flattens away. GetMatch
     * projects on demand.
     */
    Models::Device m_matches[MAX_MATCHES];
    int m_matchCount;
    int m_totalMatchCount;

    /**
     * Issues one authenticated call against m_baseUrl + path.
     *
     * On success *response receives the complete body as a heap-allocated,
     * NUL-terminated string the caller must delete[]; on failure it is NULL.
     * Pass NULL to discard the body.
     */
    bool MakeApiRequest(const TCHAR* method, const TCHAR* path, const TCHAR* body,
                        TCHAR** response);

    void SetAuthHeaders();

    /** Sends one lookup and stores the results. False on transport failure. */
    bool FetchDeviceList(const TCHAR* filterKey, const TCHAR* code, bool requireExactMatch);

    /** Fetches one device by primary key from the detail endpoint. */
    bool FetchDeviceById(const TCHAR* deviceId);

    ReplayResult ReplayStatus(const TCHAR* data);
    ReplayResult ReplayMove(const TCHAR* data);

    // Not copyable: the instance owns the HTTP client and its strings.
    NbClient(const NbClient&);
    NbClient& operator=(const NbClient&);
};

} // namespace HBX

#endif // NBCLIENT_HPP
