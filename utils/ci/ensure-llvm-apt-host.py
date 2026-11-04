"""Recover apt.llvm.org resolution in a disposable Linux Actions runner."""
import ipaddress
import json
import os
import subprocess


HOST = 'apt.llvm.org'
# This is apt.llvm.org's CDN CNAME. Resolve its current addresses rather than
# pinning an address; verify the original LLVM HTTPS hostname before using one.
CDN = 'dualstack.l2.shared.us-eu.fastly.net'


def addresses(text):
    result = []
    for line in text.splitlines():
        if not line.split():
            continue
        try:
            address = ipaddress.IPv4Address(line.split()[0])
        except ipaddress.AddressValueError:
            continue
        if str(address) not in result:
            result.append(str(address))
    return result


def lookup(host):
    try:
        result = subprocess.run(['getent', 'ahostsv4', host], capture_output=True,
            text=True, timeout=15, check=False)
    except subprocess.TimeoutExpired:
        return 124, []
    return result.returncode, addresses(result.stdout)


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('RUNNER_OS') != 'Linux':
        raise RuntimeError('LLVM APT host recovery is restricted to Linux Actions runners.')
    code, original = lookup(HOST)
    if code == 0 and original:
        print(json.dumps(dict(llvm_apt_resolution='system', addresses=original)), flush=True)
        return
    code, candidates = lookup(CDN)
    candidates = [value for value in candidates if ipaddress.IPv4Address(value).is_global]
    if code != 0 or not candidates:
        raise RuntimeError('Neither LLVM APT nor its CDN can be resolved.')
    attempts = []
    for address in candidates[:4]:
        command = ['curl', '--disable', '--silent', '--show-error', '--head', '--fail',
            '--proto', '=https', '--proto-redir', '=https', '--max-redirs', '0',
            '--connect-timeout', '5', '--max-time', '15',
            '--resolve', HOST + ':443:' + address, '--output', '/dev/null',
            '--write-out', '%{http_code} %{remote_ip} %{ssl_verify_result}',
            'https://' + HOST + '/llvm.sh']
        result = subprocess.run(command, capture_output=True, text=True, timeout=20, check=False)
        attempts.append(dict(address=address, returncode=result.returncode,
            verification=result.stdout.strip()))
        if result.returncode != 0 or result.stdout.split() != ['200', address, '0']:
            continue
        entry = '\n' + address + ' ' + HOST + ' # neverc-stl-llvm-dns-fallback\n'
        subprocess.run(['sudo', 'tee', '-a', '/etc/hosts'], input=entry, text=True,
            stdout=subprocess.DEVNULL, timeout=10, check=True)
        code, resolved = lookup(HOST)
        if code != 0 or address not in resolved:
            raise RuntimeError('Verified LLVM APT host mapping was not installed.')
        print(json.dumps(dict(llvm_apt_resolution='verified-cdn', hostname=HOST,
            cdn=CDN, address=address, tls_hostname_verified=True, attempts=attempts)), flush=True)
        return
    raise RuntimeError('LLVM APT CDN HTTPS verification failed: ' + json.dumps(attempts))


if __name__ == '__main__':
    main()
