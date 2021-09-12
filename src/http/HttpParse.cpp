#include "../../include/HttpParse.h"
#include "../../include/HttpRequest.h"
#include "../../include/Util.h"

#include <algorithm>
#include <string.h>

std::unordered_map<std::string, HttpRequest::HTTP_HEADER>
    HttpRequest::header_map = {
        {"HOST", HttpRequest::Host},
        {"USER-AGENT", HttpRequest::User_Agent},
        {"CONNECTION", HttpRequest::Connection},
        {"ACCEPT-ENCODING", HttpRequest::Accept_Encoding},
        {"ACCEPT-LANGUAGE", HttpRequest::Accept_Language},
        {"ACCEPT", HttpRequest::Accept},
        {"CACHE-CONTROL", HttpRequest::Cache_Control},
        {"UPGRADE-INSECURE-REQUESTS", HttpRequest::Upgrade_Insecure_Requests}};

// 解析其中一行内容, buffer[checked_index, read_index)
// STL 左闭右开风格
// checked_index 是需要解析的第一个字符，read_index
// 是已经读取数据末尾的下一个字符
HttpRequestParser::LINE_STATE HttpRequestParser::parse_line(char *buffer,
                                                            int &checked_index,
                                                            int &read_index) {
  char temp;
  for (; checked_index < read_index; checked_index++) {

    temp = buffer[checked_index];
    if (temp == CR) {
      // 到末尾，需要读入
      if (checked_index + 1 == read_index)
        return LINE_MORE;
      // 完整 "\r\n"
      if (buffer[checked_index + 1] == LF) {
        buffer[checked_index++] = LINE_END;
        buffer[checked_index++] = LINE_END;
        return LINE_OK;
      }

      return LINE_BAD;
    }
  }
  // 需要读入更多
  return LINE_MORE;
}

// 解析请求行
HttpRequestParser::HTTP_CODE
HttpRequestParser::parse_requestline(char *line, PARSE_STATE &parse_state,
                                     HttpRequest &request) {
  char *url = strpbrk(line, " \t");
  if (!url) {
    return BAD_REQUEST;
  }

  // 分割 method 和 url
  *url++ = '\0';

  char *method = line;

  if (strcasecmp(method, "GET") == 0) {
    request.mMethod = HttpRequest::GET;
  } else if (strcasecmp(method, "POST") == 0) {
    request.mMethod = HttpRequest::POST;
  } else if (strcasecmp(method, "PUT") == 0) {
    request.mMethod = HttpRequest::PUT;
  } else {
    return BAD_REQUEST;
  }
  // size_t strspn(const char *str1, const char *str2)
  //该函数返回 str1 中第一个不在字符串 str2 中出现的字符下标。

  //  char *strpbrk(const char *str1, const char *str2)
  // 该函数返回 str1 中第一个(仅仅是第一个字符即可）匹配字符串 str2
  // 中字符的字符数，如果未找到字符则返回 NULL。 用在这里是查找 \t
  url += strspn(url, " \t");
  char *version = strpbrk(url, " \t");
  if (!version) {
    return BAD_REQUEST;
  }
  *version++ = '\0';
  version += strspn(version, " \t");

  // HTTP/1.1 后面可能还存在空白字符
  if (strncasecmp("HTTP/1.1", version, 8) == 0) {
    request.mVersion = HttpRequest::HTTP_11;
  } else if (strncasecmp("HTTP/1.0", version, 8) == 0) {
    request.mVersion = HttpRequest::HTTP_10;
  } else {
    return BAD_REQUEST;
  }
  // strncasecmp()用来比较参数s1 和s2
  // 字符串前n个字符，比较时会自动忽略大小写的差异。
  // 相同返回零
  if (strncasecmp(url, "http://", 7) == 0) {
    url += 7;
    // 查找第一次出现位置
    url = strchr(url, '/');
  } else if (strncasecmp(url, "/", 1) == 0) {
    // 等同空字符串，为了提示作用
    PASS;
  } else {
    return BAD_REQUEST;
  }

  if (!url || *url != '/') {
    return BAD_REQUEST;
  }
  request.mUri = std::string(url);
  // 分析头部字段
  parse_state = PARSE_HEADER;
  return NO_REQUEST;
}

// 分析头部字段
HttpRequestParser::HTTP_CODE
HttpRequestParser::parse_headers(char *line, PARSE_STATE &parse_state,
                                 HttpRequest &request) {

  if (*line == '\0') {
    if (request.mMethod == HttpRequest::GET) {
      return GET_REQUEST;
    }
    parse_state = PARSE_BODY;
    return NO_REQUEST;
  }

  // char key[20]曾被缓冲区溢出, value[100]也被 chrome的 user-agent 溢出
  // valgrind *** stack smashing detected ***: terminated
  // 提示在parse_headers出错，可能是用来存请求的kv分配小了，扩大了，没有问题了
  // 在程序函数中，数组越界访问，在程序运行时没出现问题，但当函数return的时候就会出现上面的错误
  // 再用 valgrind跑一遍，一切正常
  char key[100], value[300];

  // 有些 value里也包含了':'符号
  // sscanf 配合正则表达式，format里面表示，%[^:]
  // 表示匹配冒号之外的符号，三个组合一起，表示匹配一个结构
  sscanf(line, "%[^:]:%[^:]", key, value);

  decltype(HttpRequest::header_map)::iterator it;
  std::string key_s(key);
  std::transform(key_s.begin(), key_s.end(), key_s.begin(), ::toupper);
  std::string value_s(value);
  // 仅支持http，不能 upgrade
  //    if (key_s == std::string("UPGRADE-INSECURE-REQUESTS")) {
  //        return NO_REQUEST;
  //    }

  if ((it = HttpRequest::header_map.find(trim(key_s))) !=
      (HttpRequest::header_map.end())) {
    request.mHeaders.insert(std::make_pair(it->second, trim(value_s)));
  } else {
    std::cout << "Header no support: " << key << " : " << value << std::endl;
  }

  return NO_REQUEST;
}

// 解析body
HttpRequestParser::HTTP_CODE
HttpRequestParser::parse_body(char *body, HttpRequest &request) {
  request.mContent = body;
  return GET_REQUEST;
}

// http 请求入口
HttpRequestParser::HTTP_CODE
HttpRequestParser::parse_content(char *buffer, int &check_index,
                                 int &read_index,
                                 HttpRequestParser::PARSE_STATE &parse_state,
                                 int &start_line, HttpRequest &request) {

  LINE_STATE line_state = LINE_OK;
  HTTP_CODE retcode = NO_REQUEST;
  while ((line_state = parse_line(buffer, check_index, read_index)) ==
         LINE_OK) {
    char *temp = buffer + start_line; // 这一行在buffer中的起始位置
    start_line = check_index;         // 下一行起始位置

    switch (parse_state) {
    case PARSE_REQUESTLINE: {
      retcode = parse_requestline(temp, parse_state, request);
      if (retcode == BAD_REQUEST)
        return BAD_REQUEST;

      break;
    }

    case PARSE_HEADER: {
      retcode = parse_headers(temp, parse_state, request);
      if (retcode == BAD_REQUEST) {
        return BAD_REQUEST;
      } else if (retcode == GET_REQUEST) {
        return GET_REQUEST;
      }
      break;
    }

    case PARSE_BODY: {
      retcode = parse_body(temp, request);
      if (retcode == GET_REQUEST) {
        return GET_REQUEST;
      }
      return BAD_REQUEST;
    }
    default:
      return INTERNAL_ERROR;
    }
  }
  if (line_state == LINE_MORE) {
    return NO_REQUEST;
  } else {
    return BAD_REQUEST;
  }
}
