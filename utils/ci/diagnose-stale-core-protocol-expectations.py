"""Collect successful responses to stale negative protocol cases for diagnosis."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import sys

parser=argparse.ArgumentParser()
parser.add_argument('--verify', type=Path, required=True)
parser.add_argument('--verify-sha256', required=True)
parser.add_argument('--diagnostics',type=Path,required=True)
parser.add_argument('--neverc',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--target',required=True)
args=parser.parse_args()
original=args.verify.read_bytes()
assert hashlib.sha256(original).hexdigest()==args.verify_sha256
old='            assert result.returncode and code in codes, (name, result.returncode, data)\n'
replacement="""            if result.returncode == 0 and not codes:
                    assert len(DIAGNOSTICS) < 512 and len(source.encode()) <= 8 * 1024**2
                    DIAGNOSTICS.append(dict(name=name, expected_code=code, target=request_target, profile=profile,
                        source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                        response_sha256=hashlib.sha256(response.read_bytes()).hexdigest()))
            else:
                assert result.returncode and code in codes, (name, result.returncode, data)
"""
text=original.decode()
assert text.count(old)==1
cookie_check = "        assert module['memory_lifetimes'] and module['array_cookie_abi'] in ('itanium', 'apple-arm64', 'msvc')"
cookie_diagnostics = '''        assert module['memory_lifetimes'], module
        abi = module.get('array_cookie_abi')
        assert abi in (None, 'itanium', 'apple-arm64', 'msvc'), module
        assert len(COOKIES) < 128
        COOKIES.append(dict(name=name, source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                            target=module['target']['triple'], array_cookie_abi=abi,
                            native_heap=module.get('native_heap', False),
                            op_kinds=sorted({node.get('op') for node in walk(module['functions'])
                                             if node.get('op')})))'''
body = text.replace(old, replacement)
assert body.count(cookie_check) == 1
tree = ast.parse(body.replace(cookie_check, cookie_diagnostics, 1))
scope=dict(__name__='__main__',__file__=str(args.verify),DIAGNOSTICS=[],COOKIES=[],hashlib=hashlib)
sys.argv=[str(args.verify),'--neverc',str(args.neverc),'--output-dir',str(args.output_dir),'--target',args.target]
failure=None
try:
    exec(compile(tree,str(args.verify),'exec'),scope)
except Exception as error:
    failure=dict(type=type(error).__name__,message=str(error)[:1500])
    raise
finally:
    payload=dict(diagnostic_only=True, unchanged_stock_suite_required=True,
                 source_sha256=args.verify_sha256, unexpected_accepts=scope['DIAGNOSTICS'], cookie_observations=scope['COOKIES'], failure=failure)
    with args.diagnostics.open('x') as stream:
        json.dump(payload,stream,indent=2)
        stream.write('\n')
