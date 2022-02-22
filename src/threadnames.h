// Copyright (c) 2018-2022 The Bitcoin Core developers
// Copyright (c)      2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREADNAMES_H
#define BITCOIN_THREADNAMES_H

#include <string>
#include <ctpl.h>

namespace ctpl { class thread_pool; }

namespace util {
//! Rename a thread both in terms of an internal (in-memory) name
//! as well as its system thread name.
//! @note Do not call this for the main thread, as this will interfere with
//! UNIX utilities such as top and killall. Use ThreadSetInternalName instead.
void ThreadRename(const char* name);

std::string GetThreadName();

//! Set the internal (in-memory) name of the current thread only.
void ThreadSetInternalName(const char* name);

//! Get the thread's internal (in-memory)
//! name used, e.g. for identification in logging.
const std::string& ThreadGetInternalName();

void RenameThreadPool(ctpl::thread_pool& tp, const char* baseName);

} // namespace util

#endif // BITCOIN_THREADNAMES_H
