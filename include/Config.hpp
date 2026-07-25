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

private:
    TCHAR* m_apiBaseUrl;
    TCHAR* m_deviceId;
    TCHAR* m_authToken;
    TCHAR* m_apiKey;
    TCHAR* m_journalPath;
    TCHAR* m_logLevel;
    TCHAR* m_configPath;
    int m_syncIntervalSeconds;
    bool m_offlineModeEnabled;
    bool m_scannerBeepEnabled;
    bool m_scannerVibrateEnabled;

    // Helper methods
    void InitDefaults();
    void Cleanup();

    // Not copyable: the class owns raw allocations.
    Config(const Config&);
    Config& operator=(const Config&);
};

} // namespace HBX

#endif // CONFIG_HPP
