#ifndef GTOS_I18N_PINYIN_H
#define GTOS_I18N_PINYIN_H
#include <i18n/utf8.h>
namespace gtos {
namespace i18n {
// A bounded exact-spelling phrase dictionary, not a general-purpose or learning IME.
class PinyinComposer {
  public:
    enum { MaxPreedit = 31, MaxCandidates = 9, CommitCapacity = 64 };
    enum FeedResult { PassedThrough, Composing, Committed, Canceled, Rejected };
    PinyinComposer();
    FeedResult Feed(uint8_t key, char *committed, uint32_t capacity);
    void Cancel();
    bool Active() const {
        return length != 0;
    }
    const char *Preedit() const {
        return preedit;
    }
    uint32_t CandidateCount() const;
    const char *Candidate(uint32_t index) const;
    // Returns Rejected without dropping preedit if index/capacity is invalid.
    FeedResult Select(uint32_t index, char *committed, uint32_t capacity);

  private:
    char preedit[MaxPreedit + 1];
    uint32_t length;
};
uint32_t DictionaryEntryCount();
const char *DictionarySpelling(uint32_t entry);
uint32_t DictionaryCandidateCount(uint32_t entry);
const char *DictionaryCandidate(uint32_t entry, uint32_t candidate);
} // namespace i18n
} // namespace gtos
#endif
