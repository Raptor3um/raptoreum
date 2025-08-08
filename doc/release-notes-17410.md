Command-line options
--------------------

-  The `-debug=db` logging category has been renamed to `-debug=walletdb`, to distinguish it from `coindb`.
   `-debug=db` has been deprecated and will be removed in the next major release.

RPC
---

-  The `listassets` RPC command now supports an optional `mine` parameter (default: false). When set to true, only assets owned by the current wallet are returned.
