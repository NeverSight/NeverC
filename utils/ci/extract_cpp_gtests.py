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
    expected_count = 7552 if sha(source) == '6b9f8e9f180e15630e45a9f3a625a106091b021d530e54ac4a8c02a3676171cd' else (7552 if sha(source) == 'b3aaaaee7a1d4990a2972a247dcd2052352b0170c06b0065567c4cdb0bfcd6f5' else (7542 if sha(source) == 'f78087a41d7783873f9080d002099fb2289aa8661975da0efc9487c8a74379df' else (7532 if sha(source) == 'be5ce8350e32f64b17b9db709b4721d2cb5923bc5a36171431a5b441d072509f' else (7522 if sha(source) == '99164b5b4e996784afc538467afe5afe2f7205d31a6af5cd9a59d21aa9582d21' else (7512 if sha(source) == '0fef0914482ab677ee540f2ebbf62bed291cc9a49af17e8d08de4d2dd25afc63' else (7502 if sha(source) == '93fefb63333bea119087a1265f504b3d20f6db1e4ccdf794021a973899d0be50' else (7500 if sha(source) == 'c758bb163adc94a8c5988a3093cddb16c78c0bf500948f9a0c136962088db782' else (7496 if sha(source) in {'748a1966c60985df355d52f5746a7610b155aec2e1480a55144efb52a9c054b8', '7a86aa397d598346ab9ef84f05098452ed0c41c16852586e7ce6ee4f5b1fc31a'} else 7493))))))))
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
