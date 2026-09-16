#include "store.h"

#include <string.h>

void store_init(timecast_store_t *store)
{
    if (!store) {
        return;
    }

    memset(store->entries, 0, sizeof(store->entries));
    store->present_count = 0;
}

bool store_import(timecast_store_t *store, uint8_t node_id,
                  const void *data, uint8_t len)
{
    timecast_store_entry_t *entry = &store->entries[node_id];

    if (entry->present) {
        return false;
    }

    store->present_count++;
    if (len != 0U) {
        memcpy(entry->data, data, len);
    }
    entry->len = len;
    entry->present = true;
    return true;
}

bool store_has_data(const timecast_store_t *store, uint8_t node_id)
{
    return store && store->entries[node_id].present;
}

uint16_t store_present_count(const timecast_store_t *store)
{
    return store ? store->present_count : 0U;
}
