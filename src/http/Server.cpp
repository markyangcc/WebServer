#include "../../include/Server.h"
#include "../../include/Epoll.h"
#include "../../include/HttpData.h"
#include "../../include/HttpParse.h"
#include "../../include/HttpResponse.h"
#include "../../include/ThreadPool.h"
#include "../../include/Util.h"

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

char NOT_FOUND_PAGE[] = "<!DOCTYPE html>\n"
                        "<html>\n"
                        "<head>\n"
                        "    <title>404 Not Found</title>\n"
                        "    <style>\n"
                        "        body {\n"
                        "            width: 35em;\n"
                        "            margin: 0 auto;\n"
                        "            font-family: Arial, sans-serif;\n"
                        "        }\n"
                        "    </style>\n"
                        "</head>\n"
                        "<body>\n"
                        "<h1>404 Not Found</h1>\n"
                        "\n"
                        "</body>\n"
                        "</html>";

char FORBIDDEN_PAGE[] = "<!DOCTYPE html>\n"
                        "<html>\n"
                        "<head>\n"
                        "    <title>403 Forbidden</title>\n"
                        "    <style>\n"
                        "        body {\n"
                        "            width: 35em;\n"
                        "            margin: 0 auto;\n"
                        "            font-family: Arial, sans-serif;\n"
                        "        }\n"
                        "    </style>\n"
                        "</head>\n"
                        "<body>\n"
                        "<h1>403 Forbidden</h1>\n"
                        "\n"
                        "</body>\n"
                        "</html>";

char INDEX_PAGE[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>Welcome to My WebServer!</title>\n"
    "    <style>\n"
    "        body {\n"
    "            width: 35em;\n"
    "            margin: 0 auto;\n"
    "            font-family: Arial, sans-serif;\n"
    "        }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>Welcome to My WebServer!</h1>\n"
    "\n"
    "<p>Webserver Socrce Code and README.md is hosted on Github\n"
    "    <a href=\"https://github.com/markyangcc/WebServer\"> "
    "WebServer</a>.<br/>\n"
    "\n"
    "</body>\n"
    "</html>";

char TEST[] = "HELLO WORLD";

extern std::string basePath;

void HttpServer::run(int thread_num, int max_queque_size) {
  ThreadPool threadPool(thread_num, max_queque_size);

  // 此参数已经不起作用，大于0即可
  int epoll_fd = Epoll::init(1024);

  std::shared_ptr<HttpData> httpData(new HttpData());
  httpData->epoll_fd = epoll_fd;
  serverSocket.epoll_fd = epoll_fd;

  __uint32_t event = (EPOLLIN | EPOLLET);
  Epoll::addfd(epoll_fd, serverSocket.listen_fd, event, httpData);

  // non-clocking 循环接受请求，核心逻辑，在poll里面做
  while (true) {
    // 核心是epoll_wait 返回 events
    std::vector<std::shared_ptr<HttpData>> events =
        Epoll::poll(serverSocket, 1024, -1);

    for (auto &req : events) {
      threadPool.append(
          // std::placeholders::_1 占位符
          req, std::bind(&HttpServer::do_request, this, std::placeholders::_1));
    }
    // 循环定时处理超时连接
    // 遍历完一次 events，处理一次，时间不敏感
    Epoll::timerManager.handle_expired_event();
  }
}

void HttpServer::do_request(std::shared_ptr<void> arg) {
  std::shared_ptr<HttpData> sharedHttpData =
      std::static_pointer_cast<HttpData>(arg);

  char buffer[BUFFERSIZE];
  bzero(buffer, BUFFERSIZE);

  int check_index = 0, read_index = 0, start_line = 0;
  ssize_t recv_data;
  HttpRequestParser::PARSE_STATE parse_state =
      HttpRequestParser::PARSE_REQUESTLINE;

  while (true) {

    // non-blocking 循环读 需要判断 errno
    recv_data = recv(sharedHttpData->clientSocket_->fd, buffer + read_index,
                     BUFFERSIZE - read_index, 0);
    if (recv_data == -1) {
      if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        // 要读到 EAGAIN
        return;
      } else {
        std::cout << "reading faild" << std::endl;
        return;
      }
    }
    // 返回值为 0表示对端关闭, 关闭定时器
    if (recv_data == 0) {
      std::cout << "connection closed by peer" << std::endl;
      sharedHttpData->closeTimer();
      sharedHttpData->clientSocket_.reset();
      break;
    }
    read_index += recv_data;

    HttpRequestParser::HTTP_CODE retcode = HttpRequestParser::parse_content(
        buffer, check_index, read_index, parse_state, start_line,
        *sharedHttpData->request_);

    if (retcode == HttpRequestParser::NO_REQUEST) {
      continue;
    }

    if (retcode == HttpRequestParser::GET_REQUEST) {
      // 检查 keep_alive选项
      auto it =
          sharedHttpData->request_->mHeaders.find(HttpRequest::Connection);
      if (it != sharedHttpData->request_->mHeaders.end()) {
        if (it->second == "keep-alive") {
          sharedHttpData->response_->setKeepAlive(true);
          // timeout=20s
          sharedHttpData->response_->addHeader("Keep-Alive",
                                               std::string("timeout=20"));
        } else {
          sharedHttpData->response_->setKeepAlive(false);
        }
      }
      header(sharedHttpData);
      getMime(sharedHttpData);
      // 文件路径
      FileState fileState = static_file(sharedHttpData, basePath.c_str());
      send(sharedHttpData, fileState);
      // 如果是keep_alive
      // sharedHttpData将会自动析构释放clientSocket，从而关闭资源
      if (sharedHttpData->response_->keep_alive()) {
        // FIXME std::cout << "重设定时器  keep_alive: " <<
        // sharedHttpData->clientSocket_->fd << std::endl;
        Epoll::modfd(sharedHttpData->epoll_fd,
                     sharedHttpData->clientSocket_->fd, Epoll::DEFAULT_EVENTS,
                     sharedHttpData);
        Epoll::timerManager.addTimer(sharedHttpData,
                                     TimerManager::DEFAULT_TIME_OUT);
      }

    } else {
      // Bad Request
      // 应该关闭定时器,(其实定时器已经关闭,在每接到一个新的数据时)
      std::cout << "Bad Request" << std::endl;
    }
  }
}

void HttpServer::header(std::shared_ptr<HttpData> httpData) {
  if (httpData->request_->mVersion == HttpRequest::HTTP_11) {
    httpData->response_->setVersion(HttpRequest::HTTP_11);
  } else {
    httpData->response_->setVersion(HttpRequest::HTTP_10);
  }
  httpData->response_->addHeader("Server", " WebServer");
}

// 获取Mime 同时设置path到response
void HttpServer::getMime(std::shared_ptr<HttpData> httpData) {
  std::string filepath = httpData->request_->mUri;
  std::string mime;
  int pos;
  //    std::cout << "uri: " << filepath << std::endl;
  if ((pos = filepath.rfind('?')) != std::string::npos) {
    filepath.erase(filepath.rfind('?'));
  }

  if (filepath.rfind('.') != std::string::npos) {
    mime = filepath.substr(filepath.rfind('.'));
  }
  decltype(Mime_map)::iterator it;

  if ((it = Mime_map.find(mime)) != Mime_map.end()) {
    httpData->response_->setMime(it->second);
  } else {
    httpData->response_->setMime(Mime_map.find("default")->second);
  }
  httpData->response_->setFilePath(filepath);
}

HttpServer::FileState
HttpServer::static_file(std::shared_ptr<HttpData> httpData,
                        const char *basepath) {
  struct stat file_stat;
  char file[strlen(basepath) + strlen(httpData->response_->filePath().c_str()) +
            1];
  strcpy(file, basepath);
  strcat(file, httpData->response_->filePath().c_str());

  // 文件不存在
  if (httpData->response_->filePath() == "/" || stat(file, &file_stat) < 0) {
    // 设置 Mime 为 html
    httpData->response_->setMime(MimeType("text/html"));
    if (httpData->response_->filePath() == "/") {
      httpData->response_->setStatusCode(HttpResponse::k200Ok);
      httpData->response_->setStatusMsg("OK");
    } else {
      httpData->response_->setStatusCode(HttpResponse::k404NotFound);
      httpData->response_->setStatusMsg("Not Found");
    }
    // 404就不需要设置filepath

    return FIlE_NOT_FOUND;
  }
  // 不是普通文件或无访问权限
  if (!S_ISREG(file_stat.st_mode)) {
    // FIXME 设置Mime 为 html
    httpData->response_->setMime(MimeType("text/html"));
    httpData->response_->setStatusCode(HttpResponse::k403forbiden);
    httpData->response_->setStatusMsg("ForBidden");
    // 403就不需要设置filepath
    std::cout << "Access file denied" << std::endl;
    return FILE_FORBIDDEN;
  }

  httpData->response_->setStatusCode(HttpResponse::k200Ok);
  httpData->response_->setStatusMsg("OK");
  httpData->response_->setFilePath(file);
  //    std::cout << "文件存在 - ok" << std::endl;
  return FILE_OK;
}

void HttpServer::send(std::shared_ptr<HttpData> httpData, FileState fileState) {
  char header[BUFFERSIZE];
  bzero(header, '\0');
  const char *internal_error = "Internal Error";
  struct stat file_stat;
  httpData->response_->appenBuffer(header);
  // 404
  if (fileState == FIlE_NOT_FOUND) {

    // 如果是 '/'开头就发送默认页
    if (httpData->response_->filePath() == std::string("/")) {
      // 现在使用测试页面
      sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
              strlen(INDEX_PAGE));
      sprintf(header, "%s%s", header, INDEX_PAGE);
    } else {
      sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
              strlen(NOT_FOUND_PAGE));
      sprintf(header, "%s%s", header, NOT_FOUND_PAGE);
    }
    ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
    return;
  }

  if (fileState == FILE_FORBIDDEN) {
    sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
            strlen(FORBIDDEN_PAGE));
    sprintf(header, "%s%s", header, FORBIDDEN_PAGE);
    ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
    return;
  }
  // 获取文件状态
  if (stat(httpData->response_->filePath().c_str(), &file_stat) < 0) {
    sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
            strlen(internal_error));
    sprintf(header, "%s%s", header, internal_error);
    ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
    return;
  }

  int filefd = ::open(httpData->response_->filePath().c_str(), O_RDONLY);
  // 内部错误
  if (filefd < 0) {
    std::cout << "打开文件失败" << std::endl;
    sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
            strlen(internal_error));
    sprintf(header, "%s%s", header, internal_error);
    ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
    close(filefd);
    return;
  }

  sprintf(header, "%sContent-length: %ld\r\n\r\n", header, file_stat.st_size);
  ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
  void *mapbuf =
      mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, filefd, 0);
  ::send(httpData->clientSocket_->fd, mapbuf, file_stat.st_size, 0);
  munmap(mapbuf, file_stat.st_size);
  close(filefd);
  return;
err:
  sprintf(header, "%sContent-length: %zu\r\n\r\n", header,
          strlen(internal_error));
  sprintf(header, "%s%s", header, internal_error);
  ::send(httpData->clientSocket_->fd, header, strlen(header), 0);
  return;
}
