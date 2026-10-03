# Signing Windows releases from CI

The manual process in [WINDOWS_CODE_SIGNING_GUIDE.md](WINDOWS_CODE_SIGNING_GUIDE.md)
works, but it depends on somebody having the USB token in hand. This describes
how to have the `Sign Windows Release` workflow do it instead.

Nothing here signs anything on its own: the workflow is run by hand for a
release, and does nothing at all until the repository variable
`SIGNING_METHOD` is set.

## First, a correction to the existing guide

Part 8 of the guide says signing in GitHub Actions "requires certificate
export". **That is no longer possible.** Since 1 June 2023 the CA/Browser Forum
Baseline Requirements have required every code signing private key to be
generated on, and never leave, a FIPS 140-2 Level 2 (or equivalent) hardware
module. Keys are marked non-exportable, and no CA will issue a `.pfx` you can
drop into a GitHub secret. Any advice that starts with "export the certificate"
predates that rule.

So there are two real options, and they differ in what you have to buy and what
you have to run.

## Option A: self-hosted runner with the token you already own

Keeps the existing Comodo/Sectigo certificate. Nothing is re-issued, nothing new
is bought.

You need a Windows machine that:

- stays online whenever a release is signed,
- has the USB token plugged in,
- has the SafeNet Authentication Client and the Windows SDK installed,
- runs a GitHub Actions self-hosted runner registered with the labels
  `self-hosted`, `windows`, `signing`.

Then set:

| Kind | Name | Value |
|---|---|---|
| Variable | `SIGNING_METHOD` | `selfhosted` |
| Secret | `SIGNING_CERT_THUMBPRINT` | the 40-character thumbprint of the signing certificate |

The workflow calls the existing `scripts/sign-raptoreum.ps1`, so the signing
behaviour is identical to signing by hand.

The catch worth knowing before committing to this: SafeNet normally prompts for
the token PIN on every signature. For unattended signing the PIN has to be
cached in the SafeNet client (single logon), which means a machine that, while
powered on, can sign anything. Treat that machine as sensitive: no other
workloads, no other runners, restricted network access. A self-hosted runner
also executes whatever the workflow says, so it should only ever be enabled for
this repository, and preferably gated behind a protected environment so a pull
request cannot reach it.

## Option B: Azure Artifact Signing (formerly Trusted Signing)

Signing happens on a normal GitHub-hosted runner, so there is no machine to look
after and no token to keep plugged in. The private key lives in Microsoft's HSM
and is never in the repository or on the runner.

- Cost is roughly $10/month on the Basic plan, which includes far more
  signatures than this project will use.
- It requires an Azure subscription and an identity validation step for the
  publisher name that will appear in the signature.
- Authentication is OpenID Connect, so there is no long-lived credential to
  store or rotate. The workflow already requests `id-token: write` for this.

Set:

| Kind | Name | Value |
|---|---|---|
| Variable | `SIGNING_METHOD` | `azure` |
| Secret | `AZURE_CLIENT_ID` | app registration used for OIDC federation |
| Secret | `AZURE_TENANT_ID` | directory tenant |
| Secret | `AZURE_SUBSCRIPTION_ID` | subscription holding the signing account |
| Secret | `AZURE_SIGNING_ENDPOINT` | e.g. `https://eus.codesigning.azure.net` |
| Secret | `AZURE_SIGNING_ACCOUNT` | the signing account name |
| Secret | `AZURE_CERT_PROFILE` | the certificate profile name |

The identity must hold the **Trusted Signing Certificate Profile Signer** role,
and the federated credential must be scoped to this repository.

## Which one

If a machine can be dedicated to it, Option A costs nothing extra and reuses the
certificate that is already paid for. If not, Option B removes the hardware
question and the "who has the token" question at about $10/month, at the cost of
a new certificate identity and an Azure account.

Both produce the same artifact, so the choice can be revisited later by changing
one variable.

## Running it

1. Let `Raptoreum Build` finish and note its run ID (it is in the URL of the
   run).
2. Actions -> `Sign Windows Release` -> `Run workflow`, and give it that run ID
   and the version string.
3. It downloads that run's `raptoreum-win64-<version>` artifact, signs every
   executable, and uploads `raptoreum-win64-signed-<version>`.

Signatures are read back afterwards with `Get-AuthenticodeSignature`, and the
run fails unless every executable is both valid **and** timestamped — an
un-timestamped signature stops verifying the day the certificate expires. The
`checksums.txt` inside each archive is regenerated, because signing changes
every binary and the checksums from the build describe the unsigned ones.

## What is not covered

- Linux and macOS artifacts are not signed by this workflow. The usual practice
  for those is a detached GPG signature over a `SHA256SUMS` file, published
  alongside the release; that is a separate decision about which key signs and
  where it lives.
- No installer is signed, because the build does not currently produce one.
- The workflow does not attach anything to a GitHub release; it uploads a signed
  artifact, and publishing stays a deliberate manual step.
