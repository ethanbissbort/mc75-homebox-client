#ifndef MODELS_ITEM_HPP
#define MODELS_ITEM_HPP

#include <windows.h>

namespace HBX {
namespace Models {

/**
 * Item data model
 * Represents an inventory item in the HomeBox system
 */
class Item {
public:
    Item();
    ~Item();

    // Accessors
    const TCHAR* GetId() const;
    const TCHAR* GetBarcode() const;
    const TCHAR* GetName() const;
    const TCHAR* GetDescription() const;
    const TCHAR* GetLocationId() const;
    int GetQuantity() const;
    const TCHAR* GetCategory() const;

    // Mutators
    void SetId(const TCHAR* id);
    void SetBarcode(const TCHAR* barcode);
    void SetName(const TCHAR* name);
    void SetDescription(const TCHAR* description);
    void SetLocationId(const TCHAR* locationId);
    void SetQuantity(int quantity);
    void SetCategory(const TCHAR* category);

    // Serialization
    bool FromJson(const TCHAR* json);
    TCHAR* ToJson() const;

    // Validation
    bool IsValid() const;

private:
    TCHAR* m_id;
    TCHAR* m_barcode;
    TCHAR* m_name;
    TCHAR* m_description;
    TCHAR* m_locationId;
    int m_quantity;
    TCHAR* m_category;

    void Cleanup();
};

} // namespace Models
} // namespace HBX

#endif // MODELS_ITEM_HPP
