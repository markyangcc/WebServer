#pragma once

#include "HttpData.h"
#include "HttpParse.h"
#include "HttpResponse.h"
#include "Socket.h"
#include <memory>

#define BUFFERSIZE 2048

class HttpServer {
public:
  enum FileState { FILE_OK, FIlE_NOT_FOUND, FILE_FORBIDDEN };

public:
  explicit HttpServer(int port = 80, const char *ip = nullptr)
      : serverSocket(port, ip) {
    serverSocket.bind();
    serverSocket.listen();
  }

  void run(int, int max_queue_size = 10000);

  void do_request(std::shared_ptr<void> arg);

private:
  void header(std::shared_ptr<HttpData>);

  FileState static_file(std::shared_ptr<HttpData>, const char *);

  void send(std::shared_ptr<HttpData>, FileState);

  void getMime(std::shared_ptr<HttpData>);
  void hanleIndex();

  ServerSocket serverSocket;
};
