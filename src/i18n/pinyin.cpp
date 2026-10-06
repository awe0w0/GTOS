#include <i18n/pinyin.h>
using namespace gtos::i18n;
namespace {
struct Entry {
    const char *spelling;
    uint32_t count;
    const char *candidates[PinyinComposer::MaxCandidates];
};
const Entry dictionary[] = {
#include <i18n/pinyin_data.inc>
};
int Compare(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (uint8_t)*a - (uint8_t)*b;
}
const Entry *Find(const char *spelling) {
    uint32_t low = 0, high = DictionaryEntryCount();
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        int cmp = Compare(dictionary[mid].spelling, spelling);
        if (!cmp)
            return dictionary + mid;
        if (cmp < 0)
            low = mid + 1;
        else
            high = mid;
    }
    return 0;
}
} // namespace
uint32_t gtos::i18n::DictionaryEntryCount() {
    return sizeof(dictionary) / sizeof(dictionary[0]);
}
const char *gtos::i18n::DictionarySpelling(uint32_t entry) {
    return entry < DictionaryEntryCount() ? dictionary[entry].spelling : "";
}
uint32_t gtos::i18n::DictionaryCandidateCount(uint32_t entry) {
    return entry < DictionaryEntryCount() ? dictionary[entry].count : 0;
}
const char *gtos::i18n::DictionaryCandidate(uint32_t entry, uint32_t candidate) {
    return entry < DictionaryEntryCount() && candidate < dictionary[entry].count
               ? dictionary[entry].candidates[candidate]
               : "";
}
PinyinComposer::PinyinComposer() {
    Cancel();
}
void PinyinComposer::Cancel() {
    length = 0;
    preedit[0] = 0;
}
uint32_t PinyinComposer::CandidateCount() const {
    const Entry *entry = Find(preedit);
    return entry ? entry->count : 0;
}
const char *PinyinComposer::Candidate(uint32_t index) const {
    const Entry *entry = Find(preedit);
    return entry && index < entry->count ? entry->candidates[index] : "";
}
PinyinComposer::FeedResult PinyinComposer::Select(uint32_t index, char *committed,
                                                  uint32_t capacity) {
    if (committed && capacity)
        committed[0] = 0;
    if (!committed || !capacity || index >= CandidateCount())
        return Rejected;
    const char *text = Candidate(index);
    uint32_t bytes = ByteLength(text);
    if (bytes >= capacity)
        return Rejected;
    Copy(committed, capacity, text);
    Cancel();
    return Committed;
}
PinyinComposer::FeedResult PinyinComposer::Feed(uint8_t key, char *committed, uint32_t capacity) {
    if (committed && capacity)
        committed[0] = 0;
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    if (key >= 'a' && key <= 'z') {
        if (length == MaxPreedit)
            return Rejected;
        preedit[length++] = key;
        preedit[length] = 0;
        return Composing;
    }
    if (!Active())
        return PassedThrough;
    if (key == 27) {
        Cancel();
        return Canceled;
    }
    if (key == '\b' || key == 127) {
        preedit[--length] = 0;
        return Composing;
    }
    if (key >= '1' && key <= '9')
        return Select(key - '1', committed, capacity);
    if (key == ' ')
        return Select(0, committed, capacity);
    if (key == '\n' || key == '\r') {
        if (CandidateCount())
            return Select(0, committed, capacity);
        // Enter explicitly commits an unmatched Latin spelling; space never does.
        if (!committed || capacity <= length)
            return Rejected;
        Copy(committed, capacity, preedit);
        Cancel();
        return Committed;
    }
    // Unsupported punctuation/control cannot leak into the underlying query mid-composition.
    return Rejected;
}
