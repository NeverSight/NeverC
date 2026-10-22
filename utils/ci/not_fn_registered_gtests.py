"""Restore only the four exact registered not_fn GTests for original-suite replay."""
import hashlib

ORIGINAL_SOURCE_SHA256 = '0099922e5f92464dde0ea0c4d9cd0359d0659eeb886a099d6f558c17ef8e70b5'
REGISTERED_SOURCE_SHA256 = '6fab1ae0e41fac1b3c351d2f23f9043c41e18cd641b0bb4620ef2c98acd55866'
GOLDEN_SUFFIX_SHA256 = 'a4be872bcc824c43b7962f2a341247286fe363b78f60beab56b28892e3905137'


def restore_original_gtests(source, additions):
    """Retain all original bytes; reject changed, missing or repeated additions."""
    digest = lambda data: hashlib.sha256(data).hexdigest()
    assert digest(additions) == GOLDEN_SUFFIX_SHA256
    if digest(source) == ORIGINAL_SOURCE_SHA256:
        return source
    assert digest(source) == REGISTERED_SOURCE_SHA256
    assert source.endswith(additions)
    original = source[:-len(additions)]
    assert digest(original) == ORIGINAL_SOURCE_SHA256
    return original
