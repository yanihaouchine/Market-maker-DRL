#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#define ORDERBOOK_HAVE_MMAP 1
#endif

#include "Core/Types.h"
#include "OrderBook/OrderPool.h"

namespace ob {
// Les niveaux d'un côté du carnet sont indexés par une clé non signée telle que
// « clé plus petite = meilleur prix », pour les achats comme pour les ventes :
//   ventes : clé = prix décalé en non signé ; achats : son complément.
// Le moteur n'a ainsi qu'un seul chemin de code pour les deux côtés.
inline constexpr std::uint32_t kSideMask[2] = {0xFFFFFFFFu, 0u}; // indexé par Side (Buy = 0, Sell = 1)

inline std::uint32_t PriceToKey(Price price, std::uint32_t sideMask) {
    return (static_cast<std::uint32_t>(price) ^ 0x80000000u) ^ sideMask;
}

inline Price KeyToPrice(std::uint32_t key, std::uint32_t sideMask) {
    return static_cast<Price>((key ^ sideMask) ^ 0x80000000u);
}

// Un niveau de prix : file FIFO intrusive (tête, queue) et quantité agrégée tenue à jour.
struct Level {
    std::uint32_t key;
    std::uint32_t head;
    std::uint32_t tail;
    std::uint64_t quantity;
};

// Candidat (a) : vecteur contigu trié par clé décroissante, meilleur niveau en dernière position.
// L'activité d'un carnet se concentre près du meilleur prix : la recherche part de la fin,
// linéairement sur `LinearSteps` niveaux, puis par dichotomie sur le reste.
template<int LinearSteps = 8>
class SortedLevels {
public:
    Level *Best() { return levels_.empty() ? nullptr : &levels_.back(); }
    const Level *Best() const { return levels_.empty() ? nullptr : &levels_.back(); }

    Level *Find(std::uint32_t key) {
        const std::size_t i = Position(key);
        return (i > 0 && levels_[i - 1].key == key) ? &levels_[i - 1] : nullptr;
    }

    Level *FindOrCreate(std::uint32_t key) {
        const std::size_t i = Position(key);
        if (i > 0 && levels_[i - 1].key == key) return &levels_[i - 1];
        return &*levels_.insert(levels_.begin() + static_cast<std::ptrdiff_t>(i), Level{key, kNil, kNil, 0});
    }

    void Erase(Level *level) { levels_.erase(levels_.begin() + (level - levels_.data())); }

    // Du meilleur au moins bon ; s'arrête quand f renvoie false.
    template<class F>
    void ForEach(F &&f) const {
        for (std::size_t i = levels_.size(); i > 0; --i) {
            if (!f(levels_[i - 1])) return;
        }
    }

    // i-ème niveau en partant du meilleur.
    const Level &FromBest(std::size_t i) const { return levels_[levels_.size() - 1 - i]; }

    bool Empty() const { return levels_.empty(); }
    std::size_t Size() const { return levels_.size(); }
    void Clear() { levels_.clear(); }

private:
    // Plus petit i tel que tous les niveaux d'indice >= i ont une clé strictement inférieure à key.
    std::size_t Position(std::uint32_t key) const {
        std::size_t i = levels_.size();
        int steps = LinearSteps;
        while (i > 0 && levels_[i - 1].key < key) {
            if (--steps < 0) {
                const auto first = levels_.begin();
                const auto it = std::partition_point(first, first + static_cast<std::ptrdiff_t>(i),
                                                     [key](const Level &l) { return l.key >= key; });
                return static_cast<std::size_t>(it - first);
            }
            --i;
        }
        return i;
    }

    std::vector<Level> levels_;
};

// Candidat (b) : fenêtre dense de 65 536 ticks indexée directement par la clé, avec un bitmap
// hiérarchique 64-aire à trois étages pour retrouver le meilleur niveau (structure des solutions
// « Predecessor Problem » de Library Checker). Accès en O(1) quel que soit le nombre de niveaux.
// Les prix hors fenêtre (dont les prix extrêmes des ordres Market) vont dans un SortedLevels.
class DenseLevels {
public:
    DenseLevels() = default;
    DenseLevels(const DenseLevels &) = delete;
    DenseLevels &operator=(const DenseLevels &) = delete;
    ~DenseLevels() { Release(); }

    Level *Best() {
        Level *far = far_.Best();
        if (!top_) return far;
        Level *dense = &levels_[FirstIndex()];
        return (far && far->key < dense->key) ? far : dense;
    }

    Level *Find(std::uint32_t key) {
        const std::uint32_t offset = key - base_;
        if (levels_ && offset < kWindow) {
            return ((low_[offset >> 6] >> (offset & 63)) & 1) ? &levels_[offset] : nullptr;
        }
        return far_.Find(key);
    }

    Level *FindOrCreate(std::uint32_t key) {
        if (!levels_) [[unlikely]] {
            Open(key);
        }
        const std::uint32_t offset = key - base_;
        if (offset >= kWindow) return far_.FindOrCreate(key);

        Level *level = &levels_[offset];
        const std::uint64_t bit = std::uint64_t{1} << (offset & 63);
        if (!(low_[offset >> 6] & bit)) {
            low_[offset >> 6] |= bit;
            mid_[offset >> 12] |= std::uint64_t{1} << ((offset >> 6) & 63);
            top_ |= std::uint64_t{1} << (offset >> 12);
            *level = Level{key, kNil, kNil, 0};
            ++denseCount_;
        }
        return level;
    }

    void Erase(Level *level) {
        if (level >= levels_ && level < levels_ + kWindow) {
            const std::uint32_t offset = static_cast<std::uint32_t>(level - levels_);
            if ((low_[offset >> 6] &= ~(std::uint64_t{1} << (offset & 63))) == 0) {
                if ((mid_[offset >> 12] &= ~(std::uint64_t{1} << ((offset >> 6) & 63))) == 0) {
                    top_ &= ~(std::uint64_t{1} << (offset >> 12));
                }
            }
            --denseCount_;
        } else {
            far_.Erase(level);
        }
    }

    template<class F>
    void ForEach(F &&f) const {
        // Fusion des deux suites triées : fenêtre dense et niveaux lointains.
        const std::size_t farCount = far_.Size();
        std::size_t next = 0;
        for (std::uint64_t t = top_; t; t &= t - 1) {
            const unsigned a = static_cast<unsigned>(__builtin_ctzll(t));
            for (std::uint64_t m = mid_[a]; m; m &= m - 1) {
                const unsigned b = a * 64 + static_cast<unsigned>(__builtin_ctzll(m));
                for (std::uint64_t w = low_[b]; w; w &= w - 1) {
                    const Level &dense = levels_[b * 64 + static_cast<unsigned>(__builtin_ctzll(w))];
                    while (next < farCount && far_.FromBest(next).key < dense.key) {
                        if (!f(far_.FromBest(next++))) return;
                    }
                    if (!f(dense)) return;
                }
            }
        }
        while (next < farCount) {
            if (!f(far_.FromBest(next++))) return;
        }
    }

    bool Empty() const { return top_ == 0 && far_.Empty(); }
    std::size_t Size() const { return denseCount_ + far_.Size(); }

    void Clear() {
        if (low_) std::fill(low_, low_ + (kWindow >> 6), 0);
        std::fill(std::begin(mid_), std::end(mid_), 0);
        top_ = 0;
        denseCount_ = 0;
        far_.Clear();
    }

private:
    static constexpr std::uint32_t kWindow = 1u << 16;

    std::uint32_t FirstIndex() const {
        const unsigned a = static_cast<unsigned>(__builtin_ctzll(top_));
        const unsigned b = a * 64 + static_cast<unsigned>(__builtin_ctzll(mid_[a]));
        return b * 64 + static_cast<unsigned>(__builtin_ctzll(low_[b]));
    }

    static constexpr std::size_t kBytes = kWindow * sizeof(Level) + (kWindow >> 6) * sizeof(std::uint64_t);

    // Centre la fenêtre sur le premier prix vu. Les niveaux et l'étage bas du bitmap partagent une
    // seule projection anonyme : le noyau fournit des pages à zéro à la demande, donc ni la
    // construction du carnet ni cette ouverture ne paient l'initialisation de 1,5 Mio
    // (calloc, lui, effectuait un memset complet : ~100 µs par carnet créé).
    [[gnu::noinline, gnu::cold]] void Open(std::uint32_t key) {
        const std::uint32_t maxBase = 0xFFFFFFFFu - (kWindow - 1);
        base_ = key >= kWindow / 2 ? key - kWindow / 2 : 0;
        if (base_ > maxBase) base_ = maxBase;
#if defined(ORDERBOOK_HAVE_MMAP)
        void *memory = mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) throw std::bad_alloc();
#else
        void *memory = std::calloc(1, kBytes);
        if (!memory) throw std::bad_alloc();
#endif
        levels_ = static_cast<Level *>(memory);
        low_ = reinterpret_cast<std::uint64_t *>(levels_ + kWindow);
    }

    void Release() {
        if (!levels_) return;
#if defined(ORDERBOOK_HAVE_MMAP)
        munmap(levels_, kBytes);
#else
        std::free(levels_);
#endif
    }

    Level *levels_ = nullptr;
    std::uint64_t *low_ = nullptr; // étage bas du bitmap : un bit par tick de la fenêtre
    std::uint32_t base_ = 0;
    std::uint64_t top_ = 0;
    std::uint64_t mid_[kWindow >> 12] = {};
    std::size_t denseCount_ = 0;
    SortedLevels<8> far_;
};

// Candidat (c), témoin : arbre rouge-noir de la bibliothèque standard.
class MapLevels {
public:
    Level *Best() { return levels_.empty() ? nullptr : &levels_.begin()->second; }

    Level *Find(std::uint32_t key) {
        const auto it = levels_.find(key);
        return it == levels_.end() ? nullptr : &it->second;
    }

    Level *FindOrCreate(std::uint32_t key) {
        return &levels_.try_emplace(key, Level{key, kNil, kNil, 0}).first->second;
    }

    void Erase(Level *level) { levels_.erase(level->key); }

    template<class F>
    void ForEach(F &&f) const {
        for (const auto &[key, level]: levels_) {
            if (!f(level)) return;
        }
    }

    bool Empty() const { return levels_.empty(); }
    std::size_t Size() const { return levels_.size(); }
    void Clear() { levels_.clear(); }

private:
    std::map<std::uint32_t, Level> levels_;
};
} // namespace ob
