#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <windows.h>

namespace HBX {

/**
 * Configuration manager for HBXClient application
 * Handles loading and storing application configuration from hb_conf.json
 *
 * Every key documented in docs/DEPLOYMENT.md and README.md is accepted; keys
 * missing from the file fall back to the documented default, so a partially
 * filled hb_conf.json behaves the way the deployment guide promises.
 *
 * The second backend (NetBox) is configured with flat, prefixed keys rather
 * than a nested object per backend. That is forced by the reader, not chosen
 * for style: JsonLite cannot descend into a nested object by key, and the
 * tolerant fallback scanner used on a hand-edited file that fails strict parse
 * matches a quoted key *anywhere* in the document - so with two nested
 * "baseUrl" members it would silently return whichever came first. Loading the
 * wrong server's URL is a worse failure than not loading at all.
 */
class Config {
public:
    Config();
    ~Config();

    // Load configuration from file
    bool Load(const TCHAR* configPath);

    /**
     * Loads from the documented locations, in the order DEPLOYMENT.md lists
     * them: the install directory, then \My Documents, then \Storage Card.
     * Falls back to defaults when none of them exists. Always returns true
     * unless a file was found but could not be read.
     */
    bool LoadFromDefaultLocations();

    // Save configuration to file
    bool Save(const TCHAR* configPath);

    /** Path passed to the last Load() call, or NULL if none succeeded. */
    const TCHAR* GetConfigPath() const;

    // Configuration accessors
    const TCHAR* GetApiBaseUrl() const;
    const TCHAR* GetDeviceId() const;
    const TCHAR* GetAuthToken() const;
    const TCHAR* GetApiKey() const;
    const TCHAR* GetJournalPath() const;
    const TCHAR* GetLogLevel() const;
    int GetSyncIntervalSeconds() const;
    bool IsOfflineModeEnabled() const;
    bool IsScannerBeepEnabled() const;
    bool IsScannerVibrateEnabled() const;

    // ---- backend selection ----------------------------------------------

    /**
     * Instance id of the backend new work is queued against ("activeBackend").
     * Defaults to the HomeBox instance id, so a device that has never heard of
     * NetBox keeps behaving exactly as it did.
     */
    const TCHAR* GetActiveBackendId() const;

    /**
     * Instance ids are the contract that keeps queued work with the server it
     * was meant for, so they are configured rather than derived: an id is
     * written into every queue record, and changing it deliberately strands the
     * entries belonging to the old server instead of replaying them against a
     * different instance where the same asset tag means a different device.
     */
    const TCHAR* GetHomeboxInstanceId() const;
    const TCHAR* GetNetboxInstanceId() const;

    // ---- NetBox ----------------------------------------------------------

    /** Empty when no NetBox is configured, which is how the client tells. */
    const TCHAR* GetNetboxBaseUrl() const;

    /** Long-lived API token; NetBox has no credential-exchange endpoint. */
    const TCHAR* GetNetboxToken() const;

    /**
     * Scheme word placed before the token in the Authorization header. NetBox
     * v1 tokens use "Token <key>"; v4.5+ v2 tokens use "Bearer nbt_<key>.<secret>".
     * Keeping the scheme in configuration rather than in code lets one build
     * talk to both, and survives the v1 removal in NetBox 5.0 without a
     * reflash. Defaults to "Token".
     */
    const TCHAR* GetNetboxAuthScheme() const;

    /**
     * Whether the transport may proceed when the server's certificate cannot be
     * validated. Default false. This exists because Windows Mobile 6.5 tops out
     * at TLS 1.0 with an RSA/RC4/3DES cipher list and cannot verify a
     * SHA-256-signed certificate at all, so a local NetBox behind a modern TLS
     * stack is unreachable over HTTPS; the supported deployment is plain HTTP on
     * a trusted LAN segment. Turning this on does not make TLS 1.2 work - it
     * only stops a validation failure from being fatal.
     */
    bool IsInsecureTlsAllowed() const;

    // Configuration mutators
    void SetApiBaseUrl(const TCHAR* url);
    void SetDeviceId(const TCHAR* deviceId);
    void SetAuthToken(const TCHAR* token);
    void SetApiKey(const TCHAR* apiKey);
    void SetJournalPath(const TCHAR* path);
    void SetLogLevel(const TCHAR* level);
    void SetSyncIntervalSeconds(int seconds);
    void SetOfflineModeEnabled(bool enabled);
    void SetScannerBeepEnabled(bool enabled);
    void SetScannerVibrateEnabled(bool enabled);

    void SetActiveBackendId(const TCHAR* instanceId);
    void SetHomeboxInstanceId(const TCHAR* instanceId);
    void SetNetboxInstanceId(const TCHAR* instanceId);
    void SetNetboxBaseUrl(const TCHAR* url);
    void SetNetboxToken(const TCHAR* token);
    void SetNetboxAuthScheme(const TCHAR* scheme);
    void SetAllowInsecureTls(bool allowed);

private:
    TCHAR* m_apiBaseUrl;
    TCHAR* m_deviceId;
    TCHAR* m_authToken;
    TCHAR* m_apiKey;
    TCHAR* m_journalPath;
    TCHAR* m_logLevel;
    TCHAR* m_configPath;
    TCHAR* m_activeBackendId;
    TCHAR* m_homeboxInstanceId;
    TCHAR* m_netboxInstanceId;
    TCHAR* m_netboxBaseUrl;
    TCHAR* m_netboxToken;
    TCHAR* m_netboxAuthScheme;
    int m_syncIntervalSeconds;
    bool m_offlineModeEnabled;
    bool m_scannerBeepEnabled;
    bool m_scannerVibrateEnabled;
    bool m_allowInsecureTls;

    // Helper methods
    void InitDefaults();
    void Cleanup();

    // Not copyable: the class owns raw allocations.
    Config(const Config&);
    Config& operator=(const Config&);
};

} // namespace HBX

#endif // CONFIG_HPP
