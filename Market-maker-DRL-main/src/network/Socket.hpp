#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "stdlib.h"

namespace hft::network {

class Socket {
public:
    virtual ~Socket() = default;

    // Initialise la connexion (DNS + TCP + SSL Handshake)
    virtual bool connect(std::string_view host, uint16_t port) = 0;
    
    // Encapsule les données dans une frame WebSocket et les envoie
    virtual ssize_t send(const void* data, size_t size) = 0;
    
    // Lit une frame WebSocket et extrait les données utiles dans le buffer
    virtual ssize_t receive(void* buffer, size_t size) = 0;
    
    // Ferme proprement la session réseau
    virtual void close() = 0;

    // Retourne l'état de la connexion (True si le handshake est valide)
    virtual bool is_connected() const = 0;
};

}