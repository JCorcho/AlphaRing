#include "entry.h"

#include <cassert>

#include <MinHook.h>

Entry::Entry(EntrySet* set, const AlphaRing::Offset& offset, void *pDetour, AlphaRing::Feature* feature) {
    m_pOriginal = nullptr;
    m_pDetour = pDetour;
    m_source = &offset;
    m_feature = feature;
    m_offset = 0;
    m_target = 0;

    if (feature != nullptr) feature->Add(offset);
    set->append(this);
}

bool Entry::update(__int64 hModule) {
    MH_STATUS status;

    if (hModule == 0) return false;

    remove();

    // an MCC build without its offset or one of its feature's (src/offsets/Offsets.h)
    if (auto missing = m_feature ? m_feature->Missing() : (m_source->found() ? nullptr : m_source)) {
        LOG_WARNING("[Offsets] not hooking {}{}{}: {} isn't in this build", m_source->name, m_feature ? " - " : "",
                    m_feature ? m_feature->Name() : "", missing->name);
        return false;
    }

    m_offset = m_source->value;
    m_target = m_offset + hModule;

    status = MH_CreateHook((void*)m_target, (void*)m_pDetour, (void**)&m_pOriginal);
    if (status == MH_OK) status = MH_EnableHook((void*)m_target);
    if (status != MH_OK) {
        LOG_WARNING("[Hooks] {} at {:#x} NOT hooked: {}", m_source->name, m_offset, MH_StatusToString(status));
        m_target = 0;
        return false;
    }
    LOG_INFO("[Hooks] {} at {:#x} hooked", m_source->name, m_offset);
    return true;
}

void Entry::remove() {
    if (m_target == 0) return;
    MH_RemoveHook((void*)m_target, true);
    m_target = 0;
    m_pOriginal = nullptr;
}

void EntrySet::append(Entry *entry) {
    assert(entryCount < MAX_ENTRY);
    entryArray[entryCount++] = entry;
}

bool EntrySet::update(__int64 hModule) {
    bool result = true;

    if (hModule == 0) return false;

    int hooked = 0;
    for (int i = 0; i < entryCount; ++i) {
        bool ok = entryArray[i]->update(hModule);
        result &= ok;
        hooked += ok;
    }
    if (entryCount > 0) LOG_INFO("[Hooks] module {:#x}: {} of {} hooks in", (unsigned long long)hModule, hooked, entryCount);

    return result;
}

void EntrySet::remove() {
    for (int i = 0; i < entryCount; ++i) entryArray[i]->remove();
    for (int i = 0; i < callbackCount; ++i) callbackArray[i]();
}

void EntrySet::on_remove(void (*callback)()) {
    assert(callbackCount < MAX_CALLBACK);
    callbackArray[callbackCount++] = callback;
}
