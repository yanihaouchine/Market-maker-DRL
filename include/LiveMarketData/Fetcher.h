#pragma once

#include <string>

std::string FetchBinanceOrderbook(const std::string &symbol, int limit = 20);
