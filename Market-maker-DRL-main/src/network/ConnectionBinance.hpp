#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include "Connection.hpp"
#include <iostream>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace hft::network {

class ConnectionBinance : public Connection {
private:
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer read_buffer_;

public:
    ConnectionBinance(net::io_context& ioc, ssl::context& ctx) 
        : Connection(), 
          ws_(ioc, ctx) {}

    bool connect(std::string_view host, uint16_t port = 443) {
        try {
            // CRITIQUE : SNI pour Binance SSL
            std::string host_str(host);
            if (!SSL_set_tlsext_host_name(
                    ws_.next_layer().native_handle(), 
                    host_str.c_str())) {
                throw beast::system_error(
                    beast::error_code(
                        static_cast<int>(::ERR_get_error()),
                        net::error::get_ssl_category()),
                    "SNI configuration failed"
                );
            }

            tcp::resolver resolver(beast::get_lowest_layer(ws_).get_executor());
            std::string port_str = (port == 0) ? "443" : std::to_string(port);
            auto const results = resolver.resolve(host_str, port_str);

            // Timeout de connexion
            beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));

            // Connexion TCP
            beast::get_lowest_layer(ws_).connect(results);

            // Handshake SSL
            ws_.next_layer().handshake(ssl::stream_base::client);

            // Handshake WebSocket
            ws_.handshake(host_str, "/ws/btcusdt@bookTicker");

            // Désactiver timeout après connexion
            beast::get_lowest_layer(ws_).expires_never();

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Binance connection error: " << e.what() << std::endl;
            if (error_handler_) {
                error_handler_(std::string("Connection failed: ") + e.what());
            }
            return false;
        }
    }

    void poll() {
        try {
            // Réutiliser le buffer
            read_buffer_.consume(read_buffer_.size());
            
            // Lecture synchrone bloquante
            ws_.read(read_buffer_);

            if (message_handler_) {
                auto data = read_buffer_.data();
                message_handler_(std::span<const uint8_t>(
                    static_cast<const uint8_t*>(data.data()), 
                    data.size()));
            }

        } catch (const beast::system_error& e) {
            // Déconnexion ou erreur réseau
            std::cerr << "Poll error: " << e.what() << std::endl;
            if (error_handler_) {
                error_handler_(std::string("Read failed: ") + e.what());
            }
        } catch (const std::exception& e) {
            std::cerr << "Unexpected poll error: " << e.what() << std::endl;
        }
    }

    bool send(std::span<const uint8_t> data) {
        try {
            ws_.write(net::buffer(data.data(), data.size()));
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Send error: " << e.what() << std::endl;
            return false;
        }
    }

    bool is_connected() const {
        return ws_.is_open();
    }

    void close() {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
        if (ec) {
            std::cerr << "Close error: " << ec.message() << std::endl;
        }
    }
};

} // namespace hft::network