#include <unistd.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <array>
#include <limits>
#include <asio.hpp>

std::string ParseHttpUrl(const std::string& request) {
    std::stringstream req(request);
    std::string method;
    req >> method;
    if (method != "GET") {
        return "";
    }

    std::string path;
    req >> path;

    std::string http_version;
    req >> http_version;
    if (http_version.substr(0, 7) != "HTTP/1.") {
        return "";
    }
    return path;
}

class StupidToyHttpServer {
    using tcp = asio::ip::tcp;

    class ClientHandler : public std::enable_shared_from_this<ClientHandler> {
        ClientHandler(tcp::socket& s, const std::string& p) : socket_(std::move(s)), directory_path_(p) {};


    public:
        static std::shared_ptr<ClientHandler> Create(tcp::socket& socket, const std::string& directory_path) {
            return std::shared_ptr<ClientHandler>(new ClientHandler(socket, directory_path));
        }

        void ProcessClientRequest() {
            auto self = shared_from_this();
            asio::async_read_until(socket_, asio::dynamic_buffer(request_buffer_), "\r\n\r\n",
              [self](const asio::error_code& ec, size_t size) {
                if (ec) {
                    std::cerr << "Error reading request: " << ec.message() << "\n";
                    return;
                }

                auto relpath = ParseHttpUrl(self->request_buffer_);
                if (relpath == "" || relpath.find("..") != std::string::npos) {
                    self->answer_with(400);
                    return;
                }

                auto path = self->directory_path_ + relpath;
                std::ifstream file(path);
                if (!file) {
                    self->answer_with(404);
                    return;
                }
                file.seekg(0, std::ios_base::end);
                auto file_size = std::streamoff(file.tellg());
                file.seekg(0);
                if (file_size == std::numeric_limits<std::streamoff>::max()) {
                    self->answer_with(404);
                    return;
                }

                self->file_contents_.resize(file_size);
                file.read(self->file_contents_.data(), file_size);
                self->answer_with(200);
            });
        }


    private:
        void answer_with(int code) {
            response_ = "HTTP/1.0 ";
            response_ += std::to_string(code) + " ";
            switch (code) {
                case 200:
                    response_ += "OK";
                    break;
                case 400:
                    response_ += "Bad Request";
                    break;
                case 404:
                    response_ += "Not Found";
                    break;
            }
            response_ += "\r\n";
            if (code == 200) {
                response_ += "Content-Type: text/html\r\nContent-Length: " + std::to_string(file_contents_.size()) + "\r\n";
            }
            response_ += "\r\n";

            auto self = shared_from_this();
            asio::async_write(socket_, std::array<asio::const_buffer, 2>{asio::buffer(response_), asio::buffer(file_contents_)},
              [self](const asio::error_code& ec, size_t size) {
                if (ec) {
                    std::cerr << "Error writing response: " << ec.message() << "\n";
                }

                asio::error_code e;
                self->socket_.shutdown(tcp::socket::shutdown_both, e);
                self->socket_.close(e);
            });
        }

        tcp::socket socket_;
        const std::string& directory_path_;
        std::string request_buffer_;
        std::vector<char> file_contents_;
        std::string response_;
    };


public:
    StupidToyHttpServer(asio::io_context& io_ctx, tcp::endpoint ep, const std::string& path) :
        io_ctx_(io_ctx), acceptor_(io_ctx, ep), directory_path_(path) {
            do_accept();
        }


private:
    void do_accept() {
        acceptor_.async_accept([this](const asio::error_code& ec, tcp::socket client_socket) {
            if (ec) {
                std::cerr << "Can't accept client connection: " << ec.message() << "\n";
                return;
            }

            auto handler = ClientHandler::Create(client_socket, directory_path_);
            handler->ProcessClientRequest();
            do_accept();
        });
    }

    asio::io_context& io_ctx_;
    tcp::acceptor acceptor_;
    const std::string directory_path_;
};

struct Params {
    std::string error_string;
    asio::ip::tcp::endpoint endpoint;
    std::string directory_path;
};

Params ParseArgs(int argc, char* argv[]) {
    Params params;
    asio::ip::address_v4 ip;
    int port;
    bool got_ip{}, got_port{}, got_path{};

    while (true) {
        int c = getopt(argc, argv, "h:p:d:");
        if (c == -1) {
            break;
        }

        if (c == 'h') {
            got_ip = true;
            asio::error_code ec;
            ip = asio::ip::make_address_v4(optarg, ec);
            if (ec) {
                params.error_string = "Invalid ip address: " + ec.message();
                return params;
            }

        } else if (c == 'p') {
            got_port = true;
            port = std::atoi(optarg);
            if (!(port > 0 && port <= std::numeric_limits<uint16_t>::max())) {
                params.error_string = "Invalid port";
                return params;
            }

        } else if (c == 'd') {
            got_path = true;
            params.directory_path = std::string(optarg) + "/";

        } else {
            params.error_string = "Invalid argument string";
            return params;
        }
    }
    if (!(got_ip && got_port && got_path)) {
        params.error_string = "Not enough arguments";
        return params;
    }

    params.endpoint = asio::ip::tcp::endpoint(ip, port);
    return params;
}

void ContinueInChild() {
    if (fork()) {
        std::exit(0);
    }
}

int main(int argc, char* argv[]) {
    ContinueInChild();

    Params params = ParseArgs(argc, argv);
    if (!params.error_string.empty()) {
        std::cerr << params.error_string << std::endl;
        return 1;
    }

    asio::io_context io;
    StupidToyHttpServer server(io, params.endpoint, params.directory_path);
    io.run();
}
