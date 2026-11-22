"""Collect successful responses to stale negative protocol cases for diagnosis."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import sys

def retained_shape(module):
    import collections
    projection = {kind: module[kind] for kind in ('records', 'globals', 'functions')}
    if len(json.dumps(projection, separators=(',', ':')).encode()) <= 16384:
        return dict(module=projection)
    return dict(
        records=projection['records'],
        globals=[{k: value[k] for k in ('name', 'type')} for value in projection['globals']],
        functions=[dict(name=fn['name'], result=fn['result'],
                        params=[p['type'] for p in fn['params']],
                        operations=dict(collections.Counter(row['op'] for row in fn['body'])))
                   for fn in projection['functions']])


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
                    observed = DIAGNOSTICS[-1]
                    if name in REMAINING_SOURCE and observed['source_sha256'] == REMAINING_SOURCE[name]:
                        observation = retained_shape(data)
                        assert len(json.dumps(observation, separators=(',', ':')).encode()) <= 16384
                        observed['witness_ir'] = observation
            else:
                assert result.returncode and code in codes, (name, result.returncode, data)
"""
text=original.decode()
assert text.count(old)==1
tree=ast.parse(text.replace(old,replacement))
scope=dict(__name__='__main__',__file__=str(args.verify),DIAGNOSTICS=[],hashlib=hashlib,retained_shape=retained_shape,REMAINING_SOURCE={'v2-reference-member-reject-function-reference': '0e64105139dbf64d4e9b9042d91d6092a9e1356510a38db24d90a8737161d3e4', 'v2-constexpr-if-missing-discarded-function': 'a9384f7e97c6682ab9424c35acf0065b1c8cc5de46809cc7b071e2f7311b7a8e', 'v2-member-templates-reject-member-function-pointer': '9793100b24e21b40aa51895c183dc04213073b2dfeb41571a299fbfbb60038c8', 'v2-instantiated-friends-reject-friend-address': '99f754d7802b5eb3f04e409d7032489fa8c9dd3a3d681eb0a32bb74474ac7b3c', 'v2-static-reference-reject-function-reference': '2da10c0c4b3fd845c4aad907c8ff4f62e325327f64b2d3b36b183649f923ac87', 'v2-static-pointer-reject-global-heap': 'a3b05ef3f31ff8d33f99eff0eaf10507560fbee5586ae814b69058d66c63a1a0', 'v2-vector-runtime-object': '13413152050f88ef36f1aa364084f00104a0376baea3b2d9e44cae1959ae90c6', 'v2-vector-function-pointer': '030a3a44e29af770850158bfba72bded405b03860682ba8e9151b54e8d5d20d7', 'v2-vector-pointer-order': 'bcf5c6c9fff23c03a78005d0054b9a47fb54be3f6a325cf0b52fccc3436e2e39', 'v2-wrapped-fill-record': 'cf5fc24da23667a979d0d01d7d5012bb6f3e40260496a69b5dbce2f37cbe6fcb', 'v2-wrapped-reverse-record': '437a5693486010a50bfe14966d6cc869b2cfa5503ab64c42a9bb7ce5a558e82b', 'v2-wrapped-numeric-inner-product-object-pair': 'd3b9710b157d4a3ee8eb25b7d27f94c6ad74f18c22e3118763ca9d4369fe8c7a', 'v2-wrapped-numeric-transform-reduce-object-pair': 'c763084bd32ce0553e1dc8e418a87b9703a038634f84b2355ff354d7e0d5c658', 'v2-wrapped-numeric-transform-reduce-unary-object-pair': '05b2304cb7b1df16766173546befc1d21f79b4956cf90aeeb3424075538e9279', 'v2-wrapped-transfer-copy-record': '4148c86c476ec3a52633672a9480d6c7268e47329034942085f443e09b30d430', 'v2-wrapped-transfer-copy-n-record': '841927129beaa8b46b25645b795863196e70e93c55580c42d9f3fee7ee151f3c', 'v2-wrapped-transfer-fill-n-record': 'a043d5fccec4c38c86ba6839ba89ce78ee3e20e151c6a68d7c004c898b0ce8cb', 'v2-wrapped-transfer-swap-ranges-record': '1be883f9737d9ac0baa7c8e6b5da70b213788a7b61d80e62f28738a920630463', 'v2-wrapped-transfer-iter-swap-record': 'dde688392695d350a958dc74db060d36802f92d511d0b4ad1cbf2ae670e66e42', 'v2-string-runtime-object': '8ce834a8c33edf6f66cb62231e6198c13b825fa4e36b2285122305baaf320297', 'v2-deleted-reject-missing-default': 'c9b014ff9d1e6d27c14a3540c4f780d861c1e252c668a1e4d73d000f092f9c3c', 'v2-function-pointers-reject-record-by-value-argument': '437d9370137579e9b17d5b15d384e480d5f32615acda2d579f02f53a48815da1', 'v2-function-pointers-reject-record-by-value-result': 'a15ba181d1308150ef4f106e2dc4aa20f7368cc806514a4442b030187aceef8d', 'v2-function-pointers-reject-function-reference': '84c3089a216a66bb14a3677d08d8415a47e1d2fdd119922891beb04b8b5ce811', 'v2-function-pointers-reject-nonstatic-member': 'bf8d294c8f4d5aeadb777f91d3ae819fb397364bcde8c9700ac44a10ea8e8fb1', 'v2-template-function-pointers-reject-record-value-argument': 'b236df85384a1ebe41eaab0792df837675009b7f39edd1be5e2ce61917e7e12b', 'v2-template-function-pointers-reject-record-value-result': 'af1c7ad00ec906b5cac14347bc225def280c3db599fb5e6a5f668da98faff6e6', 'v2-template-function-pointers-reject-function-reference': '2112525d8890f3318a0df991ff9bade9bd3c4f177b0c021d580fc53ab7b2b1fd', 'v2-template-function-pointers-reject-nonstatic-member': '4a63ee2ae49eb2cd0e70ecea524fb8084d61a5bfa0a01073c7f5fc6e8ee6d123', 'v2-template-function-pointers-reject-pointer-nontype-argument': 'f38b17b6eba1e3e9a290b2fbc74de4d509c8411d956f2dc917141f528cd282ae', 'v2-callback-variable-templates-reject-record-callback-signature': 'b72da8aa72106446516141ccf6e5eac144f7eb10fd4b757a65f2f7b352fd7d6f', 'v2-callback-variable-templates-reject-pointer-nontype-argument': '1942831ca357f4bb6ddd2d40724297404d1517eccccc40a85af3825e36734bad', 'v2-non-type-templates-reject-function-parameter': 'b940e43616e1c74ce54a927b68075f43c0c203bfb2203f32341023a464024bd1', 'v2-function-templates-missing-declared-signature-ordinary-function': 'bc2cd926aad301ab8d199e56bb560d0424273c70b6b8e4bc52213b83c3fb7c43', 'v2-default_argument_missing-default-call': '36ee5cf9cb45c342a0cd96f35077a1dae5b008a5c6f74cd319f353b68b71804f', 'v2-default_argument_missing-overridden-missing': 'ae2bc5ee1018fad26510d847bc9c17722a0dc573699c2d9a344189cff38d9cae'})
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
        json.dump(payload,stream,separators=(',', ':'))
        stream.write('\n')
