// Copyright 2020 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_BLSELEMENTS_HPP_
#define SRC_BLSELEMENTS_HPP_

extern "C" {
#include "relic.h"
}
#include "relic_conf.h"
#include "util.hpp"

#if defined GMP && ARITH == GMP
#include <gmp.h>
#endif

#include <utility>

namespace bls {
class G1Element;
class G2Element;
class GTElement;

class G1Element {
public:
    static const size_t SIZE = 48;

    G1Element() {
        g1_set_infty(p);
    }

    static G1Element FromBytes(Bytes bytes, bool fLegacy = false);
    static G1Element FromBytesUnchecked(Bytes bytes, bool fLegacy = false);
    static G1Element FromByteVector(const std::vector<uint8_t> &bytevec, bool fLegacy = false);
    static G1Element FromNative(const g1_t element);
    static G1Element FromMessage(const std::vector<uint8_t> &message,
                                 const uint8_t *dst,
                                 int dst_len);
    static G1Element FromMessage(Bytes message,
                                 const uint8_t* dst,
                                 int dst_len);
    static G1Element Generator();

    bool IsValid() const;
    void CheckValid() const;
    void ToNative(g1_t output) const;
    G1Element Negate() const;
    GTElement Pair(const G2Element &b) const;
    uint32_t GetFingerprint(bool fLegacy = false) const;
    std::vector<uint8_t> Serialize(bool fLegacy = false) const;
    G1Element Copy();

    friend bool operator==(const G1Element &a, const G1Element &b);
    friend bool operator!=(const G1Element &a, const G1Element &b);
    friend std::ostream &operator<<(std::ostream &os, const G1Element &s);
    friend G1Element& operator+=(G1Element& a, const G1Element& b);
    friend G1Element operator+(const G1Element &a, const G1Element &b);
    friend G1Element operator*(const G1Element &a, const bn_t &k);
    friend G1Element operator*(const bn_t &k, const G1Element &a);
    friend GTElement operator&(const G1Element &a, const G2Element &b);

private:
    g1_t p;
};

class G2Element {
public:
    static const size_t SIZE = 96;

    G2Element() {
        g2_set_infty(q);
    }

    static G2Element FromBytes(Bytes bytes, bool fLegacy = false);
    static G2Element FromBytesUnchecked(Bytes bytes, bool fLegacy = false);
    static G2Element FromByteVector(const std::vector<uint8_t> &bytevec, bool fLegacy = false);
    static G2Element FromNative(const g2_t element);
    static G2Element FromMessage(const std::vector<uint8_t>& message,
                                 const uint8_t* dst,
                                 int dst_len,
                                 bool fLegacy = false);
    static G2Element FromMessage(Bytes message,
                                 const uint8_t* dst,
                                 int dst_len,
                                 bool fLegacy = false);
    static G2Element Generator();

    bool IsValid() const;
    void CheckValid() const;
    void ToNative(g2_t output) const;
    G2Element Negate() const;
    GTElement Pair(const G1Element &a) const;
    std::vector<uint8_t> Serialize(bool fLegacy = false) const;
    G2Element Copy();

    friend bool operator==(G2Element const &a, G2Element const &b);
    friend bool operator!=(G2Element const &a, G2Element const &b);
    friend std::ostream &operator<<(std::ostream &os, const G2Element &s);
    friend G2Element& operator+=(G2Element& a, const G2Element& b);
    friend G2Element operator+(const G2Element &a, const G2Element &b);
    friend G2Element operator*(const G2Element &a, const bn_t &k);
    friend G2Element operator*(const bn_t &k, const G2Element &a);

private:
    g2_t q;
};

class GTElement {
public:
    static const size_t SIZE = 384;
    static GTElement FromBytes(Bytes bytes);
    static GTElement FromBytesUnchecked(Bytes bytes);
    static GTElement FromByteVector(const std::vector<uint8_t> &bytevec);
    static GTElement FromNative(const gt_t *element);
    static GTElement Unity();  // unity

    void Serialize(uint8_t *buffer) const;
    std::vector<uint8_t> Serialize() const;

    friend bool operator==(GTElement const &a, GTElement const &b);
    friend bool operator!=(GTElement const &a, GTElement const &b);
    friend std::ostream &operator<<(std::ostream &os, const GTElement &s);
    friend GTElement operator*(GTElement &a, GTElement &b);
    GTElement &operator=(const GTElement &rhs);

private:
    gt_t r;
    GTElement() {}
};

}  // end namespace bls

#endif  // SRC_BLSELEMENTS_HPP_
