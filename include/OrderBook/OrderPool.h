#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "Core/OrderType.h"
#include "Core/Types.h"

namespace ob {
inline constexpr std::uint32_t kNil = 0xFFFFFFFFu;

// Allocateur des grands tableaux du moteur. Au-delà de 2 Mio, le bloc est aligné sur 2 Mio et
// signalé au noyau (MADV_HUGEPAGE) : une page de 2 Mio couvre 65 536 ordres au lieu de 128,
// ce qui supprime l'essentiel des défauts dTLB sur les gros carnets.
template<class T>
struct BigArrayAllocator {
    using value_type = T;
    static constexpr std::size_t kHugePage = std::size_t{1} << 21;

    BigArrayAllocator() = default;
    template<class U>
    BigArrayAllocator(const BigArrayAllocator<U> &) noexcept {
    }

    T *allocate(std::size_t n) {
        const std::size_t bytes = n * sizeof(T);
#if defined(__linux__) && !defined(ORDERBOOK_NO_HUGEPAGES)
        if (bytes >= kHugePage) {
            const std::size_t rounded = (bytes + kHugePage - 1) & ~(kHugePage - 1);
            void *p = std::aligned_alloc(kHugePage, rounded);
            if (!p) throw std::bad_alloc();
            madvise(p, rounded, MADV_HUGEPAGE);
            return static_cast<T *>(p);
        }
#endif
        void *p = std::malloc(bytes);
        if (!p) throw std::bad_alloc();
        return static_cast<T *>(p);
    }

    void deallocate(T *p, std::size_t) noexcept { std::free(p); }

    template<class U>
    bool operator==(const BigArrayAllocator<U> &) const noexcept { return true; }
};

// Un ordre au repos : 32 octets, soit deux ordres par ligne de cache. Les liens de la file FIFO
// du niveau de prix sont des indices 32 bits dans le pool, pas des pointeurs.
struct OrderNode {
    OrderId id;
    Price price;
    Quantity quantity; // quantité restante
    std::uint32_t prev;
    std::uint32_t next; // sert aussi de lien dans la liste libre
    OrderType type;
    Side side;
};

static_assert(sizeof(OrderNode) == 32, "OrderNode doit tenir sur une demi-ligne de cache");

// Pool contigu d'ordres. Les emplacements libérés sont réutilisés en LIFO : le dernier ordre
// libéré, encore présent en cache, est le premier réattribué.
class OrderPool {
public:
    std::uint32_t Allocate() {
        if (free_ != kNil) {
            const std::uint32_t index = free_;
            free_ = nodes_[index].next;
            return index;
        }
        nodes_.emplace_back();
        return static_cast<std::uint32_t>(nodes_.size() - 1);
    }

    void Free(std::uint32_t index) {
        nodes_[index].next = free_;
        free_ = index;
    }

    OrderNode &operator[](std::uint32_t index) { return nodes_[index]; }
    const OrderNode &operator[](std::uint32_t index) const { return nodes_[index]; }

    void Reserve(std::size_t n) { nodes_.reserve(n); }

    void Clear() {
        nodes_.clear();
        free_ = kNil;
    }

private:
    std::vector<OrderNode, BigArrayAllocator<OrderNode>> nodes_;
    std::uint32_t free_ = kNil;
};
} // namespace ob
