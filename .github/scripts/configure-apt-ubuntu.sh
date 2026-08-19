#!/usr/bin/env bash
# Bound apt network I/O on GitHub-hosted Ubuntu runners.
#
# The runner image lists azure.archive.ubuntu.com first in
# /etc/apt/apt-mirrors.txt and sets Acquire::Retries=10 with a 120s HTTP
# timeout. When that Azure mirror hangs, `apt-get update` can stall for
# hours (actions/runner-images#12949) until GitHub cancels the job.
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "configure-apt-ubuntu.sh: skipping on $(uname -s)"
  exit 0
fi

# The runner image writes APT::Acquire::Retries=10 to 80-retries. Override
# both spellings so a hung mirror cannot retry for the whole job timeout.
sudo tee /etc/apt/apt.conf.d/80-retries >/dev/null <<'EOF'
APT::Acquire::Retries "3";
EOF
sudo tee /etc/apt/apt.conf.d/99-ci-timeouts >/dev/null <<'EOF'
APT::Acquire::Retries "3";
Acquire::Retries "3";
Acquire::http::Timeout "20";
Acquire::https::Timeout "20";
Acquire::ftp::Timeout "20";
EOF

arch="$(dpkg --print-architecture 2>/dev/null || echo unknown)"
if [[ "${arch}" != "amd64" ]]; then
  echo "configure-apt-ubuntu.sh: timeouts set; leaving mirrors for ${arch}"
  exit 0
fi

if [[ -f /etc/apt/apt-mirrors.txt ]]; then
  {
    printf 'https://archive.ubuntu.com/ubuntu/\tpriority:1\n'
    printf 'https://security.ubuntu.com/ubuntu/\tpriority:2\n'
    printf 'http://azure.archive.ubuntu.com/ubuntu/\tpriority:3\n'
  } | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
fi

rewrite_azure_sources() {
  local f="$1"
  [[ -f "${f}" ]] || return 0
  grep -q 'azure\.archive\.ubuntu\.com' "${f}" || return 0
  sudo sed -i \
    's|http://azure\.archive\.ubuntu\.com/ubuntu/|https://archive.ubuntu.com/ubuntu/|g' \
    "${f}"
}

rewrite_azure_sources /etc/apt/sources.list
if [[ -d /etc/apt/sources.list.d ]]; then
  for f in /etc/apt/sources.list.d/*; do
    rewrite_azure_sources "${f}"
  done
fi

echo "configure-apt-ubuntu.sh: amd64 apt now prefers archive.ubuntu.com"
