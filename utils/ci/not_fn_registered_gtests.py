"""Restore only independently hashed registered not_fn GTest suffixes."""
import hashlib

ORIGINAL_SOURCE_SHA256 = '0099922e5f92464dde0ea0c4d9cd0359d0659eeb886a099d6f558c17ef8e70b5'
FOUR_REGISTERED_SOURCE_SHA256 = '6fab1ae0e41fac1b3c351d2f23f9043c41e18cd641b0bb4620ef2c98acd55866'
FIVE_REGISTERED_SOURCE_SHA256 = '67efba952e2e495682b7ad08e317a7bd3c4e77541d0c279f90c6e242a51d1e8c'
FOUR_GOLDEN_SUFFIX_SHA256 = 'a4be872bcc824c43b7962f2a341247286fe363b78f60beab56b28892e3905137'
FIVE_GOLDEN_SUFFIX_SHA256 = '79d803d7ead8ca27481ca76b4d91287209d2460fe49b148ddd38764b11a18fbe'


def restore_original_gtests(source, additions):
    """Reject changed, missing, repeated or mismatched registered additions."""
    digest = lambda data: hashlib.sha256(data).hexdigest()
    suffix_digest = digest(additions)
    if suffix_digest == FOUR_GOLDEN_SUFFIX_SHA256:
        registered_digest = FOUR_REGISTERED_SOURCE_SHA256
    else:
        assert suffix_digest == FIVE_GOLDEN_SUFFIX_SHA256
        registered_digest = FIVE_REGISTERED_SOURCE_SHA256
    if digest(source) == ORIGINAL_SOURCE_SHA256:
        return source
    assert digest(source) == registered_digest
    assert source.endswith(additions)
    original = source[:-len(additions)]
    assert digest(original) == ORIGINAL_SOURCE_SHA256
    return original
