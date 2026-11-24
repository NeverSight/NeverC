"""Retain original GoogleTest bodies and all non-test source without stubs."""
from pathlib import Path
import hashlib
import re


def sha(data):
    return hashlib.sha256(data).hexdigest()


def body_end(source, opening):
    """Lex real C++ braces, ignoring comments and escaped/raw literals."""
    assert source[opening:opening + 1] == b'{'
    depth, at = 1, opening + 1
    while at < len(source):
        token = source[at:at + 1]
        if source[at:at + 2] == b'//':
            end = source.find(b'\n', at + 2)
            at = len(source) if end < 0 else end + 1
            continue
        if source[at:at + 2] == b'/*':
            end = source.find(b'*/', at + 2)
            assert end >= 0, 'Unterminated C++ comment'
            at = end + 2
            continue
        if source[at:at + 2] == b'R"':
            end = source.find(b'(', at + 2)
            assert 0 <= end - at - 2 <= 16
            delimiter = source[at + 2:end]
            assert not any(c in delimiter for c in b' ()\\\t\r\n')
            closing = b')' + delimiter + b'"'
            end = source.find(closing, end + 1)
            assert end >= 0, 'Unterminated raw C++ literal'
            at = end + len(closing)
            continue
        if token in [b'"', b"'"]:
            # Apostrophes between numeric digits are digit separators, except
            # the quote in the u8 character-literal prefix.
            before, after = source[at - 1:at], source[at + 1:at + 2]
            if (token == b"'" and before in b'0123456789abcdefABCDEF'
                    and after in b'0123456789abcdefABCDEF'
                    and source[max(0, at - 2):at] != b'u8'):
                at += 1
                continue
            quote = token
            at += 1
            while at < len(source):
                if source[at:at + 1] == b'\\':
                    at += 2
                elif source[at:at + 1] == quote:
                    at += 1
                    break
                else:
                    at += 1
            else:
                raise AssertionError('Unterminated escaped C++ literal')
            continue
        if token == b'{':
            depth += 1
        elif token == b'}':
            depth -= 1
            if depth == 0:
                return at + 1
        at += 1
    raise AssertionError('Unterminated GoogleTest body')


def extract(source, selected_names):
    pattern = re.compile(rb'(?m)^TEST_F\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{')
    matches = list(pattern.finditer(source))
    if sha(source) == '71f05eab5c997e801174b09b261db0752041dcd4a7a19e10b805af00a9568b0f':
        expected_count = 8837
    elif sha(source) == 'b18ee73fa062befcb4f029a8c1e076be476d306a8567dc8f01e56b7386944094':
        expected_count = 8820
    elif sha(source) == '4e44a789e4a9597fca5fb62d252ce9290b7d018e019bda016525ba9641f98da5':
        expected_count = 8804
    elif sha(source) == '615020e719791e665aa2823bf4bd2e007176c2aebf65ab55c8ff693bff2f3d00':
        expected_count = 8788
    elif sha(source) == '8807f2fc6c47f571b715521d18d1c2f1aca6885e38b2de6260559ec67831e18d':
        expected_count = 8770
    elif sha(source) == '7f999b85b761decd27f7e336a4ae778552062c84936b797d0d834c864a31f26a':
        expected_count = 8757
    elif sha(source) == '7ba291b819182fba645dd7581f92b0a6629deabd4d7da9975b1a7b2da83c7abd':
        expected_count = 8748
    elif sha(source) == '2780fb91a65d18cf4059ff79ac2436884aa57f420b42296c99f3f0b80b69ebd7':
        expected_count = 8734
    elif sha(source) == '44d47c10a82a3d2ef973db9f9a87c2b5cce5389141a648093a5939e4bcf1d926':
        expected_count = 8694
    elif sha(source) == 'b0ebfd804a188b3740667ec710c8b116ed96e8924a57a6f66882051165586f48':
        expected_count = 8670
    elif sha(source) == 'b537c8295007d1c8b9bef82c985bc3b7db2f5701d9b9b24e3650c6a0d456962e':
        expected_count = 8655
    elif sha(source) == '92b5b5e9b171ff0c9f887fe4669f900809ff206624fc5e08df2c516d5dd33175':
        expected_count = 8651
    elif sha(source) == 'ad6cdfb5806a10416b5bec0e8a1c2d7f4e700c8a7bd317d057ae676eb7e52e68':
        expected_count = 8643
    elif sha(source) == 'a302000f528d3c79c9d2169d56f19ecc664d4c5245e897a333bb2f561a1c0f64':
        expected_count = 8631
    elif sha(source) == '518897789c68d3c75c2040ed9f914fde53c7dee14830d0891ba1f5c465b1ac89':
        expected_count = 8624
    elif sha(source) == '965da4c3ce1920bcf0f371fa02e17f56e8fcade8a67b7bbcef60058040053f16':
        expected_count = 8611
    elif sha(source) == '40a07a2f7ba2b85c8d134ae3d3d3a2ae1a52479182df85070c11a53307d580bc':
        expected_count = 8603
    elif sha(source) == '8ee172c3bf7dae6f0e65cce5d6c44e5bfda3406278f19ccffadfbdeacbc69edc':
        expected_count = 8588
    elif sha(source) == '600ae75bd4b6ceaa0e9a445adcf96712e00f83a9b504fee7d470953c3aa95f47':
        expected_count = 8578
    elif sha(source) == '856be15f1897ef27455fdd7d98857ddb130215430a02e637bc383507a3f1e382':
        expected_count = 8570
    elif sha(source) == '128ff7799af546d360def0abcb3e0ba34a382cd3629da0794750c8e444e5434c':
        expected_count = 8562
    elif sha(source) == '376aa6cafedc4544350af55c4762116affe6ffb8a1e3283ac406b8c9db064275':
        expected_count = 8552
    elif sha(source) == '59f5c6dc6377471051c60a473bcf81661b95045dc16b598e1c5d0fa55d2ddf79':
        expected_count = 8544
    elif sha(source) == 'b5861e602fe2e117514cbc02fbff1737609071bd6aa6d4452161039c5c032491':
        expected_count = 8536
    elif sha(source) == '91ca47fa646e424eed96bb7914230e20deb00af9f7afee5efc04af1e71492b9e':
        expected_count = 8528
    elif sha(source) == '83193b7e67658eecd8f05e1f39207b4c07eea93fabaf11c3fdfaa4259719510a':
        expected_count = 8520
    elif sha(source) == '0a65cf385c60ea9997fa815565a9cb56465fffddae54c41396bf63707b83fc46':
        expected_count = 8512
    elif sha(source) == 'a57a30b29ed03d9a47ce06015d225f01c1b486230d130ab63fc8ec3d3b1116a0':
        expected_count = 8504
    elif sha(source) == '5755793b0747f53c69ae511749f7e732123906f01e4b86cf93288415cb2fa7a9':
        expected_count = 8496
    elif sha(source) == '7103b1330261f496dad389a54d80f8d6be1864a02ae967b62c2fb4e32b06340a':
        expected_count = 8488
    elif sha(source) == '44fc110f1f40d86b7d188fe484d1a166c7da73e98f76edcd001d249395538cbf':
        expected_count = 8480
    elif sha(source) == '6d3886e9bdf1e29f2b9152a585312e5875beb45ef1616cf7c519cba7e76ae0b2':
        expected_count = 8472
    elif sha(source) == 'c5ff82c8dc13eb2e50d661a576629adc82e54a54d6485bf6441f491dacaeac06':
        expected_count = 8464
    elif sha(source) == 'ba8bed2d85cd298d5366b5b93516e4f955509abde146b71335c934aa9c50bc87':
        expected_count = 8456
    elif sha(source) == 'f891e8b10a7cd8b1272be7479bd65840ddbc3b601441bb84318e260497b7cdad':
        expected_count = 8448
    elif sha(source) == '77b784892333c39f8a9d1f795615d22e645480038b1ffaefc9e900856402aad2':
        expected_count = 8440
    elif sha(source) == 'f59b6b5c51fc70f851268ca84fd1accdf137b796b27c95213ac5e10418af5741':
        expected_count = 8432
    elif sha(source) == 'af6ac408f8952c76df507ba8cd681bc37e8caac5a6d4929a123187b498967475':
        expected_count = 8424
    elif sha(source) == 'd54d1ec315de5daf79db5c01d8e1dbd53a914fd11baf2c5b4388ee32201de29f':
        expected_count = 8416
    elif sha(source) == 'cd98c424f8e06373e80d1dc167574d4a61c2e2a23934bdd4e8987316cc6bf647':
        expected_count = 8408
    elif sha(source) == '484fbd59e4e3d7eac40f457d31331f446f1e9e29d217bc4d262945e25b091f7a':
        expected_count = 8400
    elif sha(source) == '7d57c282e737021daad1b73c5869c5a3658f65dbded610460e5e703148810ffc':
        expected_count = 8392
    elif sha(source) == 'e181edb71c0c639a568de03643a59c6f0b7d0a14c386cf59b7b59c82c80504e9':
        expected_count = 8384
    elif sha(source) == '92d39572e7134da39264c7046fee2ab75315632ce43552ac1d04cf7473a88896':
        expected_count = 8376
    elif sha(source) == 'fae0fb9ccf2cc94269c627dfff161db7092c3ae45dbf081309e168a8158fd5da':
        expected_count = 8366
    elif sha(source) == 'b4a0dd37f0dde5f034dd8c52c3474b2bd571f0766befa55fbe257312e8ccb2a5':
        expected_count = 8358
    elif sha(source) == '97626e342214f6eef8bd21ec028281d4bc2319f8749140a006b1e99559bf70d6':
        expected_count = 8350
    elif sha(source) == 'b981812e100743864d81cd802d9b0d314b60e59b2362aff71f6a635b11e0cee5':
        expected_count = 8342
    elif sha(source) == '5ef4d0491c68e82841467b5fd534a89ec5330e0220fc1c33babab7cf342c9e37':
        expected_count = 8334
    elif sha(source) == '4ea24098e82989877d78a96781e0fcd959889139da7c31491c27a2eceb946f60':
        expected_count = 8326
    elif sha(source) == 'bdf19d17388b4157ca9e10e6b4454c833104e8a331a09b0924694c19718d994e':
        expected_count = 8318
    elif sha(source) == '0a33208d656054ec6265c968c1c3c9ed504da77d0709bb325cc0ecdd9bded1a5':
        expected_count = 8310
    elif sha(source) == 'b9b9b2935d5da5039a8934541b708a06a32b58e0f1070850b729cdc9b9f76a6d':
        expected_count = 8302
    elif sha(source) == '1df1f37351188954c2cd051342565e4e2ee0ec53ede1404d9eeb971f05c29ae4':
        expected_count = 8294
    elif sha(source) == 'f19e1177591283d289bfd01e07fb890c63cb715e9c631b27490566f78912a0cc':
        expected_count = 8286
    elif sha(source) == '428d544caff0c79322d8d0a239a9680dc0c01a016e357f8506aebf19a683b702':
        expected_count = 8278
    elif sha(source) == '1e41b38a7df2478c43bf82959c64a4633b553bc93617880d9ef6d2e9c4a7c462':
        expected_count = 8270
    elif sha(source) == '1a39c3d2d7c96ed5470131a21ef92d8b098fe9d56ecb012ed91cd0ecc9d540d7':
        expected_count = 8263
    elif sha(source) == '4ebe1755104459f128be4ab723a1898ac57af93ceffb33a885f9cbd46f924d69':
        expected_count = 8256
    elif sha(source) == '044375f31196b86b6225261c611c6f8100a8ec7ab0219709cfffe35e44d6daed':
        expected_count = 8248
    elif sha(source) == '079938a41465897ea07a93d6707738c9173278fee37980e4ee3757da699028eb':
        expected_count = 8244
    elif sha(source) == 'd46e00876290437ca25e5bb20bee825ac7fefe03bd6447d02e060695381d35f9':
        expected_count = 8240
    elif sha(source) == 'bbdd93fd3a492641167ec5618c88f5dd8fff5115e940b8737487acab8ef4c73e':
        expected_count = 8234
    elif sha(source) == 'bb50602a198d000bfebf68c70354d6deac438fcee8db75a6e2c28e12703fc1d0':
        expected_count = 8228
    elif sha(source) == '91d19f5afd022ffacdcb11a0d764e6bb0e308db2d969cc51cf1cd3156953bba3':
        expected_count = 8206
    elif sha(source) == 'b41ccc064da285dc98be686407bd95e978596261c5baf6b23b57afb6f9716dd4':
        expected_count = 8193
    elif sha(source) == '5ea4dd16eb081c1ae850c9041998ca7c81b9520eb9e68cdfd75b76fe06c7c215':
        expected_count = 8191
    elif sha(source) == 'a440054763014f0dc34b4964cfd525447e5a4df69d6ba2ca69f9eab19e5c8346':
        expected_count = 8185
    elif sha(source) == '76e71d65563703e0372a79fc12bfa65883aea738411972f16dc3d06ba863072f':
        expected_count = 8179
    elif sha(source) == 'cb1e66caf89902b5ff655e287f1bc206b3dfee1f5e39c0c2a62c306577373851':
        expected_count = 8172
    elif sha(source) == '2d1d01058bae252d865258eddeecdba869624a3ef3d75fef98d09054936dd508':
        expected_count = 8158
    elif sha(source) == '8cb9ba423920483c71e8c728341a619b5661dfe1bf808c84d11b68f378ec7d66':
        expected_count = 8131
    elif sha(source) == '20c9f075f5b6d3a2f1394438f689368c7f2da1a75286dce1704b219e87e6d5df':
        expected_count = 8119
    elif sha(source) == '71ea495d9ecd713746a7e5fca2acf69633b5b264ff17437bc0252078575cc947':
        expected_count = 8097
    else:
        expected_count = 8043 if sha(source) in {'b94a4b7d501a8060a7484729768cbaf0ae4e7a76d4f3abbb4fc96f0b94646bd0', '2a750a4f20ba6f0674466279aa069b55d0dc71afd4062fc701b22cf286e7c49b'} else (8018 if sha(source) == 'e51b392c0556e228dc98fa264fa5fc80232273c8bf68b4645b4f1c78b9cd1951' else (7998 if sha(source) == '1dbff1fae5daafe294034608d7cf1d9e0117c2b9b760b3c1d1af614bdd5ea49a' else (7986 if sha(source) == '74d94aedd2955dc195f554dfb82ff9fd3572f2554078eff49f521a205ea92be2' else (7913 if sha(source) == 'c0fc72639d8af8570b3e7d545de2e008d1d2efdd7ec0bd947ef7b0ead94ef2b2' else (7886 if sha(source) == '7be41ba79cf42d57a4686a0741e1f8ca4a2b6f75ce61f2028e55ff971261f0dd' else (7830 if sha(source) == '865a77f3c06eff31e2a1c905487cb44ae4b626b6642e8639c5f8b4ccc4c51535' else (7788 if sha(source) == '8f1fa8e34a5758b14e300009995b640ac0f931e16bb0909fc207f88f97a70014' else (7772 if sha(source) == 'f7034c728986dbaea562fe6ac871c6b9c9d9b71c3df214ebaef2306ac8066bfa' else (7762 if sha(source) == '9d42a1b9c2c0aacabdee67e345e5f9861cb70f6742e5897a133dce0877099870' else (7752 if sha(source) == '7fe5f01d7e6e8b808796ba6d60a3e086042c075429399a6f0f0bbd777ba32448' else (7742 if sha(source) == 'dab765c07a980c8f65fb2925b11df21886aaa0336956156339e04f40c54cc213' else (7732 if sha(source) == 'b4b3d48914d151ba193d0027f2a57876570bdf91163ba331c0e5898a4717545e' else (7722 if sha(source) == '165b3cf63c6843a33e856b2249ac44313e06de683da79c21fad91587ab2a6b71' else (7712 if sha(source) == '89763d0ef80e1d5c1601500e6d1f6e581caf82301292ca249f6ebeb89ee08d26' else (7702 if sha(source) == '24c4a41ce4f2c53230d3c2db1383cf11c4093b03665132d9f7e4088f8a7ac6dc' else (7692 if sha(source) == '2b947d22f6a442a9dda86fa3bc2f54668ef50106e208a40460830d32f32131a3' else (7682 if sha(source) == 'a7bfc90042c2c08510081c8e46c3ae612ac3de3d6de3f331c4750888d6d55d02' else (7672 if sha(source) == '55e1ef9b846da39792666567def634fef22a7aed0e15bac74e3e6b91459e43ae' else (7662 if sha(source) == '1986f8a1b8783949442396a0e757c52d73af54ca7d425014375598d6501a19a4' else (7652 if sha(source) == '46331fdc4f910d1edb51e9a6801151cb8339a60885f4b346500310858e38c56b' else (7642 if sha(source) == '5b6c619e94bccfa7327ded8c23e3ceb0cba594ecf8650acdbf7429c72bb9cf5c' else (7632 if sha(source) == 'fcab2527d50a98b9425902a4897a3e8b5600554947d04516c09173fae697b7d9' else (7622 if sha(source) == 'b214d59f1b1a42ebb6fad5eb859aecbdda973b596a6ded29a3444bbf1108fb08' else (7612 if sha(source) == 'ec04f1f8680519bb4ca25a8bd47998c292e4717da44826fccc6b476463b824c9' else (7602 if sha(source) == 'aa10a69f4c0902212f0c1b08063294e9bb8270dfe708bdc3e0011bafb2eb99f0' else (7592 if sha(source) == '2f6296ebbabc79f1a74007f41b33557c0e8f068209bcb5deb3c0128953731e4d' else (7582 if sha(source) == '290bfbd9b89cb8c15185c13d43b6d559e4d2037a7502e3211a4677d4d9c5a5ec' else (7572 if sha(source) == '8c9074b42025e91e0d4b9cdec291b890c17877deb6f5f2ecfb185bab4a32abd0' else (7562 if sha(source) == 'e918c2f8ee5d9bc260a95fa010842b93572c5c8b844cedab0f9bff6d69ef849d' else (7552 if sha(source) == '6b9f8e9f180e15630e45a9f3a625a106091b021d530e54ac4a8c02a3676171cd' else (7552 if sha(source) == 'b3aaaaee7a1d4990a2972a247dcd2052352b0170c06b0065567c4cdb0bfcd6f5' else (7542 if sha(source) == 'f78087a41d7783873f9080d002099fb2289aa8661975da0efc9487c8a74379df' else (7532 if sha(source) == 'be5ce8350e32f64b17b9db709b4721d2cb5923bc5a36171431a5b441d072509f' else (7522 if sha(source) == '99164b5b4e996784afc538467afe5afe2f7205d31a6af5cd9a59d21aa9582d21' else (7512 if sha(source) == '0fef0914482ab677ee540f2ebbf62bed291cc9a49af17e8d08de4d2dd25afc63' else (7502 if sha(source) == '93fefb63333bea119087a1265f504b3d20f6db1e4ccdf794021a973899d0be50' else (7500 if sha(source) == 'c758bb163adc94a8c5988a3093cddb16c78c0bf500948f9a0c136962088db782' else (7496 if sha(source) in {'748a1966c60985df355d52f5746a7610b155aec2e1480a55144efb52a9c054b8', '7a86aa397d598346ab9ef84f05098452ed0c41c16852586e7ce6ee4f5b1fc31a'} else 7493))))))))))))))))))))))))))))))))))))))
    assert len(matches) == expected_count
    assert len(set(selected_names)) == len(selected_names)
    selected = set(selected_names)
    available = {m[1].decode() + '.' + m[2].decode() for m in matches}
    assert len(available) == expected_count and selected <= available
    parts, kept, excluded, at = [], {}, {}, 0
    for index, match in enumerate(matches):
        assert at <= match.start()
        parts.append(source[at:match.start()])
        end = body_end(source, match.end() - 1)
        if index + 1 < len(matches):
            assert end <= matches[index + 1].start()
        name = match[1].decode() + '.' + match[2].decode()
        body = source[match.start():end]
        record = {'start': match.start(), 'end': end, 'bytes': len(body), 'sha256': sha(body)}
        if name in selected:
            parts.append(body)
            kept[name] = record
        else:
            excluded[name] = record
        at = end
    parts.append(source[at:])
    generated = b''.join(parts)
    assert set(kept) == selected
    selected_matches = list(pattern.finditer(generated))
    assert [m[1].decode() + '.' + m[2].decode() for m in selected_matches] == list(kept)
    for match in selected_matches:
        name = match[1].decode() + '.' + match[2].decode()
        end = body_end(generated, match.end() - 1)
        assert sha(generated[match.start():end]) == kept[name]['sha256']
    return generated, {'original_source_sha256': sha(source), 'generated_source_sha256': sha(generated),
        'original_test_names': expected_count, 'selected_count': len(kept),
        'selected_original_test_bodies': kept, 'nonselected_original_test_bodies': excluded,
        'all_non_test_source_retained': True, 'selected_bodies_modified': False,
        'whole_original_suite_executed': False, 'formal_xml_created': False}
