#pragma once
#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
namespace strtools {
    template<typename ... Args>
    inline std::string format(const std::string& formatStr, Args ... args) {
        std::size_t size = (std::size_t) std::snprintf(nullptr, 0, formatStr.c_str(), args ... ) + 1; // Extra space for '\0'
        std::unique_ptr<char[]> buf {new char[size]};
        std::snprintf(buf.get(), size, formatStr.c_str(), args ... );
        return std::string{buf.get(), buf.get() + size - 1}; // We don't want the '\0' inside
    }
    inline std::vector<std::string> split(std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::stringstream ss {s};
        std::string item;
        while(std::getline(ss, item, delim))
        {
            tokens.push_back(item);
        }
        return tokens;
    }
    inline std::vector<std::string> words(std::string s) {
        std::istringstream iss (s);
        std::vector<std::string> tokens;
        std::copy(std::istream_iterator<std::string>(iss),
            std::istream_iterator<std::string>(),
            std::back_inserter(tokens));
        return tokens;
    }
}