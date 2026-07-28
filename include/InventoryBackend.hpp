#ifndef INVENTORYBACKEND_HPP
#define INVENTORYBACKEND_HPP

#include <windows.h>
#include "Models/AssetSummary.hpp"

namespace HBX {

/**
 * Outcome of replaying one queued transaction.
 *
 * The third state matters: a queue entry can belong to a backend that is no
 * longer configured (the operator switched systems, or changed the NetBox
 * instance id). Reporting that as a failure would pin the sync status at
 * "failed" forever and make the queue look permanently broken, while reporting
 * it as success would silently discard the operator's work. SKIPPED is neither
 * -- the entry stays queued, is excluded from the success/failure ratio, and is
 * surfaced separately in the queue view.
 */
enum ReplayResult {
    REPLAY_SENT,     // accepted by the server; the entry can be acknowledged
    REPLAY_RETRY,    // transient failure; leave queued and try again later
    REPLAY_SKIPPED   // not addressed to any configured backend
};

/**
 * One inventory system the handheld can talk to.
 *
 * Two implementations exist: HbClient (HomeBox) and NbClient (NetBox). The
 * controller and the sync engine work only through this interface, so neither
 * needs to know which system is active.
 *
 * The interface is deliberately narrow -- it covers exactly what the device
 * does, which is resolve a scanned code and push small mutations. Bulk reads,
 * creation and schema browsing are not here: a 240x320 screen with a numeric
 * keypad is not where anyone builds a new device record, and every operation
 * that is not on this list is one the app has no UI for.
 *
 * Cost note: one vtable pointer per instance and at most two instances exist,
 * so the abstraction costs 8 bytes of RAM on a device where that matters.
 */
class InventoryBackend {
public:
    virtual ~InventoryBackend() {}

    // ---- identity -------------------------------------------------------

    /** Stable kind token used in queue records: "hb" or "nb". */
    virtual const TCHAR* GetKind() const = 0;

    /**
     * Operator-assigned instance id, e.g. "nb-prod". Queue records carry this
     * rather than just the kind, because replaying a queued move against a
     * *different* NetBox instance would silently target whatever object
     * happens to hold that asset tag there -- asset tags are unique per
     * instance, not globally.
     */
    virtual const TCHAR* GetInstanceId() const = 0;

    /** Short name for the title bar and the queue view, e.g. "NetBox". */
    virtual const TCHAR* GetDisplayName() const = 0;

    // ---- transport and session -----------------------------------------

    virtual void SetBaseUrl(const TCHAR* baseUrl) = 0;
    virtual const TCHAR* GetBaseUrl() const = 0;
    virtual void SetRequestTimeout(DWORD timeoutMs) = 0;

    virtual bool IsAuthenticated() const = 0;
    virtual int GetLastStatusCode() const = 0;

    /**
     * Whether a rejected session can be renewed by authenticating again.
     * HomeBox exchanges credentials for a short-lived token, so a 401 is worth
     * one retry. NetBox uses a long-lived API token supplied by configuration:
     * a 401 there means the configured token is wrong or revoked, and retrying
     * only burns a round trip per scan. The controller gates its retry on this.
     */
    virtual bool SessionIsRenewable() const = 0;

    /** Establishes a session. Returns true when the backend is usable. */
    virtual bool Authenticate() = 0;

    // ---- operations -----------------------------------------------------

    /**
     * Resolves a scanned code to an asset. `out` is filled only on success.
     * Returns false when nothing matched, when more than one thing matched
     * ambiguously, or on a transport failure -- inspect GetLastStatusCode and
     * GetMatchCount to tell those apart.
     */
    virtual bool LookupByCode(const TCHAR* code, Models::AssetSummary* out) = 0;

    /**
     * Number of results the last LookupByCode saw. 0 means "no such asset",
     * greater than 1 means the operator must disambiguate -- NetBox does not
     * enforce uniqueness on a device serial, so one scan can legitimately match
     * several devices, and picking the first would move the wrong one.
     */
    virtual int GetMatchCount() const = 0;

    /** Fills `out` with match `index` from the last LookupByCode. */
    virtual bool GetMatch(int index, Models::AssetSummary* out) = 0;

    /** Whether this backend has a status concept the UI should offer. */
    virtual bool SupportsStatus() const = 0;

    /**
     * Valid status values for this backend, for the status picker.
     * Returns the count; `index` selects one. HomeBox returns 0.
     */
    virtual int GetStatusChoiceCount() const = 0;
    virtual const TCHAR* GetStatusChoice(int index) const = 0;

    /**
     * Replays one queued transaction addressed to this backend. `type` is the
     * transaction type with its backend prefix already stripped; `data` is the
     * payload built when it was queued.
     */
    virtual ReplayResult Replay(const TCHAR* type, const TCHAR* data) = 0;

protected:
    InventoryBackend() {}

private:
    // Not copyable: implementations own transports and heap strings.
    InventoryBackend(const InventoryBackend&);
    InventoryBackend& operator=(const InventoryBackend&);
};

} // namespace HBX

#endif // INVENTORYBACKEND_HPP
