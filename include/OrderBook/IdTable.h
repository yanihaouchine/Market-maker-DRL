#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

#include "Core/Types.h"
#include "OrderBook/OrderPool.h"

namespace ob {
// Hachage multiplicatif de Fibonacci : une multiplication, les bits de poids fort servent d'indice.
struct FibonacciHash {
    static std::size_t Index(OrderId id, unsigned bits) {
        return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ull) >> (64 - bits));
    }
};

// Identité : idéale pour des identifiants séquentiels, dégénère sur des identifiants à pas régulier.
struct IdentityHash {
    static std::size_t Index(OrderId id, unsigned bits) {
        return static_cast<std::size_t>(id & ((std::uint64_t{1} << bits) - 1));
    }
};

#if defined(__SSE4_2__)
struct Crc32Hash {
    static std::size_t Index(OrderId id, unsigned bits) {
        return static_cast<std::size_t>(_mm_crc32_u64(0, id) & ((std::uint64_t{1} << bits) - 1));
    }
};
#endif

// Table OrderId -> indice dans le pool. Adressage ouvert, sondage linéaire, capacité en puissance
// de deux, suppression par décalage arrière (pas de pierres tombales). Une case fait 16 octets :
// quatre cases par ligne de cache, donc une recherche touche presque toujours une seule ligne.
template<class Hash = FibonacciHash, unsigned LoadNum = 1, unsigned LoadDen = 2>
class IdTable {
public:
    IdTable() { Rebuild(4); }

    std::uint32_t Find(OrderId id) const {
        std::size_t i = Hash::Index(id, bits_);
        while (true) {
            const Slot &slot = slots_[i];
            if (slot.index == kNil) return kNil;
            if (slot.id == id) return slot.index;
            i = (i + 1) & mask_;
        }
    }

    void Prefetch(OrderId id) const { __builtin_prefetch(&slots_[Hash::Index(id, bits_)]); }

    // Précondition : id absent.
    void Insert(OrderId id, std::uint32_t index) {
        if ((count_ + 1) * LoadDen > slots_.size() * LoadNum) [[unlikely]] {
            Grow();
        }
        std::size_t i = Hash::Index(id, bits_);
        while (slots_[i].index != kNil) i = (i + 1) & mask_;
        slots_[i].id = id;
        slots_[i].index = index;
        ++count_;
    }

    // Précondition : id présent.
    void Erase(OrderId id) {
        std::size_t i = Hash::Index(id, bits_);
        while (slots_[i].id != id || slots_[i].index == kNil) i = (i + 1) & mask_;

        std::size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (slots_[j].index == kNil) break;
            const std::size_t home = Hash::Index(slots_[j].id, bits_);
            if (((j - home) & mask_) >= ((j - i) & mask_)) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].index = kNil;
        --count_;
    }

    std::size_t Size() const { return count_; }

    void Reserve(std::size_t n) {
        unsigned bits = bits_;
        while ((n + 1) * LoadDen > (std::size_t{1} << bits) * LoadNum) ++bits;
        if (bits != bits_) Rehash(bits);
    }

    void Clear() { Rebuild(bits_); }

    template<class F>
    void ForEach(F &&f) const {
        for (const Slot &slot: slots_) {
            if (slot.index != kNil) f(slot.id, slot.index);
        }
    }

private:
    struct Slot {
        OrderId id;
        std::uint32_t index;
    };

    void Rebuild(unsigned bits) {
        bits_ = bits;
        mask_ = (std::size_t{1} << bits) - 1;
        slots_.assign(std::size_t{1} << bits, Slot{0, kNil});
        count_ = 0;
    }

    void Rehash(unsigned bits) {
        std::vector<Slot, BigArrayAllocator<Slot>> old;
        old.swap(slots_);
        Rebuild(bits);
        for (const Slot &slot: old) {
            if (slot.index == kNil) continue;
            std::size_t i = Hash::Index(slot.id, bits_);
            while (slots_[i].index != kNil) i = (i + 1) & mask_;
            slots_[i] = slot;
            ++count_;
        }
    }

    [[gnu::noinline, gnu::cold]] void Grow() { Rehash(bits_ + 1); }

    std::vector<Slot, BigArrayAllocator<Slot>> slots_;
    std::size_t mask_ = 0;
    std::size_t count_ = 0;
    unsigned bits_ = 0;
};

// Table à accès direct avec débordement. La case d'un identifiant est `id & masque`, sans
// sondage : des identifiants attribués en séquence remplissent la table dans l'ordre de la
// mémoire (une ligne de cache sert quatre ordres consécutifs et le préchargeur matériel suit).
// Si la case est déjà prise par un autre ordre vivant, l'entrée va dans une table hachée
// classique. Insertion, recherche et suppression restent en O(1) quelle que soit la distribution
// des identifiants : au pire (identifiants aléatoires) on retombe sur le coût d'une table hachée.
template<class Overflow = IdTable<FibonacciHash, 1, 2>>
class DirectIdTable {
public:
    DirectIdTable() { Rebuild(8); }

    std::uint32_t Find(OrderId id) const {
        const Slot &slot = slots_[id & mask_];
        if (slot.id == id && slot.index != kNil) return slot.index;
        if (overflow_.Size() == 0) [[likely]] {
            return kNil;
        }
        return overflow_.Find(id);
    }

    void Prefetch(OrderId id) const { __builtin_prefetch(&slots_[id & mask_]); }

    // Précondition : id absent.
    void Insert(OrderId id, std::uint32_t index) {
        if ((count_ + 1) * 2 > slots_.size()) [[unlikely]] {
            Grow();
        }
        Slot &slot = slots_[id & mask_];
        if (slot.index == kNil) {
            slot.id = id;
            slot.index = index;
        } else {
            overflow_.Insert(id, index);
        }
        ++count_;
    }

    // Précondition : id présent.
    void Erase(OrderId id) {
        Slot &slot = slots_[id & mask_];
        if (slot.id == id && slot.index != kNil) {
            slot.index = kNil;
        } else {
            overflow_.Erase(id);
        }
        --count_;
    }

    std::size_t Size() const { return count_; }

    void Reserve(std::size_t n) {
        unsigned bits = bits_;
        while ((n + 1) * 2 > (std::size_t{1} << bits)) ++bits;
        if (bits != bits_) Rehash(bits);
    }

    void Clear() { Rebuild(bits_); }

private:
    struct Slot {
        OrderId id;
        std::uint32_t index;
    };

    void Rebuild(unsigned bits) {
        bits_ = bits;
        mask_ = (std::size_t{1} << bits) - 1;
        slots_.assign(std::size_t{1} << bits, Slot{0, kNil});
        overflow_.Clear();
        count_ = 0;
    }

    void Rehash(unsigned bits) {
        std::vector<Slot, BigArrayAllocator<Slot>> old;
        old.swap(slots_);
        std::vector<Slot> spilled;
        overflow_.ForEach([&spilled](OrderId id, std::uint32_t index) { spilled.push_back(Slot{id, index}); });
        Rebuild(bits);
        for (const Slot &slot: old) {
            if (slot.index != kNil) Insert(slot.id, slot.index);
        }
        for (const Slot &slot: spilled) Insert(slot.id, slot.index);
    }

    [[gnu::noinline, gnu::cold]] void Grow() { Rehash(bits_ + 1); }

    std::vector<Slot, BigArrayAllocator<Slot>> slots_;
    Overflow overflow_;
    std::size_t mask_ = 0;
    std::size_t count_ = 0;
    unsigned bits_ = 0;
};
} // namespace ob
