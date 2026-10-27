// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/receipt.h>

#include <cstring>

namespace evm {

CEvmLog ConvertHostLog(const CEvmHost::Log& hostLog)
{
    CEvmLog out;
    std::memcpy(out.address.begin(), hostLog.address.bytes, 20);
    out.topics.reserve(hostLog.topics.size());
    for (const auto& t : hostLog.topics) {
        uint256 topic;
        std::memcpy(topic.begin(), t.bytes, 32);
        out.topics.push_back(topic);
    }
    out.data = hostLog.data;
    return out;
}

} // namespace evm
