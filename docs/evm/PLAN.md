# Plan de integración EVM nativo en Raptoreum (RTM-EVM)

> Repositorio base: `C:\Users\JS_UG\Documents\Raptoreum\raptoreum\`
> Estrategia elegida: **Camino C** — EVM integrado en mainchain vía hard-fork, con Account Abstraction Layer (estilo Qtum).
> Horizonte: **~26 meses** (24 base + ~6-8 semanas de las decisiones de revisión D1-D3), 8 fases. Punto de no retorno: Fase 6 (testnet pública).
>
> **Plan revisado tras `/plan-eng-review` — decisiones D1-D6 incorporadas (ver sección "Decisiones de revisión" al final).**

---

## Context

Raptoreum hoy es un fork de Dash con dos extensiones propias notables:
- **Smart Assets** (`src/assets/`) — creación de tokens/NFTs nativos via tx types especiales.
- **GhostRider PoW** — algoritmo CPU-only, ASIC-resistente.

Pero carece de programabilidad: no hay VM, por tanto no hay DeFi, ni AMMs, ni lending, ni infraestructura componible. Todo lo que mueve volumen real en cripto vive donde hay contratos.

Este plan integra una EVM completa **dentro** del mainchain (no sidechain), reusando agresivamente la infraestructura existente: el framework de tx especiales heredado de Dash (`src/evo/specialtx.cpp`) para introducir tx types EVM, el patrón `CAssetsCache` para el state trie, y — la diferenciación clave — los quórums LLMQ y la lista determinística de masternodes como **precompiles nativos**, dándole a RTM-EVM 4 superpoderes que ninguna otra L1 EVM tiene.

**Qué buscamos:**
1. Solidity funcional sobre RTM con compatibilidad MetaMask out-of-the-box.
2. Smart Assets accesibles como ERC-20 desde contratos (sin wrapping).
3. Oráculos threshold-firmados nativos vía LLMQ (Chainlink-killer).
4. Finalidad de 2 segundos para dApps via ChainLocks expuestos a EVM.
5. Una sola cadena, una sola liquidez, un solo modelo de seguridad.

---

## Arquitectura global

```
┌────────────────────────────────────────────────────────────────────────┐
│                    raptoreumd (un solo binario)                        │
│                                                                        │
│  ┌─────────────────────┐         ┌───────────────────────────────┐     │
│  │   UTXO subsystem    │         │   EVM subsystem (NUEVO)       │     │
│  │                     │         │                               │     │
│  │  CCoinsViewCache    │ ◄─AAL─► │  CEvmStateCache               │     │
│  │  (chainstate/)      │         │  (evmstate/)                  │     │
│  │                     │         │                               │     │
│  │  - Coinbase/PoW     │         │  - Account state trie         │     │
│  │  - Smart Assets ────┼─precompile 0a01──┐                      │     │
│  │  - DMN/LLMQ ────────┼─precompiles 0a02-04                     │     │
│  │  - InstantSend      │         │  - Storage trie por contrato  │     │
│  │  - ChainLocks       │         │  - Logs / receipts            │     │
│  └─────────────────────┘         └───────────────────────────────┘     │
│           ▲                                       ▲                    │
│           │                                       │                    │
│       OP_NORMAL                       OP_EVMCREATE/OP_EVMCALL/         │
│   (existing scripts)                       OP_EVMSPEND                 │
│                                                                        │
│           ▼                                       ▼                    │
│  ┌──────────────────────────────────────────────────────────┐          │
│  │  ConnectBlock (validation.cpp:2075) — atomic per block   │          │
│  │  ├─ UpdateCoins (UTXO)                                   │          │
│  │  └─ ApplyEvmTx  (EVM state)  ← NUEVO, mismo lock         │          │
│  └──────────────────────────────────────────────────────────┘          │
│                                                                        │
│  ┌────────────────────────────────────────────────────────┐            │
│  │  RPC: namespace eth_* en puerto 8545 (NUEVO)           │            │
│  │  + RPC tradicional (getbalance, etc.) en puerto actual │            │
│  └────────────────────────────────────────────────────────┘            │
└────────────────────────────────────────────────────────────────────────┘
```

**Decisiones de diseño no negociables:**

| Decisión | Valor | Razón |
|---|---|---|
| Token de gas | RTM (no token nuevo) | Una sola liquidez. Ver Ethereum vs. BNB para entender el split. |
| Modelo de fees | EIP-1559 (base fee + tip) | Base fee **quemado** → presión deflacionaria ligada al uso real. |
| Motor EVM | [evmone](https://github.com/ethereum/evmone) via [EVMC](https://github.com/ethereum/evmc) | C++ puro (igual que `raptoreumd`), el más rápido del mercado. EVMC permite cambiar de motor sin tocar consenso. |
| Espacios de direcciones | Separados (Qtum-style) | base58 P2PKH para UTXO, `0x...` para contratos. AAL traduce. |
| Replay protection | EIP-155 (chainId) | MetaMask + ethers.js + viem funcionan sin parches. |
| Activación | Soft-fork BIP9 + masternode signaling | Reusar `src/update/update.h` y `Updates().IsAssetsActive()` como precedente. |

---

## Mapa de archivos críticos del repo (descubierto en exploración)

Estos son los archivos que tocaremos. Marcados con `★` los que requieren cambios estructurales mayores.

### Framework de tx especiales (a reusar)
- `src/primitives/transaction.h` líneas 16–28 ★ — Enum `TRANSACTION_*` (extender con tipos EVM 11–13).
- `src/evo/specialtx.h` — Interfaz `CheckSpecialTx()`, `ProcessSpecialTxsInBlock()`, template `GetTxPayload<T>()`.
- `src/evo/specialtx.cpp` líneas 18–56 ★ — Dispatcher switch por `tx.nType` (añadir cases EVM).
- `src/consensus/tx_verify.cpp` líneas 50–94 ★ — Fee validation por tipo (extender con tipos EVM).

### Smart Assets (a reusar como modelo Y como precompile target)
- `src/assets/assetstype.h` — `CAssetTransfer`, builders.
- `src/assets/assets.h` líneas 39–233 — `CAssetsCache` (modelo a copiar para `CEvmStateCache`).
- `src/assets/assetsdb.cpp` líneas 15–87 — Schema LevelDB con prefijos 'A','B','C','D' (modelo para `evmstatedb`).
- `src/evo/providertx.cpp` líneas 125–219 — `CheckNewAssetTx()`, etc. (patrón de validación).

### Activation soft-fork
- `src/update/update.h` líneas 16–31 — `enum EUpdate`, `EUpdateState` (Voting→LockedIn→Active).
- `src/chainparams.h` línea 141 — `UpdateManager& Updates()`.
- `src/chainparams.cpp` ★ — Añadir activation params para `UPDATE_EVM`.

### LLMQ / ChainLocks / DMN (target de precompiles)
- `src/llmq/quorums_signing.h` líneas 258–284 — `AsyncSignIfMember()`, `VerifyRecoveredSig()`, `GetRecoveredSigForId()`.
- `src/llmq/quorums_signing.h` líneas 79–120 — `CRecoveredSig` (id, msgHash, sig BLS 96 bytes).
- `src/llmq/quorums_chainlocks.h` línea 153 — `HasChainLock(nHeight, blockHash)`.
- `src/llmq/quorums_instantsend.h` línea 364 — `IsLocked(txHash)`.
- `src/evo/deterministicmns.h` línea 676 — `CDeterministicMNManager::GetListAtChainTip()`.
- Tipos de quórum: `LLMQ_50_60` (InstantSend), `LLMQ_400_85` (ChainLocks/críticos), `LLMQ_100_67` (Platform).

### Script (nuevos opcodes)
- `src/script/script.h` líneas 47–195 ★ — Enum `opcodetype`. Último usado: `OP_ASSET_ID = 0xbc`. Disponibles: `0xbd`, `0xbe`, `0xbf`.
- `src/script/interpreter.cpp` línea 270+ ★ — `EvalScript()` switch principal.

### Validation pipeline
- `src/validation.cpp` línea 2075 ★ — `CChainState::ConnectBlock()` (extender con `evmStateCache`).
- `src/validation.cpp` línea 1635 ★ — `DisconnectBlock()` (revert EVM state).
- `src/validation.cpp` línea 1352 — `UpdateCoins()` (referencia, no se modifica).
- `src/coins.h` línea 253 — `CCoinsViewCache` (modelo de cache UTXO).
- `src/undo.h` línea 51 ★ — `CTxUndo` / `CBlockUndo` (extender con `vEvmStateUndo`).

### RPC framework
- `src/rpc/server.h` líneas 95–138 — `CRPCCommand`, dispatcher.
- `src/rpc/register.h` líneas 13–57 ★ — Añadir `RegisterEthereumRPCCommands()`.
- `src/httprpc.cpp/h` ★ — Segundo HTTP listener para puerto 8545 (MetaMask).
- `src/rpc/blockchain.cpp` líneas 2889–2934 — Patrón de tabla de comandos (modelo).

### Database
- `src/dbwrapper.h` líneas 46–80 — `CDBBatch`, `CDBWrapper` (heredar para `EvmStateDB`).

### Wallet / Qt
- `src/wallet/wallet.cpp` ★ — `CWallet::CreateTransaction()` (extender con `CreateEvmTransaction()`).
- `src/qt/transactionrecord.h` líneas 88–106 ★ — Enum `Type` (añadir `DeployContract`, `CallContract`).
- `src/wallet/rpcwallet.cpp` ★ — Wallet RPC commands para EVM signing.

---

## Fases del plan

### Fase 0 — Spike técnico (2 semanas, 1 ingeniero) ⚡

**Objetivo:** validar que evmone compila y linkea dentro de `raptoreumd` sin romper la build. Cero cambios de consenso.

**Entregables:**
1. evmone añadido como submódulo en `depends/` (igual que se hace con BLS, secp256k1, etc.).
2. `Makefile.am` actualizado para linkear `libevmone.a`.
3. Comando RPC nuevo (no consensus): `evm_executeReadOnly(bytecode_hex, calldata_hex)` que ejecuta bytecode contra estado vacío y devuelve el return data + gas usado.
4. Test unitario en `src/test/evm_smoke_tests.cpp` con 3 casos: contrato vacío, suma de dos uint256, llamada a opcode KECCAK256.

**Archivos nuevos:**
- `depends/packages/evmone.mk`
- `src/evm/smoke.cpp`, `src/evm/smoke.h`
- `src/test/evm_smoke_tests.cpp`

**Verificación:**
```bash
cd src && make -j8                          # Compila sin errores
src/test/test_raptoreum --run_test=evm_smoke_tests   # 3/3 pasa
src/raptoreumd -regtest -daemon
src/raptoreum-cli -regtest evm_executeReadOnly "6005600401" "0x"   # Devuelve 9
```

**Por qué importa:** si esto no compila limpio en 2 semanas, el resto del plan está en duda. Es la inversión más barata para detectar showstoppers.

---

### Fase 1 — AAL: Account Abstraction Layer (3 meses, 2 ingenieros)

**Objetivo:** introducir los nuevos tx types y opcodes EVM **sin ejecución todavía**. Solo el "andamio" de consenso para que la red sepa qué es una tx EVM, aunque aún no haga nada con ella.

#### 1.1 Nuevos tx types (semana 1–2)

Editar `src/primitives/transaction.h` líneas 16–28:

```cpp
enum {
    TRANSACTION_NORMAL = 0,
    TRANSACTION_PROVIDER_REGISTER = 1,
    // ... (existing) ...
    TRANSACTION_NEW_ASSET = 8,
    TRANSACTION_UPDATE_ASSET = 9,
    TRANSACTION_MINT_ASSET = 10,
    TRANSACTION_EVM_DEPLOY = 11,    // NUEVO
    TRANSACTION_EVM_CALL = 12,      // NUEVO
    TRANSACTION_EVM_SPEND = 13,     // NUEVO (mover RTM de cuenta EVM a UTXO)
};
```

#### 1.2 Payload structures (semana 2–3)

Crear `src/evm/evmtx.h` con tres structs siguiendo patrón de `CAssetTransfer`:

```cpp
struct CEvmDeployTx {
    static constexpr uint16_t CURRENT_VERSION = 1;
    uint16_t nVersion;
    std::vector<uint8_t> code;       // bytecode
    uint64_t gasLimit;
    uint64_t maxFeePerGas;           // EIP-1559
    uint64_t maxPriorityFeePerGas;
    uint256 senderHash;              // hash de pubkey EVM (no UTXO addr)
    SERIALIZE_METHODS(CEvmDeployTx, obj) { ... }
};

struct CEvmCallTx {
    uint16_t nVersion;
    uint160 to;                       // contract address (20 bytes EVM-style)
    uint64_t value;                   // RTM en weis (1 RTM = 10^18 weis)
    std::vector<uint8_t> data;        // calldata
    uint64_t gasLimit;
    uint64_t maxFeePerGas;
    uint64_t maxPriorityFeePerGas;
    uint256 senderHash;
    SERIALIZE_METHODS(CEvmCallTx, obj) { ... }
};

struct CEvmSpendTx {
    uint16_t nVersion;
    uint160 from;                     // EVM account
    CScript outputScript;             // UTXO destination
    uint64_t amount;                  // satoshis
    SERIALIZE_METHODS(CEvmSpendTx, obj) { ... }
};
```

#### 1.3 Validación específica (semana 3–4)

Crear `src/evm/evmtx.cpp` con `CheckEvmDeployTx()`, `CheckEvmCallTx()`, `CheckEvmSpendTx()`. Por ahora solo validan estructura y firmas, **no ejecutan**.

#### 1.4 Dispatcher (semana 4)

Editar `src/evo/specialtx.cpp` líneas 18–56:

```cpp
bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state) {
    // ... existing ...
    switch (tx.nType) {
        // existing cases for assets, ProRegTx, etc.
        case TRANSACTION_EVM_DEPLOY:
            return CheckEvmDeployTx(tx, pindexPrev, state);
        case TRANSACTION_EVM_CALL:
            return CheckEvmCallTx(tx, pindexPrev, state);
        case TRANSACTION_EVM_SPEND:
            return CheckEvmSpendTx(tx, pindexPrev, state);
    }
}
```

Idem en `ProcessSpecialTx()` y `UndoSpecialTx()`.

#### 1.5 Nuevos opcodes (semana 5–6)

Editar `src/script/script.h`:

```cpp
OP_ASSET_ID         = 0xbc,
OP_EVMCREATE        = 0xbd,    // Crear contrato (acompañado de EVM_DEPLOY tx)
OP_EVMCALL          = 0xbe,    // Llamar contrato (acompañado de EVM_CALL tx)
OP_EVMSPEND         = 0xbf,    // Salida desde cuenta EVM a UTXO
MAX_OPCODE          = OP_EVMSPEND,
```

En `src/script/interpreter.cpp` línea 270+, añadir cases en `EvalScript()`. Por ahora simplemente marcan la tx como tx EVM válida (la ejecución viene en Fase 2).

#### 1.6 Activation params (semana 6)

Editar `src/update/update.h` y `src/chainparams.cpp`:

```cpp
enum EUpdate {
    UPDATE_DIP0001,
    UPDATE_DIP0003,
    UPDATE_ASSETS,
    UPDATE_EVM,           // NUEVO
};
```

Añadir en `Params()` los version bits y bit position para `UPDATE_EVM`. **Importante:** todo lo de EVM detrás de `Updates().IsEvmActive()` durante todo el desarrollo.

#### 1.7 Tests de regresión (semana 7–12)

- `src/test/evmtx_tests.cpp` — round-trip serialización de los 3 tx types.
- `src/test/evmtx_validation_tests.cpp` — txs malformadas son rechazadas.
- `test/functional/feature_evm_activation.py` — testnet en regtest activando EVM via signaling.

**Verificación de fase 1:**

```bash
src/raptoreumd -regtest
src/raptoreum-cli -regtest createrawtransaction '[]' '[{"data":"...","type":11}]'
# La tx se acepta en mempool en regtest con EVM activo, se rechaza en pre-EVM
```

**Estado al final de fase 1:** la red sabe qué es una tx EVM y qué opcodes EVM existen, pero ejecutarlas es no-op. Los tx types ya están reservados a nivel de consenso.

---

### Fase 2 — Ejecución EVM + State trie (3 meses, 3 ingenieros)

**Objetivo:** que las tx EVM **realmente hagan algo**. Estado persistente, gas accounting, integración en ConnectBlock.

#### 2.1 EvmStateDB (semana 1–3)

Modelar 1:1 sobre `src/assets/assetsdb.cpp`. Crear:
- `src/evm/evmstatedb.h` — clase `EvmStateDB : public CDBWrapper`.
- `src/evm/evmstatedb.cpp` — implementación.

Schema LevelDB con prefijos:
```
'A' + address(20) → CEvmAccount { nonce, balance, codeHash, storageRoot }
'C' + codeHash(32) → bytecode bytes
'S' + address(20) + storageKey(32) → storageValue(32)
'L' + blockHash(32) + txIdx(4) + logIdx(4) → CEvmLog
'R' + txid(32) → CEvmReceipt
```

#### 2.2 CEvmStateCache (semana 3–5)

Modelar sobre `CAssetsCache` (`src/assets/assets.h:39-233`). Crear `src/evm/evmstatecache.h`:

```cpp
class CEvmStateCache {
    EvmStateDB* base;
    std::map<uint160, CEvmAccount> mapAccountDirty;
    std::map<std::pair<uint160,uint256>, uint256> mapStorageDirty;
    std::vector<CEvmLog> vLogsBlock;

public:
    bool GetAccount(const uint160& addr, CEvmAccount& out);
    void SetAccount(const uint160& addr, const CEvmAccount& acc);
    bool GetStorage(const uint160& addr, const uint256& key, uint256& out);
    void SetStorage(const uint160& addr, const uint256& key, const uint256& val);
    bool Flush();         // commit a base
    void Discard();       // descarta cambios (para reorg)
};
```

#### 2.3 Integración EVMC (semana 5–8)

Crear `src/evm/host.cpp` implementando la interfaz `evmc::Host` de evmone. Conecta `CEvmStateCache` con el motor EVM:

```cpp
class RtmEvmcHost : public evmc::Host {
    CEvmStateCache& state;
    const CBlockIndex* pindexExec;

public:
    evmc::bytes32 get_storage(const evmc::address&, const evmc::bytes32&) override;
    evmc_storage_status set_storage(...) override;
    evmc::uint256be get_balance(const evmc::address&) override;
    // ... resto de la interfaz
};
```

#### 2.4 Gas accounting + EIP-1559 (semana 8–10)

- Añadir `nGasUsed`, `nGasLimit`, `nBaseFee` al `CBlockHeader` (bump de versión + soft-fork).
- Implementar fórmula EIP-1559 de ajuste de base fee bloque a bloque.
- **Base fee se quema** (no va al miner) — modificación al script de coinbase.
- Tip va al miner.

#### 2.5 ConnectBlock integration (semana 10–12)

Editar `src/validation.cpp:2075`. La función actual ya recibe `CAssetsCache* assetsCache`. Añadir paralelo:

```cpp
bool CChainState::ConnectBlock(
    const CBlock& block, CValidationState& state,
    CBlockIndex* pindex, CCoinsViewCache& view,
    const CChainParams& chainparams,
    CAssetsCache* assetsCache,
    CEvmStateCache* evmCache,        // NUEVO
    bool fJustCheck)
{
    // ... existing logic ...
    for (const CTransaction& tx : block.vtx) {
        UpdateCoins(tx, view, txundo, pindex->nHeight, assetsCache);

        if (tx.nType == TRANSACTION_EVM_DEPLOY ||
            tx.nType == TRANSACTION_EVM_CALL ||
            tx.nType == TRANSACTION_EVM_SPEND) {
            if (!ApplyEvmTx(tx, *evmCache, pindex, state)) {
                return state.Invalid(...);
            }
        }
    }
    // ...
}
```

`ApplyEvmTx()` en `src/evm/apply.cpp`:
1. Decodifica payload via `GetTxPayload<CEvmCallTx>(tx)`.
2. Construye `evmc_message`.
3. Llama `evmone_execute(host, msg)`.
4. Aplica gas refund / consume.
5. Si revert, restaura snapshot de `evmCache`.
6. Persiste logs y receipt.

#### 2.6 DisconnectBlock + undo (semana 12)

Editar `src/validation.cpp:1635`. Extender `CBlockUndo` (en `src/undo.h:51`) con:

```cpp
class CBlockUndo {
    std::vector<CTxUndo> vtxundo;
    std::vector<CEvmStateUndo> vEvmUndo;     // NUEVO
};

struct CEvmStateUndo {
    std::vector<std::pair<uint160, CEvmAccount>> accountPrev;
    std::vector<std::tuple<uint160, uint256, uint256>> storagePrev;
};
```

`DisconnectBlock()` revierte aplicando estos diffs en orden inverso.

**Verificación de fase 2:**

```bash
# regtest con EVM activo desde bloque 200
src/raptoreumd -regtest -evmactivationheight=200
# Deploy contrato simple (storage de un uint256)
src/raptoreum-cli -regtest evm_sendDeploy "<bytecode>" 1000000 1
# Verificar storage
src/raptoreum-cli -regtest evm_getStorageAt "<addr>" "0x0"
# Reorg test: minar 5 bloques alternativos en otra rama, ver que estado revierte
```

---

### Fase 3 — JSON-RPC `eth_*` namespace (2 meses, 1 ingeniero)

**Objetivo:** que MetaMask, ethers.js, viem, web3.js se conecten a un nodo RTM **sin saberlo**.

#### 3.1 Segundo HTTP listener (semana 1)

Editar `src/httprpc.cpp` para abrir un segundo listener en `-evmrpcport=8545` (default) usando el mismo framework HTTP existente. Auth opcional via `-evmrpcauth`.

#### 3.2 Comandos eth_* (semana 1–6)

Crear `src/rpc/ethereum.cpp` con tabla `CRPCCommand` siguiendo modelo de `src/rpc/blockchain.cpp:2889-2934`. Comandos prioritarios (orden = orden de implementación):

| Método | Implementación interna |
|---|---|
| `eth_chainId` | Devuelve chainId fijo (sugerido: 7373 mainnet, 7374 testnet) |
| `eth_blockNumber` | `chainActive.Tip()->nHeight` |
| `eth_getBalance(addr, blockTag)` | `evmCache->GetAccount(addr).balance` |
| `eth_getTransactionCount` | `account.nonce` |
| `eth_call` | Ejecución read-only via `RtmEvmcHost` con state snapshot |
| `eth_estimateGas` | Binary search sobre `eth_call` |
| `eth_sendRawTransaction` | Decode RLP → mapear a `CEvmCallTx` o `CEvmDeployTx` → broadcast |
| `eth_getTransactionByHash` | Lookup en mempool + chain |
| `eth_getTransactionReceipt` | Lookup en `EvmStateDB` con prefijo 'R' |
| `eth_getLogs` | Filtros sobre prefijo 'L' |
| `eth_getBlockByNumber/Hash` | Mapear `CBlock` a formato Ethereum (bloques mixtos UTXO+EVM) |
| `eth_gasPrice` / `eth_maxPriorityFeePerGas` | Median del último N bloques |
| `eth_subscribe` (WebSocket) | Para `newHeads`, `logs` — Fase 5 |

#### 3.3 RLP ↔ raptoreum tx adapter (semana 6–8)

Lo más sutil: `eth_sendRawTransaction` recibe RLP estilo Ethereum. Hay que:
1. Decodificar RLP (añadir lib RLP en `depends/`).
2. Verificar firma EIP-155.
3. Construir un `CTransaction` de Raptoreum con `nType = TRANSACTION_EVM_CALL` y payload equivalente.
4. Broadcast via mempool normal.

Esto significa que **una tx Ethereum válida es indistinguible de una tx RTM EVM** una vez en mempool.

**Verificación:**
```javascript
// Desde Node con ethers.js
const provider = new ethers.JsonRpcProvider("http://localhost:8545");
console.log(await provider.getBlockNumber());
console.log(await provider.getBalance("0x..."));
// Deploy un ERC-20 standard de OpenZeppelin
const factory = new ethers.ContractFactory(abi, bytecode, signer);
const token = await factory.deploy("Test", "TST", 1000000);
await token.waitForDeployment();   // Funciona en regtest
```

---

### Fase 4 — Precompiles RTM-nativos (2 meses, 2 ingenieros) ⭐

**Esto es la diferenciación.** Los 4 precompiles que ningún otro chain EVM tiene.

#### 4.1 Precompile `0x...0a01` — Smart Assets como ERC-20

**Interfaz Solidity expuesta (un contrato standard ERC-20 por cada Smart Asset):**

```solidity
interface IRtmAsset {
    function name() external view returns (string memory);
    function symbol() external view returns (string memory);
    function decimals() external view returns (uint8);
    function totalSupply() external view returns (uint256);
    function balanceOf(address owner) external view returns (uint256);
    function transfer(address to, uint256 value) external returns (bool);
    function transferFrom(address from, address to, uint256 value) external returns (bool);
    function approve(address spender, uint256 value) external returns (bool);
    function allowance(address owner, address spender) external view returns (uint256);
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
}
```

**Resolución:** la dirección del precompile es **derivada determinísticamente del assetId**:

```cpp
uint160 assetIdToEvmAddress(const std::string& assetId) {
    // Reservar prefijo 0x000000000000000000000000A55E7... ("ASSET" en hex)
    // Los 12 últimos bytes: hash160(assetId)
    return 0xA55E700000000000ULL << 96 | hash160(assetId);
}
```

**Implementación** en `src/evm/precompiles/asset_erc20.cpp`:
- `balanceOf(addr)` → lookup `CAssetsCache::mapAssetAddressAmount[{assetId, addr}]`.
- `transfer(to, amount)` → emite internamente una `TRANSACTION_MINT_ASSET` virtual aplicada al cache de assets en el mismo ConnectBlock.
- Eventos `Transfer` / `Approval` se persisten en log trie EVM.

**Implicación:** `Uniswap V2` puede listar pares contra Smart Assets sin wrapping. Crear un asset cuesta ~5 RTM (≈$0.X), deployar un ERC-20 en Ethereum cuesta $50–500. Diferenciador masivo para tokenización RWA.

#### 4.2 Precompile `0x...0a02` — LLMQ Threshold Oracle

**Interfaz Solidity:**

```solidity
interface ILlmqOracle {
    /// @notice Solicita firma threshold de un mensaje por un quórum
    /// @param llmqType 1=LLMQ_50_60, 2=LLMQ_400_60, 3=LLMQ_400_85, 4=LLMQ_100_67
    /// @param id identificador único de la sesión de firma
    /// @param msgHash SHA256 del mensaje
    /// @return requestId hash de la solicitud (para consultar después)
    function requestSignature(uint8 llmqType, bytes32 id, bytes32 msgHash)
        external returns (bytes32 requestId);

    /// @notice Consulta si una firma threshold está disponible
    /// @return available si hay firma recuperada
    /// @return signature firma BLS de 96 bytes (si available)
    function getSignature(uint8 llmqType, bytes32 id)
        external view returns (bool available, bytes memory signature);

    /// @notice Verifica una firma BLS threshold
    function verifySignature(
        uint8 llmqType, uint32 signedAtHeight,
        bytes32 id, bytes32 msgHash, bytes calldata signature
    ) external view returns (bool);
}
```

**Implementación** en `src/evm/precompiles/llmq_oracle.cpp`:
- `requestSignature` → `quorumSigningManager->AsyncSignIfMember(llmqType, id, msgHash)` (función en `src/llmq/quorums_signing.h:258`).
- `getSignature` → `quorumSigningManager->GetRecoveredSigForId(llmqType, id, retSig)` (línea 267).
- `verifySignature` → `CSigningManager::VerifyRecoveredSig(...)` static (línea 282), thread-safe.

**Modelo asíncrono importante:** `requestSignature` no bloquea el bloque actual (las firmas tardan ~3s para LLMQ_50_60, ~10min para LLMQ_400_85). Los contratos consumen el patrón **request-then-poll**: emiten `requestSignature`, esperan N bloques, llaman `getSignature`. Patrón estándar en oráculos descentralizados.

**Implicación:** **No necesitas Chainlink.** Tienes oráculos verificables por threshold BLS de masternodes con ~4000 nodos validando, **integrados al chain**. Ejemplos de uso: feeds de precios firmados off-chain por DMNs autorizados, randomness verificable, atestaciones cross-chain.

#### 4.3 Precompile `0x...0a03` — ChainLocks

**Interfaz Solidity:**

```solidity
interface IChainLocks {
    function isChainLocked(uint32 height, bytes32 blockHash) external view returns (bool);
    function isTxInstantLocked(bytes32 txid) external view returns (bool);
    function latestChainLockedHeight() external view returns (uint32);
}
```

**Implementación:**
- `isChainLocked` → `chainLocksHandler->HasChainLock(height, blockHash)` (`src/llmq/quorums_chainlocks.h:153`).
- `isTxInstantLocked` → `quorumInstantSendManager->IsLocked(txid)` (`src/llmq/quorums_instantsend.h:364`).

**Implicación:** dApps tienen finalidad real en ~2s vs. los 12 minutos de finalidad probabilística de Ethereum. Bridges, DEXes y exchanges pueden tratar swaps como finales casi al instante.

#### 4.4 Precompile `0x...0a04` — Masternode Registry

**Interfaz Solidity:**

```solidity
interface IMasternodeRegistry {
    struct Masternode {
        bytes32 proTxHash;
        uint32 ip;
        uint16 port;
        bytes pubKeyOperator;     // BLS 48 bytes
        address payoutAddress;
        uint64 collateralAmount;
        bool isBanned;
    }

    function getCount() external view returns (uint256);
    function getByIndex(uint256 index) external view returns (Masternode memory);
    function getByProTxHash(bytes32 proTxHash) external view returns (Masternode memory);
    function isMasternode(bytes32 proTxHash) external view returns (bool);
}
```

**Implementación:**
- Llama `deterministicMNManager->GetListAtChainTip()` (`src/evo/deterministicmns.h:676`).
- Itera el `immer::map<uint256, CDeterministicMNCPtr>`.
- **Ojo thread-safety:** requiere `cs_main` implícito, lo cual ya se sostiene durante validación de bloques.

**Implicación:** governance on-chain componible. DAOs que voten ponderado por masternode collateral. Treasury contracts que paguen servicios a masternodes verificados. Nuevas formas de staking delegado.

---

### Fase 5 — Wallet + Qt integration (2 meses, 2 ingenieros)

**Objetivo:** que un usuario pueda firmar y enviar tx EVM desde el wallet propio, no solo via MetaMask.

#### 5.1 EVM keys en wallet (semana 1–3)

`src/wallet/wallet.cpp`: el wallet ya gestiona claves secp256k1. EVM usa el **mismo curve**, pero la derivación de dirección difiere:
- UTXO addr = `Base58Check(version || RIPEMD160(SHA256(pubkey)))`
- EVM addr = `last 20 bytes of Keccak256(uncompressed_pubkey)`

Añadir `CWallet::GetEvmAddressForKey(const CKey&)` y derivar tanto UTXO como EVM addr de la misma seed BIP39 (path BIP44 estándar `m/44'/60'/0'/0/i`).

#### 5.2 CreateEvmTransaction (semana 3–5)

Añadir en `src/wallet/wallet.cpp`:

```cpp
CWalletTx CWallet::CreateEvmCallTransaction(
    const uint160& to, const std::vector<uint8_t>& data,
    uint64_t value, uint64_t gasLimit, uint64_t maxFeePerGas,
    uint64_t maxPriorityFeePerGas, std::string& strError);
```

Internamente: construye `CEvmCallTx`, lo serializa en `vExtraPayload`, marca `nType = TRANSACTION_EVM_CALL`, añade output cero-valor con `OP_EVMCALL`, firma con la clave EVM, broadcast.

#### 5.3 Wallet RPC (semana 5–6)

`src/wallet/rpcwallet.cpp`: añadir:
- `evm_deploycontract(bytecode, gasLimit, [value])`
- `evm_sendcall(to, data, [value], [gasLimit])`
- `evm_spendtoutxo(amount, address)` — mover RTM de cuenta EVM a UTXO.
- `evm_getbalance(addr)` (alias de `eth_getBalance` en namespace nativo).

#### 5.4 Qt UI (semana 6–8)

Editar `src/qt/transactionrecord.h:88-106`:

```cpp
enum Type {
    Other, Generated, SendToAddress, /* ... existing ... */
    DeployContract,        // NUEVO
    CallContract,          // NUEVO
    EvmInternalTransfer,   // NUEVO (entre cuentas EVM)
    EvmToUtxo,             // NUEVO
};
```

Nuevo diálogo Qt `EvmContractDialog` para deploy/call. Modelo simple: pegar bytecode/ABI, parámetros, preview gas cost.

---

### Fase 6 — Activation, testnet pública, auditorías (3 meses, full team)

**Objetivo:** llevar todo lo anterior a una testnet pública estable que se pueda usar para auditar y construir.

#### 6.1 Activation params en mainchain (semana 1)

`src/chainparams.cpp`: añadir entrada en `consensus.vUpdates[UPDATE_EVM]`:
```cpp
consensus.vUpdates[UPDATE_EVM].bit = 5;
consensus.vUpdates[UPDATE_EVM].nStartTime = 1735689600;   // 2025-01-01
consensus.vUpdates[UPDATE_EVM].nTimeout   = 1767225600;   // 2026-01-01
consensus.vUpdates[UPDATE_EVM].nWindowSize = 4032;        // ~7 días
consensus.vUpdates[UPDATE_EVM].nThreshold  = 3226;        // 80%
```

#### 6.2 Testnet pública dedicada (semana 1–2)

Levantar **3 nodos seed en infraestructura propia** + 5 masternodes seed. Genesis fresh, EVM activado desde bloque 0 (no signaling). Block explorer: fork de [Blockbook](https://github.com/trezor/blockbook) con plugin EVM.

Endpoint público RPC: `evm-testnet.raptoreum.com:8545`.

#### 6.3 Bug bounty + auditorías paralelas (semana 2–12)

- **Trail of Bits** — auditoría del AAL y consensus changes ($150k–250k, 6 semanas).
- **ChainSecurity** — auditoría de los precompiles ($100k–150k, 4 semanas).
- **Open Zeppelin** — auditoría de los contracts de bridge entre Smart Assets y ERC-20 ($80k–120k, 3 semanas).
- **Bug bounty pool**: $1M en RTM bloqueado en multisig, payouts ImmuneFi-tier.

#### 6.4 Infraestructura de devs (semana 4–12, en paralelo)

- **TypeScript SDK**: `@raptoreum/evm-sdk` — wrapper sobre ethers v6 con helpers para Smart Assets, LLMQ, ChainLocks. Repo nuevo, no en `raptoreum/raptoreum`.
- **Docs site nuevo**: `docs.raptoreum.com` (Docusaurus o Mintlify). Migrar todo lo disperso.
- **Faucet** para testnet.
- **Subgraph fork**: indexer estilo TheGraph adaptado a RTM-EVM.
- **Tutoriales paso a paso**: "Deploy your first ERC-20", "Create a DEX pool with Smart Assets", "Build a price oracle with LLMQ".

#### 6.5 Programa early-builders (semana 6–12)

- Grants $5k–50k para 10–20 proyectos que construyan en testnet RTM-EVM.
- Mínimo 3 verticals representados: DEX, NFT marketplace, RWA tokenization.

---

### Fase 7 — Mainnet activation (3 meses)

**Objetivo:** activar EVM en mainnet sin chain split.

#### 7.1 Comunicación masternodes (semana 1–6)

- Anuncio formal con 90 días de aviso.
- Coordinación con operadores de pools (Suprnova, etc.).
- Email/Discord/Telegram broadcasts a operadores de masternodes (la lista DMN es pública on-chain — comunicación dirigida).
- Release candidate (`v2.0.0-rc1`) disponible ≥60 días antes de start time.

#### 7.2 Soft-fork signaling (semana 6–12)

Activación via BIP9 sobre 4032 bloques (~7 días). Threshold: 80% de masternode signaling **además** del miner signaling. Si no llega, retry en la siguiente window.

#### 7.3 Activation height + grace period (semana 12)

Tras LockedIn: 30 días grace period antes de Active. Última oportunidad para parchear bugs críticos.

---

### Fase 8 — Ecosystem launch (2 meses, marketing-heavy)

**Objetivo:** que el día +1 de mainnet activation, RTM-EVM tenga un ecosistema usable.

Pre-deployments en testnet, listos para mainnet con un script de migración:

| Proyecto | Tipo | Origen |
|---|---|---|
| **RaptorSwap** | AMM (Uniswap V2 fork) | Equipo interno + grant |
| **RaptorLend** | Lending (Aave V2 fork) | Grant a equipo externo |
| **RaptorNFT** | Marketplace NFT (Smart Assets como NFT) | Equipo interno |
| **wRTM↔ETH bridge** | LayerZero | Integración oficial |
| **wRTM↔BSC bridge** | LayerZero | Integración oficial |
| **Stable USD lending** | Stablecoin propia con sobrecolateralización en RTM | Equipo interno |
| **Indexer público** | Subgraph + GraphQL endpoint | Equipo interno |
| **Block explorer EVM** | Blockbook fork | Equipo interno |

Marketing: Twitter campaign coordinada, AMAs en Discord/Reddit, partnerships con CoinGecko/CoinMarketCap, push activo para listing en Binance/KuCoin/Gate del par RTM/USDT (si no está ya), narrativa: **"The first Bitcoin-derivative L1 with native EVM, threshold-signed oracles, and 2-second finality."**

---

## Equipo y presupuesto

| Rol | Cantidad | Coste/año |
|---|---|---|
| Senior C++ engineer (Bitcoin Core experience) | 2 | $200k × 2 = $400k |
| Senior engineer (geth/erigon experience) | 2 | $200k × 2 = $400k |
| Cryptographer (BLS / threshold sigs) | 0.5 (part-time) | $80k |
| SRE / DevOps | 1 | $150k |
| DevRel / Docs | 1 | $130k |
| Product manager | 1 | $150k |
| **Subtotal salarios/año** | | **$1.31M** |

**Costes adicionales (no recurrentes):**
- Auditorías Fase 6: ~$400k–500k
- Bug bounty pool: $1M (locked, no gastado a priori)
- Grants Fase 6 a builders: $300k–500k
- Marketing Fase 8: $200k–400k
- Infraestructura (servers, RPC pública, testnet): $50k/año

**Total 24 meses: $4M–6M USD.**

Para contexto: Polygon levantó $450M, Avalanche $230M, Sei $120M. Esto es 1–3% del rango de competidores serios. Es **financiable** vía treasury (RTM tiene reservas), token sale modesto, o subvención de la Fundación Raptoreum.

---

## Verificación end-to-end

Después de Fase 8, este test debe pasar (= success):

```bash
# 1. Compilar mainnet binary
make -j8 && make install

# 2. Sync mainnet (post-activation)
raptoreumd -daemon
raptoreum-cli getblockchaininfo | jq .updates.evm   # "active"

# 3. JSON-RPC eth_* funciona
curl -s http://localhost:8545 -d '{"jsonrpc":"2.0","method":"eth_chainId","id":1}'
# → "0x1ccd"   (7373)

# 4. MetaMask: añadir red custom (RPC: localhost:8545, ChainId: 7373) → conecta
# 5. Importar private key → balance RTM aparece en weis

# 6. ethers.js: deploy ERC-20 standard
node -e '
  const { ethers } = require("ethers");
  const provider = new ethers.JsonRpcProvider("http://localhost:8545");
  const signer = new ethers.Wallet(process.env.KEY, provider);
  const factory = new ethers.ContractFactory(abi, bytecode, signer);
  const token = await factory.deploy("Test", "TST", 1_000_000n);
  console.log("Deployed at", token.target);
'

# 7. Smart Asset → ERC-20 precompile
# Crear asset RTM via tx tradicional
raptoreum-cli createasset '{"name":"TESTASSET","amount":1000000}'
# Calcular su EVM address (precompile)
ASSET_EVM_ADDR=$(raptoreum-cli evm_assetAddress TESTASSET)
# Llamar balanceOf desde Solidity (via cast / web3)
cast call $ASSET_EVM_ADDR "balanceOf(address)(uint256)" 0xMyEvmAddr
# → 1000000 * 10^8

# 8. LLMQ oracle precompile
# Solicitar firma de mensaje desde contrato
cast send 0x...0a02 "requestSignature(uint8,bytes32,bytes32)" 1 0x... 0x...
# Esperar ~3 bloques (InstantSend latency)
cast call 0x...0a02 "getSignature(uint8,bytes32)(bool,bytes)" 1 0x...
# → (true, 0x<96 bytes BLS sig>)

# 9. ChainLocks precompile
cast call 0x...0a03 "isChainLocked(uint32,bytes32)(bool)" 1234567 0x<blockhash>
# → true

# 10. RaptorSwap (Uniswap V2 fork) — añadir liquidez RTM/TESTASSET
# UI funciona, swap completa, evento Transfer emitido on-chain

# 11. Reorg test (regtest): reorganización de 6 bloques tras tx EVM compleja
# → estado EVM revierte exactamente, accounts/storage/logs idénticos a pre-tx
```

Si los 11 pasos pasan en mainnet sin asistencia manual: **success**.

---

## Riesgos y mitigaciones

| Riesgo | Severidad | Mitigación |
|---|---|---|
| Bug de consenso en EVM (state divergence entre nodos) | **Crítico** | Auditorías triples + testnet ≥6 meses + soft launch con gas limit reducido las primeras 4032 alturas |
| Hard-fork no activa (signaling <80%) | **Alto** | Comunicación 90 días antes; default opt-in en releases; window de retry. Plan B: extender deadline 6 meses. |
| State bloat mata descentralización (full node >TB) | **Medio** | Pruning agresivo; snapshot sync desde día 1; objetivo ≤500GB a 5 años |
| MEV destructivo en mempool | **Medio** | LLMQ-based PBS (proposer-builder separation): masternodes ordenan tx en commit-reveal con BLS. Diseño detallado en Fase 4 si tiempo permite, si no en post-launch. |
| Audit encuentra bug crítico tarde | **Alto** | Plan de contingencia: poder retrasar mainnet 6 meses sin deshacer trabajo. Activation params ajustables en release de emergencia. |
| Regulatorio (DeFi atrae scrutiny) | **Medio** | Foundation en jurisdicción cripto-friendly (Suiza, Singapur, Cayman) **antes** de mainnet. Asesoría legal pre-launch. |
| Equipo no encuentra ingenieros senior C++ | **Medio** | Empezar reclutamiento en Fase 0. Comunidad Bitcoin Core es pequeña pero existente. Considerar Rust para nuevos componentes opcionales. |

---

## Decisiones que tomar antes de empezar

Estas no las he asumido — necesitan input antes de Fase 0:

1. **¿ChainId fijo definitivo?** Sugerencia: 7373 mainnet, 7374 testnet, 7375 regtest. ChainID list de Ethereum debe registrarse oficialmente.
2. **¿Quemado total o parcial de base fee?** EIP-1559 puro (100% burn) maximiza scarcity narrative. Alternativa: 50% burn / 50% a treasury de masternodes.
3. **¿Mantener Smart Assets actuales como están o sunset?** Recomiendo mantener tal cual: la dualidad UTXO-asset + EVM-precompile es la diferenciación.
4. **¿Foundation legal antes de Fase 6 o esperar?** Recomiendo antes — bug bounty y grants requieren entidad pagadora.
5. **¿Token sale parcial para financiar?** 24 meses × $250k/mes operativo + ~$2M extras = ~$8M. Decisión de tokenomics + comunidad.

---

## Próximo paso inmediato

Si se aprueba este plan, en las próximas **2 semanas** ejecutar Fase 0 completa:

1. Fork del repo a `raptoreum/raptoreum` interno con branch `evm-spike`.
2. Añadir `evmone` como submódulo en `depends/`.
3. Implementar comando RPC `evm_executeReadOnly`.
4. Test smoke pasa.
5. PR interno con review cruzada.

Si Fase 0 termina sin showstoppers: contratar y arrancar Fase 1.
Si Fase 0 revela problemas estructurales: re-evaluar y posiblemente pivotar a Camino A (sidechain).

---

*Plan v1 — basado en exploración del repositorio en commit master local, fecha 2026-05-09. Sujeto a revisión tras Fase 0.*

---

## Decisiones de revisión (`/plan-eng-review`, 2026-05-09)

### D1 — Modelo de ejecución EVM y `cs_main`
**Decisión: B — Worker pool + merge serializado** (estilo `CCheckQueue` de Bitcoin Core).

EVM tx ejecutadas en pool de workers paralelo. `cs_main` se sostiene solo para serializar el merge final al state cache. Reusa el patrón existente en `src/checkqueue.h`. Coste: +3-4 semanas a Phase 2.

**Implicación:** evita el problema famoso de Qtum (TPS bajo por lock holding durante ejecución EVM). Sin esta decisión, mainnet con uso real saturaría el lock y degradaría P2P.

### D2 — Campos del `CBlockHeader` y modelo de activación
**Decisión: A — Añadir `stateRoot` + `receiptsRoot` + `transactionsRoot` al header, activar via hard-fork coordinado.**

Cambia el roadmap de activación: el plan original decía "soft-fork BIP9 + masternode signaling" — esto **no es válido** para cambios de estructura del header. Activación real será:
- Bloque de activación fijo anunciado con ≥90 días de aviso
- 95% de masternodes deben señalizar upgrade
- Antiguos nodos rechazan bloques nuevos automáticamente — los operadores deben actualizar antes del bloque de activación

**Implicación:** activación más estricta que un soft-fork. Modificar `src/chainparams.cpp` con `nEvmActivationHeight` fijo, no version-bits.

### D3 — Determinismo de precompiles LLMQ/ChainLocks
**Decisión: A — Commitment de ChainLocks/IsLocks en el header del bloque.**

Coordinar con D2: el header tendrá un cuarto campo nuevo, `chainLocksCommit` (32-128 bytes), con la lista de ChainLocks/IsLocks observados por el miner. El precompile lee de ese campo, no del estado runtime de los managers LLMQ.

**Implicación de implementación:**
- `src/llmq/quorums_chainlocks.cpp` necesita método `GetCommittedLocksAtHeight(height)` que sea determinístico
- Validación de bloque verifica que los locks committed son válidos (firmas BLS verificables)
- Precompile `0x...0a03` lee del header, no de `chainLocksHandler`

### D4 — Modelo de estado de Smart Assets en EVM
**Decisión: A — Mirror completo bidireccional.**

⚠️ **Esta es la decisión de mayor riesgo de ingeniería de las 6.** Phase 4.1 debe incluir explícitamente:

1. **Ordering rules formales de `ConnectBlock`:**
   - Pase 1: aplicar UTXO + Smart Assets nativas (incluyendo `TRANSACTION_MINT_ASSET`).
   - Pase 2: ejecutar EVM tx loop (worker pool de D1) con acceso al asset state via mirror.
   - Pase 3: reconciliation — verificar que cualquier mutación EVM-side al mirror se refleja correctamente en assetsdb.
2. **Allowance mapping vive en EVM storage trie del precompile** — NO extender `assetsdb`.
3. **Reentrancy guards en el precompile mismo:** mutex per-asset durante ejecución.
4. **Test exhaustivo de divergencia (T-mirror):** ejecutar 10000 sequences de `(Smart Asset tx, EVM tx que mute el mismo asset)` en orden aleatorio y verificar convergencia de estado entre nodos.

Si durante Phase 4 el mirror se demuestra inviable (test T-mirror falla repetidamente), pivot a Camino C alternativo: wrap/unwrap pattern (lo que era opción D4-C).

### D5 — Ordering de transacciones (consensus rule)
**Decisión: A — Fee-priority estándar (Ethereum-compatible) en v1.**

LLMQ-PBS commit-reveal documentado como upgrade v2 post-launch (otro hard fork). Razón: fee-priority es lo que el tooling (gas estimators, mempool watchers, MEV-boost) asume. Llegamos a mainnet ~3 meses antes y desbloqueamos compatibilidad.

**Trade-off aceptado:** MEV estándar (sandwich attacks, frontrun) será posible en v1. Mitigación parcial: documentar guidance para users (slippage tolerance, private mempools cuando estén). Solución estructural en v2.

### D6 — Versión de Ethereum hard fork como target
**Decisión: B + plan de upgrade — Cancun como target inicial, mecanismo de upgrade documentado para Prague/Osaka.**

Phase 2 implementa Cancun completo:
- PUSH0, MCOPY, transient storage (TLOAD/TSTORE), KZG_POINT_EVALUATION precompile
- **Excluir explícitamente EIP-4844 blob txs** (no relevantes hasta L2s eventuales)

Mecanismo de upgrade futuro: `Updates().GetEvmHardFork(height)` retorna la versión EVM activa, parametrizada por activation heights en `chainparams`. Permite añadir Prague/Osaka via soft-fork (ahora que el header structure ya está fijo).

---

## Issues documentados en revisión (sin decisión inmediata, abordar durante implementación)

### Architecture (a resolver durante design detallado de cada Phase)

- **A7 (HIGH) — Async LLMQ oracle gas/timeout:** `requestSignature` sin gas escrow + timeout permite DoS al sistema LLMQ. Dirección: implementar gas reservation pattern; reembolso si la firma llega; quema si timeout. Diseñar antes de Phase 4.2.
- **A8 (MEDIUM) — BIP44 derivation conflict:** RTM existente usa `coin_type=10226`, EVM Ethereum es `coin_type=60`. Documentar UX en docs.raptoreum.com — usuarios verán direcciones EVM diferentes que un Ethereum wallet con la misma seed. Considerar: `m/44'/10226'/0'/1/i` para EVM (subtree separado bajo coin_type RTM).
- **A9 (HIGH) — EIP-1559 calibración para PoW de ~2 min:** factor de ajuste `1/8` por bloque calibrado para 12s. Recalibrar en Phase 2.4 con análisis empírico (target: base fee ajusta ±12.5% por bloque pero amortiguado por block_time). Posible factor: `1/64` con block_time RTM.
- **A10 (HIGH) — Reorg state revert subdimensionado:** una línea de "CBlockUndo extension" no es diseño. Necesario en Phase 2.6: journal incremental al estilo geth, con tests de reorg de hasta 100 bloques de profundidad.
- **A11 (LOW) — Address collision Smart Asset → ERC-20:** prefijo `0xA55E70...` debe estar bloqueado a deploys EVM (validation rule en CheckEvmDeployTx).
- **A12 (MEDIUM) — State pruning no diseñado:** medir crecimiento real en testnet (Phase 6). Diseño formal en Phase 7. Sin esto, full nodes ≥1TB en 2-3 años.

### Code quality (pre-requisitos de implementación)

- **CQ1 (HIGH):** Sección 2.1 dice "modelar 1:1 sobre `assetsdb.cpp`" — esto es incorrecto. EVM state es Merkle Patricia Trie, no flat KV cache. Usar assetsdb solo como modelo de schema LevelDB; trie correcto desde cero.
- **CQ2 (MEDIUM):** Crear `docs/evm/BRANCH-GUIDE.md` y `TODOS.md` en el repo antes de Phase 0. Sin esto, coordinación de 6 ingenieros será caótica.
- **CQ3 (MEDIUM):** Integrar evmone con Guix deterministic builds desde Phase 0. Sin esto, supply-chain seguridad se rompe.
- **CQ4 (MEDIUM):** Phase 6 asume Foundation legal existe. Si no, Phase 6 está bloqueada por trabajo societario. Bloquear y resolver ANTES de iniciar Phase 0.

### Test gaps (críticos)

- **T1 (CRÍTICO):** Phase 0 success = los **3 smoke tests del plan + el Ethereum tests/ suite completo (10000+ casos)** pasan al 100%. Si no, evmone no está integrado correctamente. Esto es la verificación más barata.
- **T2 (CRÍTICO):** **State divergence harness** — 2 nodos en regtest comparan stateRoot después de cada bloque (posible gracias a D2=A). Aborta CI si difieren. Debe correr en cada PR desde Phase 1.
- **T3 (CRÍTICO):** **Reorg fuzzing** — property-based: `for any sequence of blocks B1...BN with random reorgs, replay produces identical state`. Captura bugs de undo journaling (A10).
- **T4 (HIGH):** **Pinning explícito de evmone version** — runtime check al arrancar el nodo, abort si version difiere de la esperada. Previene state divergence silenciosa por upgrade descoordinado.
- **T5 (HIGH):** **Test de regresión para A11 (collision)** — contratos deployados en `0xA55E700000...` rechazados.
- **T-mirror (CRÍTICO, viene de D4=A):** 10000 sequences aleatorias de `(Smart Asset tx, EVM tx mutando el mismo asset)` → estado convergente entre nodos.

### Performance (a abordar con datos reales)

- **P1:** Target "≤500GB en 5 años" no derivado. Re-evaluar tras 6 meses de testnet con datos reales.
- **P2:** State pruning ausente del plan (= A12).
- **P3:** ConnectBlock latency budget = 500ms p99 con block_gas_limit = 30M y worker pool de D1.
- **P4:** Mempool size estimation con EVM calldata grande (~24KB por deploy). Actual = 300MB, posiblemente insuficiente.

---

## Failure modes con gaps críticos

5 codepaths con failure mode silente sin test ni error handling. Estos **deben tener cobertura antes de testnet pública (Phase 6)**:

1. **EVM execution race en worker pool** → state divergence silente. Mitigación: T2 (divergence harness) + assert fail-stop en mismatch.
2. **Smart Asset mirror sync inconsistencia post-reorg** → funds locked, silent. Mitigación: T-mirror.
3. **LLMQ async signature pendiente forever** → contract gas drained, silent. Mitigación: A7 (timeout + escrow).
4. **ChainLock commitment omitido por miner malicioso** → contratos fallan inesperadamente. Mitigación: validación del header reject blocks sin locks committed.
5. **EIP-1559 base fee swing 100x** → "tx never confirms" UX. Mitigación: A9 (recalibración) + test bajo carga sintética.

---

## Worktree parallelization (post-D1...D6)

```
Lane A (consensus, 2 ingenieros):    Phase 0 → 1 → 2 (AAL, state, ConnectBlock con worker pool de D1)
Lane B (RPC + tooling, 1 ingeniero): Phase 3 (eth_* namespace, RLP adapter)
                                     ⟵ depende de Lane A finalizar Phase 1
Lane C (precompiles, 2 ingenieros):  Phase 4 (los 4 precompiles + el extra de header commitment de D3)
                                     ⟵ depende de Lane A finalizar Phase 2
Lane D (wallet + Qt, 1 ingeniero):   Phase 5 (wallet + Qt UI)
                                     ⟵ depende de Lane A finalizar Phase 1, Lane B Phase 3
Lane E (infra, 1 SRE):               CI/CD, devnet, Guix integration, harness T2/T3
                                     ⟵ desde día 1, paralelo a todo
Lane F (legal/funding, 1 PM):        Foundation, tokenomics, audits scheduling
                                     ⟵ desde día 1, paralelo a todo
```

**Conflict flag:** Lane A (Phase 2) y Lane C (Phase 4) ambos tocan `src/validation.cpp`. Coordinar via merge windows.

---

## Próximo paso revisado

Antes de empezar Phase 0:

1. **Resolver CQ4 (Foundation legal).** Sin esto, Phase 6 está bloqueada — y eso afecta todo el funding del proyecto.
2. **Crear docs/evm/BRANCH-GUIDE.md y TODOS.md** en el repo con convenciones del equipo.
3. **Phase 0 spike** ejecuta los 4 entregables del plan original **+ el Ethereum tests suite completo** (T1).
4. **En paralelo a Phase 0:** Lane E arranca con harness T2 (state divergence) y Guix integration (CQ3) para que estén listos cuando Phase 1 comience.

Si Phase 0 + T1 pasan limpio: contratar el resto del equipo y arrancar Phase 1 (Lane A).
Si Phase 0 falla en evmone integration o T1 < 100%: re-evaluar Camino C antes de gastar más capital.

---

*Revisión `/plan-eng-review` v1 completada — 2026-05-09. 6 decisiones críticas registradas (D1-D6), 16 issues documentados (A7-A12, CQ1-CQ4, T1-T5+T-mirror, P1-P4), 5 failure modes con gaps críticos identificados.*
