// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRING_H
#define BITCOIN_STRING_H

#include <attributes.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

[[nodiscard]] inline std::string TrimString(const std::string &str, const std::string &pattern = " \f\n\r\t\v") {
    std::string::size_type front = str.find_first_not_of(pattern);
    if (front == std::string::npos) {
        return std::string();
    }
    std::string::size_type end = str.find_last_not_of(pattern);
    return str.substr(front, end - front + 1);
}

/**
 * Join a list of items
 *
 * @param list       The list to join
 * @param separator  The separator
 * @param unary_op   Apply this operator to each item in the list
 */
template<typename T, typename BaseType, typename UnaryOp>
auto Join(const std::vector <T> &list, const BaseType &separator, UnaryOp unary_op)
->

decltype(unary_op(list.at(0))
)
{
decltype(unary_op(list.at(0))
)
ret;
for (
size_t i = 0;
i<list.

size();

++i) {
if (i > 0) ret +=
separator;
ret +=
unary_op(list
.
at(i)
);
}
return
ret;
}

template<typename T>
T Join(const std::vector <T> &list, const T &separator) {
    return Join(list, separator, [](const T &i) { return i; });
}

// Explicit overload nedded for c_str arguments, which would otherwise
// cause a substitution failure in the template abovce
inline std::string Join(const std::vector <std::string> &list, const std::string &separator) {
    return Join<std::string>(list, separator);
}

[[nodiscard]] inline std::string RemovePrefix(const std::string &str, const std::string &prefix) {
    if (str.substr(0, prefix.size()) == prefix) {
        return str.substr(prefix.size());
    }
    return str;
}

/**
 * Locale-independent version of std::to_string
 */
template<typename T>
std::string ToString(const T &t) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << t;
    return oss.str();
}

/**
 * Check if a string does not contain any embedded NUL (\0) characters
 */
[[nodiscard]] inline bool ValidAsCString(const std::string &str)

noexcept
{
return str.

size()

==
strlen(str
.

c_str()

);
}

#endif // BITCOIN_STRENCODINGS_H