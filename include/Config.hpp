#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <windows.h>
#include <string>

namespace HBX {

/**
 * Configuration manager for HBXClient application
 * Handles loading and storing application configuration from hb_conf.json
 */
class Config {
public:
    Config();
    ~Config();

    // Load configuration from file
    bool Load(const TCHAR* configPath);

    // Save configuration to file
    bool Save(const TCHAR* configPath);

    // Configuration accessors
    const TCHAR* GetApiBaseUrl() const;
    const TCHAR* GetDeviceId() const;
    const TCHAR* GetAuthToken() const;
    int GetSyncIntervalSeconds() const;
    bool IsOfflineModeEnabled() const;

    // Configuration mutators
    void SetApiBaseUrl(const TCHAR* url);
    void SetDeviceId(const TCHAR* deviceId);
    void SetAuthToken(const TCHAR* token);
    void SetSyncIntervalSeconds(int seconds);
    void SetOfflineModeEnabled(bool enabled);

private:
    TCHAR* m_apiBaseUrl;
    TCHAR* m_deviceId;
    TCHAR* m_authToken;
    int m_syncIntervalSeconds;
    bool m_offlineModeEnabled;

    // Helper methods
    void InitDefaults();
    void Cleanup();
};

} // namespace HBX

#endif // CONFIG_HPP
