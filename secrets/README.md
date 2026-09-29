# Test credentials

`testing.json` stores the OpenCode Go test key encrypted with SOPS and age.
The age private key remains in the user's SOPS configuration, outside this repository.
Nix builds do not decrypt this file.

Run a live test with:

```sh
nix build .#live-agent -o result-live-agent
nix develop --command bash scripts/run-live-agent.sh /tmp/opencode/lutin-live-test opencode-go/gpt-6-luna prompt.txt
```

The script decrypts the provider key at runtime into a private temporary directory.
It removes that directory when the test exits.
It copies project files, response recordings, and logs to the output directory without copying credentials.
The output directory must be empty or absent.
To continue a recorded session, supply its output directory as the fourth script argument.
