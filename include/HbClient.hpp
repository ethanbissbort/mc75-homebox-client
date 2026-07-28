#ifndef HBCLIENT_HPP
#define HBCLIENT_HPP

#include <windows.h>
#include "HttpClient.hpp"
#include "InventoryBackend.hpp"
#include "Models/AssetSummary.hpp"
#include "Models/Item.hpp"
#include "Models/Location.hpp"

namespace HBX {

/**
 * HomeBox API client
 * High-level interface for communicating with HomeBox backend
 *
 * This is one of the two InventoryBackend implementations. Everything specific
 * to HomeBox stays here -- the /api/v1 endpoint shapes, the device-credential
 * exchange, and the bearer token it hands back -- so the controller and the
 * sync engine can drive it without knowing which inventory system is active.
 *
 * The typed operations below (GetItem, UpdateItem, GetLocation, ...) are
 * deliberately *not* on the interface: they speak Models::Item and
 * Models::Location, which are HomeBox records with no NetBox counterpart. The
 * interface carries only what both systems can do, and LookupByCode projects an
 * Item into the backend-neutral Models::AssetSummary for the UI.
 */
class HbClient : public InventoryBackend {
public:
    enum {
        /**
         * Cap on the operator-assigned instance id. Ids are short by
         * convention ("hb", "hb-prod") and are read on every queue write, so
         * they live in the object rather than on the heap.
         */
        INSTANCE_ID_MAX = 32
    };

    HbClient();
    virtual ~HbClient();

    // ---- Backend identity ------------------------------------------------

    /** Always TEXT("hb"). */
    virtual const TCHAR* GetKind() const;

    /** Always TEXT("HomeBox"). */
    virtual const TCHAR* GetDisplayName() const;

    /** Operator-assigned instance id; TEXT("hb") until SetInstanceId says otherwise. */
    virtual const TCHAR* GetInstanceId() const;

    /**
     * Sets the id queue records are tagged with. A NULL or empty id restores
     * the default TEXT("hb"), which is also what an untagged record written by
     * an older build resolves to -- so a device that upgrades mid-shift still
     * drains the queue it already had. Ids longer than INSTANCE_ID_MAX are
     * truncated; that is self-consistent, because the same value is used both
     * when a transaction is queued and when it is routed back here.
     */
    void SetInstanceId(const TCHAR* instanceId);

    // ---- Authentication --------------------------------------------------

    /**
     * Records the credentials used by Authenticate(). Copies are taken, so the
     * caller keeps ownership of both strings. Returns false if either is NULL
     * or a copy could not be allocated, in which case the previously recorded
     * credentials are left intact.
     */
    bool SetCredentials(const TCHAR* deviceId, const TCHAR* apiKey);

    /** Records `deviceId`/`apiKey` and authenticates with them in one step. */
    bool Authenticate(const TCHAR* deviceId, const TCHAR* apiKey);

    /**
     * Exchanges the recorded credentials for a session token. Returns false
     * when none have been set -- notably after SetAuthToken() restored a
     * session from storage, which supplies a device id but no API key.
     */
    virtual bool Authenticate();

    virtual bool IsAuthenticated() const;

    /**
     * HomeBox issues a short-lived token in exchange for the device
     * credentials, so a rejected session is worth exactly one re-authenticate
     * and retry. Always true.
     */
    virtual bool SessionIsRenewable() const;

    /** Ends the session. The recorded credentials survive, so Authenticate() can renew. */
    void Logout();

    /**
     * Bearer token of the current session, or NULL when there is none. The
     * client keeps ownership; the pointer stays valid until the next call that
     * changes the session (Authenticate, SetAuthToken, Logout, or a 401).
     */
    const TCHAR* GetAuthToken() const;

    /**
     * Adopts a token issued earlier - typically one the caller persisted in
     * hb_conf.json - and marks the client authenticated without a round trip.
     * A battery swap ends the process several times a shift, and the restart
     * that lands out of coverage cannot authenticate at all, so the session has
     * to be restorable from storage. `deviceId` may be NULL to keep the one
     * already known; a NULL or empty `token` is a Logout(), since a client that
     * believes in an empty token sends "Bearer " and gets 401 forever.
     */
    void SetAuthToken(const TCHAR* deviceId, const TCHAR* token);

    /**
     * HTTP status of the most recent request that reached the server, or 0 when
     * the last one never got a reply (no route, DNS failure, timeout). This is
     * the only way a caller can tell a rejected session from a missing record:
     * both otherwise surface as a bare false. A 401 additionally drops the
     * session, so IsAuthenticated() reports false and the caller can decide to
     * re-authenticate and retry.
     */
    virtual int GetLastStatusCode() const;

    // ---- Backend operations ----------------------------------------------

    /**
     * Resolves a scanned barcode and projects the item into `out`. `out` is
     * written only on success.
     */
    virtual bool LookupByCode(const TCHAR* code, Models::AssetSummary* out);

    /**
     * 0 or 1, never more: HomeBox addresses an item by its barcode as a path
     * segment, so a lookup either resolves exactly one record or 404s. The
     * count exists for backends whose lookup is a query and can return several.
     */
    virtual int GetMatchCount() const;
    virtual bool GetMatch(int index, Models::AssetSummary* out);

    /** False: a HomeBox item has no status field, so the UI hides that action. */
    virtual bool SupportsStatus() const;

    /** 0 and NULL respectively; see SupportsStatus(). */
    virtual int GetStatusChoiceCount() const;
    virtual const TCHAR* GetStatusChoice(int index) const;

    /**
     * Replays one queued HomeBox transaction. `type` has already had the
     * backend prefix stripped, so it is the bare "ITEM_SCAN" or "ITEM_UPDATE";
     * `data` is the payload built when the transaction was queued.
     *
     * Only a transaction the server accepted returns REPLAY_SENT. Anything else
     * -- offline, timeout, a rejection, or a payload that will not parse --
     * returns REPLAY_RETRY, which leaves the entry queued. A malformed payload
     * will never succeed, but silently discarding an operator's queued work is
     * worse than leaving it visible in the queue view, where it can be inspected
     * and removed by hand. REPLAY_SKIPPED is reserved for a type this backend
     * does not implement at all.
     */
    virtual ReplayResult Replay(const TCHAR* type, const TCHAR* data);

    /**
     * Fills `out` with the backend-neutral view of `item`. Static because the
     * projection depends on nothing but the item: LookupByCode is this call
     * plus an HTTP round trip, which is what makes the mapping testable without
     * a server.
     */
    static void SummarizeItem(const Models::Item* item, Models::AssetSummary* out);

    // ---- HomeBox-specific operations --------------------------------------

    // Item operations
    bool GetItem(const TCHAR* barcode, Models::Item* item);
    bool UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId);
    bool CreateItem(const Models::Item* item);
    bool UpdateItem(const Models::Item* item);

    // Location operations
    bool GetLocation(const TCHAR* locationId, Models::Location* location);
    bool GetAllLocations(Models::Location** locations, int* count);

    // Sync operations
    bool SyncPendingTransactions();
    // Post a batch of queued transaction strings to the sync endpoint.
    bool SyncPendingTransactions(const TCHAR* const* transactions, int count);

    // ---- Configuration ----------------------------------------------------

    virtual void SetBaseUrl(const TCHAR* baseUrl);
    virtual const TCHAR* GetBaseUrl() const;

    /**
     * Network timeout applied to every subsequent request. The HTTP transport
     * defaults to 30 seconds, which is a frozen screen for the whole of it when
     * a scan lookup runs on the UI thread, so the owner is expected to pick a
     * value suited to the link it is on.
     */
    virtual void SetRequestTimeout(DWORD timeoutMs);

private:
    HttpClient* m_httpClient;
    TCHAR* m_baseUrl;
    TCHAR* m_authToken;
    TCHAR* m_deviceId;
    TCHAR* m_apiKey;
    bool m_authenticated;
    int m_lastStatusCode;

    TCHAR m_instanceId[INSTANCE_ID_MAX];

    // Result of the last LookupByCode, kept so GetMatch() can hand it back
    // without a second round trip. AssetSummary is fixed-size, so this costs
    // one memcpy per scan and no allocation at all -- which is the point, on a
    // heap that a shift of scanning would otherwise fragment.
    Models::AssetSummary m_lastMatch;
    int m_matchCount;

    /**
     * Issues one authenticated API call against m_baseUrl + endpoint.
     *
     * On success *response receives the complete response body as a
     * heap-allocated, NUL-terminated string (never NULL) that the caller must
     * release with delete[]; on failure it is set to NULL. Pass NULL for
     * `response` to discard the body. The body is never truncated to fit a
     * caller buffer -- an API reply that does not fit used to be silently cut
     * short and reported as a success.
     */
    bool MakeApiRequest(const TCHAR* method, const TCHAR* endpoint, const TCHAR* body, TCHAR** response);
    void SetAuthHeaders();

    ReplayResult ReplayScan(const TCHAR* data);
    ReplayResult ReplayUpdate(const TCHAR* data);

    // Not copyable: the instance owns the HTTP client and its strings.
    HbClient(const HbClient&);
    HbClient& operator=(const HbClient&);
};

} // namespace HBX

#endif // HBCLIENT_HPP
