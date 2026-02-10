#pragma once
#include "Socket.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace hft::network {

class BinanceSocket : public Socket {
private:
    net::io_context ioc_;
    ssl::context ssl_ctx_{ssl::context::tlsv12_client};
    websocket::stream<ssl::stream<beast::tcp_stream>> ws_;
    beast::flat_buffer read_buffer_;
    
public:
    BinanceSocket() : ws_(ioc_, ssl_ctx_) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }
    
    bool connect(const std::string& host, uint16_t port) {
        try {
            // SNI obligatoire pour Binance
            if (!SSL_set_tlsext_host_name(
                    ws_.next_layer().native_handle(), 
                    host.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()),
                                     net::error::get_ssl_category()),
                    "Failed to set SNI"
                );
            }
            
            tcp::resolver resolver(ioc_);
            std::string port_str = (port == 0) ? "443" : std::to_string(port);
            auto const results = resolver.resolve(host, port_str);
            
            // Timeout (optionnel pour HFT)
            beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
            
            // Connexion TCP
            beast::get_lowest_layer(ws_).connect(results);
            
            // Handshake SSL
            ws_.next_layer().handshake(ssl::stream_base::client);
            
            // 3. Handshake WebSocket
            ws_.handshake(host, "/ws/btcusdt@bookTicker");
            
            // Désactiver timeout après connexion
            beast::get_lowest_layer(ws_).expires_never();
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Binance connection failed: " << e.what() << std::endl;
            return false;
        }
    }
    
    ssize_t receive(void* buffer, size_t size) override {
        try {
            read_buffer_.consume(read_buffer_.size());
            ws_.read(read_buffer_);
            
            auto data = read_buffer_.data();
            size_t to_copy = std::min(size, data.size());
            std::memcpy(buffer, data.data(), to_copy);
            
            return static_cast<ssize_t>(to_copy);
        } catch (const std::exception& e) {
            std::cerr << "Receive error: " << e.what() << std::endl;
            return -1;
        }
    }
    
    ssize_t send(const void* data, size_t size) override {
        try {
            ws_.write(net::buffer(data, size));
            return static_cast<ssize_t>(size);
        } catch (const std::exception& e) {
            std::cerr << "Send error: " << e.what() << std::endl;
            return -1;
        }
    }
    
    void close() override {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
        if (ec) {
            std::cerr << "Close error: " << ec.message() << std::endl;
        }
    }
    
    bool is_connected() const override {
        return ws_.is_open();
    }
};

} // namespace hft::network