#pragma once

#include <string>

std::string &trim(std::string &s);
int setnonblocking(int fd);
void handle_for_sigpipe();
int check_base_path(char *basePath);
