#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <cstdint>
#include "Socket.hpp"

namespace hft::network {

// Handler appelé lorsqu'un message complet est prêt à être traité.
std::function<void(std::string_view)> error_handler_; 
class Connection {
public:
    /**
     * @brief Constructeur
     * @param handler La fonction à exécuter à chaque fois qu'un message arrive.
     */
    explicit Connection(MessageHandler handler = nullptr) 
        : message_handler_(std::move(handler)) {}

    virtual ~Connection() = default;

    /**
     * Sécurité : On interdit de copier une connexion réseau (deux objets ne peuvent pas 
     * posséder le même socket physique).
     */
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Move : On autorise le transfert d'une connexion d'une variable à une autre.
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;


    /**
     * @brief Établit la connexion.
     * Doit gérer toute la séquence : DNS -> TCP -> SSL -> Handshake WebSocket.
    */    
    virtual bool connect(std::string_view host, uint16_t port) = 0;

    /**
     * @brief Envoie un message.
     * @param data Le bloc de données (une commande d'achat JSON).
     */
    virtual bool send(std::span<const uint8_t> data) = 0;

    /**
     * @brief Méthode de surveillance.
     * Lit les données entrantes sur le socket et appelle 'message_handler_' si un message est complet.
     */
    virtual void poll() = 0;

    /**
     * @brief Permet de changer dynamiquement la fonction de traitement des messages.
     */
    void set_message_handler(MessageHandler handler) {
        message_handler_ = std::move(handler);
    }

    /**
     * @brief Vérifie si la session réseau est toujours active.
    */
    virtual bool is_connected() const = 0;

protected:
    // La fonction de rappel stockée qui lie cette couche réseau à la logique de l'OrderBook.
    MessageHandler message_handler_;
};

}  // namespace hft::network