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
tree=ast.parse(text.replace(old,replacement))
scope=dict(__name__='__main__',__file__=str(args.verify),DIAGNOSTICS=[],hashlib=hashlib)
sys.argv=[str(args.verify),'--neverc',str(args.neverc),'--output-dir',str(args.output_dir),'--target',args.target]
failure=None
try:
    exec(compile(tree,str(args.verify),'exec'),scope)
except Exception as error:
    failure=dict(type=type(error).__name__,message=str(error)[:1500])
    raise
finally:
    payload=dict(diagnostic_only=True, unchanged_stock_suite_required=True,
                 source_sha256=args.verify_sha256, unexpected_accepts=scope['DIAGNOSTICS'], failure=failure)
    with args.diagnostics.open('x') as stream:
        json.dump(payload,stream,indent=2)
        stream.write('\n')
