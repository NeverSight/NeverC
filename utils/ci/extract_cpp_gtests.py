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
    assert len(matches) == 7493
    assert len(set(selected_names)) == len(selected_names)
    selected = set(selected_names)
    available = {m[1].decode() + '.' + m[2].decode() for m in matches}
    assert len(available) == 7493 and selected <= available
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
        'original_test_names': 7493, 'selected_count': len(kept),
        'selected_original_test_bodies': kept, 'nonselected_original_test_bodies': excluded,
        'all_non_test_source_retained': True, 'selected_bodies_modified': False,
        'whole_original_suite_executed': False, 'formal_xml_created': False}
