Raptoreum Core staging tree 0.17
===========================

|CI|master|develop|
|-|-|-|

The name Raptoreum is derived from the Victoria term for a bird of prey and ium/eum place for a pertaining to in this case birds of prey. The name comes from the team’s extensive experience in the security field with top level skills covering all aspects of it. This is a unique strength in the crypto community and will leverage well into a successful project.

Introduction
Raptoreum began as the fairly simple idea, introducing smart contracts which would allow on chain, trustless transfers (goodbye centralized marketplaces) on the Ravencoin codebase, however with the automation of assets and RTM (Raptoreum).
The project has quickly evolved, adding innovative features that not only further expand the asset layer, but also introduce features that could help other Blockchain projects. Ravencoin unfortunately suffered several serious breaches of its asset layer so that code base has been abandoned by us. We are currently working with and building on Dash code expending its capabilities significantly.

Raptoreum is now a code fork of Dash and inherits current and optionally future features such as chain locks, oracles etc. We are further expanding capabilities by adding the following features:
A)	The deployment of a unique asset layer.
B)	The option to lock X amount of coins or assets into a special transaction. Coins are unlocked at a block number of choice or timestamp.
C)	Trustless on chain transfers of assets and native coins VIA Smart Contracts.
D)	Integrating, developing and deploying a VM protocol that would allow for smart contracts in 4 major programming languages as opposed to the situation with Ethereum being limited to solidity.

These additional features open up the power and ease of use of Raptoreum’s features to a greater variety of industries under the Distributed Application umbrella. DAPP's are a critical part of driving widespread adoption as recently seen with DEFI and Raptoreum hopes to help provide further alternatives and possibilities for DAPP developers particularly in the field of opening up new programming languages for DAPP and contract development.

It is one of our goals to not only innovate ideas for Raptoreum but to contribute back to the general crypto community with open-source features that anybody can use to help their Blockchain projects succeed.


License
-------

Raptoreum Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is meant to be stable. Development is done in separate branches.
[Tags](https://github.com/raptoreum/raptoreum/tags) are created to indicate new official,
stable release versions of Raptoreum Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python, that are run automatically on the build server.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

The Travis CI system makes sure that every pull request is built for Windows, Linux, and OS X, and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.
