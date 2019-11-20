#ifndef ART_ROWEX_N_H
#define ART_ROWEX_N_H
//#define ART_NOREADLOCK
//#define ART_NOWRITELOCK
#include "Epoch.h"
#include "Key.h"
#include <atomic>
#include <set>
#include <stdint.h>
#include <string.h>
#ifdef LOCK_INIT
#include "tbb/concurrent_vector.h"
#endif

namespace PART_ns {
/*
 * SynchronizedTree
 * LockCouplingTree
 * LockCheckFreeReadTree
 * UnsynchronizedTree
 */

enum class NTypes : uint8_t { N4 = 1, N16 = 2, N48 = 3, N256 = 4, Leaf = 5 };

class BaseNode {
  public:
    NTypes type;
    BaseNode(NTypes type_) : type(type_) {}
    virtual ~BaseNode() {}
};

class Leaf : public BaseNode {
  public:
    size_t key_len;
    uint64_t key;
    //    uint8_t *fkey;
    // TODO: variable key
    uint8_t fkey[16];
    uint64_t value;

  public:
    Leaf(const Key *k) : BaseNode(NTypes::Leaf) {
        key_len = k->key_len;
        value = k->value;
#ifdef KEY_INLINE
        key = k->key; // compare to store the key, new an array will decrease
                      // 30% performance
        fkey = (uint8_t *)&key;
#else
        //        fkey = new uint8_t[key_len];
        memcpy(fkey, k->fkey, key_len);
#endif
    }
    // use for test
    Leaf() : BaseNode(NTypes::Leaf) {}

    virtual ~Leaf() {
        // TODO
    }

    inline uint64_t getValue() { return value; }
    inline bool checkKey(const Key *k) const {
        if (key_len == k->getKeyLen() && memcmp(fkey, k->fkey, key_len) == 0)
            return true;
        return false;
    }
    inline size_t getKeyLen() const { return key_len; }
} __attribute__((aligned(64)));

static constexpr uint32_t maxStoredPrefixLength = 4;
struct Prefix {
    uint32_t prefixCount = 0;
    uint8_t prefix[maxStoredPrefixLength];
};
static_assert(sizeof(Prefix) == 8, "Prefix should be 64 bit long");

class N : public BaseNode {
  protected:
    N(NTypes type, uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : BaseNode(type), level(level) {
        setType(type);
        setPrefix(prefix, prefixLength, false);
    }

    N(NTypes type, uint32_t level, const Prefix &prefi)
        : BaseNode(type), prefix(prefi), level(level) {
        setType(type);
    }

    N(const N &) = delete;

    N(N &&) = delete;

    virtual ~N() {}

    // 3b type 60b version 1b lock 1b obsolete
    std::atomic<uint64_t> typeVersionLockObsolete{0b100};
    // version 1, unlocked, not obsolete
    alignas(64) std::atomic<Prefix> prefix;
    const uint32_t level;
    uint16_t count = 0;
    uint16_t compactCount = 0;

    static const uint64_t dirty_bit = ((uint64_t)1 << 60);

    void setType(NTypes type);

    static uint64_t convertTypeToVersion(NTypes type);

  public:
    static inline N *setDirty(N *val) {
        return (N *)((uint64_t)val | dirty_bit);
    }
    static inline N *clearDirty(N *val) {
        return (N *)((uint64_t)val & (~dirty_bit));
    }
    static inline bool isDirty(N *val) { return (uint64_t)val & dirty_bit; }

    static void helpFlush(std::atomic<N *> *n);

    NTypes getType() const;

    uint32_t getLevel() const;

    uint32_t getCount() const;

    void setCount(uint16_t count_, uint16_t compactCount_);

    bool isLocked(uint64_t version) const;

    void writeLockOrRestart(bool &needRestart);

    void lockVersionOrRestart(uint64_t &version, bool &needRestart);

    void writeUnlock();

    uint64_t getVersion() const;

    /**
     * returns true if node hasn't been changed in between
     */
    bool checkOrRestart(uint64_t startRead) const;
    bool readUnlockOrRestart(uint64_t startRead) const;

    static bool isObsolete(uint64_t version);

    /**
     * can only be called when node is locked
     */
    void writeUnlockObsolete() { typeVersionLockObsolete.fetch_add(0b11); }

    static std::atomic<N *> *getChild(const uint8_t k, N *node);

    static void insertAndUnlock(N *node, N *parentNode, uint8_t keyParent,
                                uint8_t key, N *val, bool &needRestart);

    static void change(N *node, uint8_t key, N *val);

    static void removeAndUnlock(N *node, uint8_t key, N *parentNode,
                                uint8_t keyParent, bool &needRestart);

    Prefix getPrefi() const;

    void setPrefix(const uint8_t *prefix, uint32_t length, bool flush);

    void addPrefixBefore(N *node, uint8_t key);

    static Leaf *getLeaf(const N *n);

    static bool isLeaf(const N *n);

    static N *setLeaf(const Leaf *k);

    static N *getAnyChild(const N *n);

    static Leaf *getAnyChildTid(const N *n);

    static void deleteChildren(N *node);

    static void deleteNode(N *node);

    static std::tuple<N *, uint8_t> getSecondChild(N *node, const uint8_t k);

    template <typename curN, typename biggerN>
    static void insertGrow(curN *n, N *parentNode, uint8_t keyParent,
                           uint8_t key, N *val, NTypes type, bool &needRestart);

    template <typename curN>
    static void insertCompact(curN *n, N *parentNode, uint8_t keyParent,
                              uint8_t key, N *val, NTypes type,
                              bool &needRestart);

    template <typename curN, typename smallerN>
    static void removeAndShrink(curN *n, N *parentNode, uint8_t keyParent,
                                uint8_t key, NTypes type, bool &needRestart);

    static void getChildren(N *node, uint8_t start, uint8_t end,
                            std::tuple<uint8_t, std::atomic<N *> *> children[],
                            uint32_t &childrenCount);

    static void rebuild_node(N *node,
                             std::set<std::pair<uint64_t, size_t>> &rs);

    static void mfence();

    static void clflush(char *data, int len, bool front, bool back);
} __attribute__((aligned(64)));

} // namespace PART_ns
#endif // ART_ROWEX_N_H
