"""Read public RPC metadata and help from an isolated copy of the released node."""

import argparse
import hashlib
import json
import re
import shutil
import socket
import subprocess
import tarfile
import tempfile
from pathlib import Path
from urllib.request import urlopen

SITE = Path(__file__).resolve().parents[1]
REFERENCE = SITE / "content/documentation/rpc"
CACHE = SITE / ".vitepress/rpc-cache"
PACKAGE = "bitcoin-roots-linux-x86_64.tar.gz"
RELEASES = "https://github.com/LuganoPlanB/bitcoin-roots/releases/download"

# Reviewed differences from the corresponding Core RPC, including methods
# whose names stayed the same. Keep these notes with the release review.
CHANGES = {
    "signmessage": "Sign a BIP322 simple message proof with a supported native SegWit or Taproot wallet address.",
    "verifymessage": "Check a BIP322 simple message proof without loading a wallet.",
    "exportaddresskey": "Export the private keys your wallet holds for an address.",
    "sweepprivkeys": "Preview a sweep from private keys and authorize the reviewed transaction separately.",
    "getmempoolinfo": "Read your node’s active relay settings in the policy field.",
    "getpeerinfo": "See additional information about the current peer connection, including CPU use when available.",
    "disconnectnode": "Disconnect peers by subnet as well as by address or node ID.",
    "prioritisetransaction": "The dust check uses the active dust relay rate. Fee deltas that would overflow are rejected.",
}
for method in ("derivehdkey", "gethdkeys", "listdescriptors"):
    CHANGES[method] = "All modes require --allow-dangerous-rpc on the command line at startup."
for method in ("getrawmempool", "getmempoolentry", "getmempoolancestors", "getmempooldescendants",
               "testmempoolaccept", "submitpackage"):
    CHANGES[method] = "The vsize_adjusted field reports virtual size after local policy adjustments."
CHANGES["getmempoolcluster"] = "Cluster and chunk weights include local policy adjustments."

# Wallet management calls in this category can be used without selecting a loaded wallet.
WALLET_MANAGEMENT = {"createwallet", "listwalletdir", "listwallets", "loadwallet",
                     "migratewallet", "restorewallet", "unloadwallet"}


def collect(bindir, tag, work):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    flags = ["-regtest", f"-datadir={work}", f"-rpcport={port}"]
    daemon = subprocess.Popen([bindir / "bitcoind", *flags, "-listen=0", "-connect=0",
                               "-networkactive=0", "-discover=0", "-listenonion=0", "-server=1", "-printtoconsole=0"])
    cli = [bindir / "bitcoin-cli", *flags]
    try:
        doc = json.loads(subprocess.check_output([*cli, "-rpcwait", "-rpcwaittimeout=30", "getopenrpcinfo"], timeout=35))
        if doc["info"]["version"] != tag or not doc["methods"]:
            raise ValueError("The node's RPC version must match the selected stable release")
        methods = {}
        for method in doc["methods"]:
            name = method["name"]
            if not re.fullmatch(r"[a-z][a-z0-9]*(?:\.[a-z0-9]+)*", name) or name in methods or name == "index":
                raise ValueError(f"Invalid or duplicate RPC name: {name}")
            help_text = subprocess.check_output([*cli, "help", name], text=True, timeout=15)
            methods[name] = {"rpc": method, "help": help_text}
        return methods
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=15)
        except subprocess.TimeoutExpired:
            daemon.kill()
            daemon.wait()


def fetch_binaries(tag, digest, directory):
    archive = directory / PACKAGE
    with urlopen(f"{RELEASES}/{tag}/{PACKAGE}", timeout=60) as response, archive.open("wb") as out:
        shutil.copyfileobj(response, out)
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != digest:
        raise ValueError("Package digest differs from the latest release inventory")
    with tarfile.open(archive) as tar:
        for name in ("bitcoind", "bitcoin-cli"):
            member = tar.getmember(f"bitcoin-roots-{tag[1:]}/bin/{name}")
            if not member.isfile():
                raise ValueError(f"Missing release executable: {name}")
            path = directory / name
            with path.open("wb") as out:
                shutil.copyfileobj(tar.extractfile(member), out)
            path.chmod(0o700)
    return directory


def load_reference(tag, digest):
    cache = CACHE / f"{tag}-{digest}.json"
    if cache.is_file():
        return json.loads(cache.read_text())
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        node = work / "node"
        node.mkdir()
        methods = collect(fetch_binaries(tag, digest, work), tag, node)
    CACHE.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=CACHE, delete=False) as out:
        json.dump(methods, out)
    Path(out.name).replace(cache)
    return methods


def prose(text):
    # Treat RPC strings as text even when they contain descriptors or Vue syntax.
    return re.sub(r"([\\*_`\[\]#|])", r"\\\1",
                  text.strip().replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                  .replace("{", "&#123;").replace("}", "&#125;"))


def fence(text, language="text"):
    delimiter = "`" * max(3, 1 + max((len(m) for m in re.findall(r"`+", text)), default=0))
    return f"{delimiter}{language}\n{text.strip()}\n{delimiter}"


# Examples for commands whose published help only provides a synopsis.
EXAMPLES = {
    "gettxoutproof": """bitcoin-cli gettxoutproof '["TXID"]'""",
    "verifytxoutproof": 'bitcoin-cli verifytxoutproof "PROOF_HEX"',
    "importprunedfunds": 'bitcoin-cli importprunedfunds "RAW_TRANSACTION_HEX" "PROOF_HEX"',
    "walletdisplayaddress": 'bitcoin-cli walletdisplayaddress "YOUR_ADDRESS"',
    "setnetworkactive": "bitcoin-cli setnetworkactive true",
}


def argument(param, help_text):
    schema = param["schema"]
    kind = schema.get("type") or " or ".join(x.get("type", "value") for x in schema.get("oneOf", []))
    if not kind:
        match = re.search(r"^\d+\. " + re.escape(param["name"]) + r"\s+\(([^,)]+)", help_text, re.M)
        kind = match[1] if match else ""
    labels = [kind, "required" if param["required"] else "optional"]
    if "default" in schema:
        labels.append("default " + json.dumps(schema["default"]))
    elif "x-bitcoin-default-hint" in schema:
        labels.append("default " + schema["x-bitcoin-default-hint"])
    names = ", ".join(f"`{name}`" for name in [param["name"], *param.get("x-bitcoin-aliases", [])])
    return (f"**{names}** ({prose(', '.join(filter(None, labels)))})\n\n"
            + prose(param.get("description", ""))).rstrip()


def render(name, entry):
    method = entry["rpc"]
    help_text = entry["help"]
    description = method["description"].strip()
    sentences = re.split(r"(?<=[.!?])\s+", description, maxsplit=1)
    lead, rest = sentences[0], "".join(sentences[1:])
    lead = " ".join(lead.split())
    category = method["x-bitcoin-category"]
    metadata = {"title": name, "description": lead, "rpcCategory": category}
    if name in CHANGES:
        metadata["rootsChange"] = CHANGES[name]
    parts = ["---\n" + "\n".join(f"{key}: {json.dumps(value, ensure_ascii=False)}"
                                    for key, value in metadata.items()) + "\n---",
             "[RPC reference](./) · " + category, f"# {name}", prose(lead)]
    example = re.search(r"^> (bitcoin-cli (?:-[^ \n]+ )*" + re.escape(name) + r"(?: [^\n]*)?)$", help_text, re.M)
    if example:
        parts.append(fence(example[1], "sh"))
    elif name in EXAMPLES:
        parts.append(fence(EXAMPLES[name], "sh"))
    elif not any(p["required"] for p in method["params"]):
        parts.append(fence(f"bitcoin-cli {name}", "sh"))
    else:
        parts.extend(["Command syntax", fence("bitcoin-cli " + help_text.splitlines()[0])])

    requirements = []
    if (category == "wallet" and name not in WALLET_MANAGEMENT) or name == "fundrawtransaction":
        requirements.append("Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.")
    if "--allow-dangerous-rpc" in description:
        requirements.append("Start the node with `--allow-dangerous-rpc` to enable this command.")
    if "Requires wallet passphrase" in description:
        requirements.append("Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.")
    if name in ("getblockfilter", "scanblocks"):
        requirements.append("Enable the block filter index with `-blockfilterindex` and let it sync.")
    if name == "verifymessage":
        requirements.append("You can verify a message without a wallet or a synced blockchain.")
    if requirements:
        parts.extend(["## Requirements", "\n".join("- " + line for line in requirements)])
    if name in CHANGES:
        parts.extend(["**New or changed in Roots**", prose(CHANGES[name])])
    if rest:
        parts.extend(["## How it works", prose(rest)])
    parts.append("## Arguments")
    parts.extend(argument(param, help_text) for param in method["params"])
    if not method["params"]:
        parts.append("No arguments.")
    sections = re.split(r"^(Arguments|Named Arguments|Result(?: \(.*\))?|Examples):\s*\n", help_text, flags=re.M)
    results = []
    for label, body in zip(sections[1::2], sections[2::2]):
        if label.startswith("Result"):
            if label != "Result":
                results.append(prose(label[len("Result ("):-1]))
            scalar = re.fullmatch(r'[^\n]+?\s+\((string|numeric|boolean|json null)\)\s*([^\n]*)', body.strip())
            if scalar:
                results.append(f"`{scalar[1]}`" + (". " + prose(scalar[2]) if scalar[2] else ""))
            else:
                results.append(fence(body))
        elif label == "Named Arguments" or (label == "Arguments" and any(
                p["schema"].keys() & {"items", "properties", "oneOf", "additionalProperties"} for p in method["params"])):
            parts.extend(["<details>\n<summary>" + ("Named arguments" if label == "Named Arguments"
                          else "Argument formats") + "</summary>", fence(body), "</details>"])
    parts.extend(["## Return value", *(results or [fence(json.dumps(method["result"]["schema"], indent=2), "json")])])
    lines = "\n\n".join(parts).splitlines()
    return "\n".join(line.rstrip() for line in lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publication", type=Path, help="Use the release selected for this Pages build")
    args = parser.parse_args()
    if args.publication:
        release = json.loads(args.publication.read_text())["rpcRelease"]
        tag, digest = release["tag"], release["digest"]
    else:
        with urlopen("https://api.github.com/repos/LuganoPlanB/bitcoin-roots/releases/latest", timeout=30) as response:
            release = json.load(response)
        if release["draft"] or release["prerelease"]:
            raise ValueError("Expected a published stable release")
        tag = release["tag_name"]
        digest = next(a["digest"] for a in release["assets"] if a["name"] == PACKAGE)
    if not re.fullmatch(r"v[0-9]+\.[0-9]+(?:\.[0-9]+)?-roots\.[0-9]+", tag) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("Invalid stable release identity")
    methods = load_reference(tag, digest.split(":")[1])
    pages = {name: render(name, entry) for name, entry in methods.items()}
    if not all(re.fullmatch(r"[a-z][a-z0-9]*(?:\.[a-z0-9]+)*", name) and name != "index" for name in pages):
        raise ValueError("Invalid RPC name")
    REFERENCE.mkdir(parents=True, exist_ok=True)
    for name, page in pages.items():
        (REFERENCE / f"{name}.md").write_text(page)
    for path in REFERENCE.glob("*.md"):
        if path.stem not in pages and "\nrpcCategory:" in path.read_text():
            path.unlink()
    index = REFERENCE / "index.md"
    updated, count = re.subn(r"(?m)^sourceRelease: .+$", f"sourceRelease: {tag}", index.read_text())
    if count != 1:
        raise ValueError("Missing source release in RPC index")
    index.write_text(updated)
    print(f"Prepared {len(pages)} RPC command pages from {tag}.")


if __name__ == "__main__":
    main()
