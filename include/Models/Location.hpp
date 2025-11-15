#ifndef MODELS_LOCATION_HPP
#define MODELS_LOCATION_HPP

#include <windows.h>

namespace HBX {
namespace Models {

/**
 * Location data model
 * Represents a storage location in the HomeBox system
 */
class Location {
public:
    Location();
    ~Location();

    // Accessors
    const TCHAR* GetId() const;
    const TCHAR* GetName() const;
    const TCHAR* GetDescription() const;
    const TCHAR* GetParentId() const;
    const TCHAR* GetPath() const;

    // Mutators
    void SetId(const TCHAR* id);
    void SetName(const TCHAR* name);
    void SetDescription(const TCHAR* description);
    void SetParentId(const TCHAR* parentId);
    void SetPath(const TCHAR* path);

    // Serialization
    bool FromJson(const TCHAR* json);
    TCHAR* ToJson() const;

    // Validation
    bool IsValid() const;

private:
    TCHAR* m_id;
    TCHAR* m_name;
    TCHAR* m_description;
    TCHAR* m_parentId;
    TCHAR* m_path;

    void Cleanup();
};

} // namespace Models
} // namespace HBX

#endif // MODELS_LOCATION_HPP
