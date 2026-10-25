"""Restore only independently hashed registered not_fn GTest suffixes."""
import hashlib

ORIGINAL_SOURCE_SHA256 = '0099922e5f92464dde0ea0c4d9cd0359d0659eeb886a099d6f558c17ef8e70b5'
FOUR_REGISTERED_SOURCE_SHA256 = '6fab1ae0e41fac1b3c351d2f23f9043c41e18cd641b0bb4620ef2c98acd55866'
FIVE_REGISTERED_SOURCE_SHA256 = '67efba952e2e495682b7ad08e317a7bd3c4e77541d0c279f90c6e242a51d1e8c'
SIX_REGISTERED_SOURCE_SHA256 = '2fa44b258cb1ef0bbbd45435f5150673f35a46f1574d7a724ede6668e57daac2'
TWELVE_REGISTERED_SOURCE_SHA256 = '006fa380968b6acc0c7c8d99ff7a1c5a59196045e08934805550b649966bbf7d'
ELEVEN_REGISTERED_SOURCE_SHA256 = 'bcae7353fcc16f7d198d72c9a7a3f0d92caff4d5e24b5d520c64ab8bcb25d87a'
TEN_REGISTERED_SOURCE_SHA256 = '694506523a109084f1a5a1ce910525915a4855936dc7687715cee2eb34f39271'
FOUR_GOLDEN_SUFFIX_SHA256 = 'a4be872bcc824c43b7962f2a341247286fe363b78f60beab56b28892e3905137'
FIVE_GOLDEN_SUFFIX_SHA256 = '79d803d7ead8ca27481ca76b4d91287209d2460fe49b148ddd38764b11a18fbe'
SIX_GOLDEN_SUFFIX_SHA256 = '7b85dcd35b2ed3581e4baf3a6b3318f471cf815e26237b516b672f4b421c8e5c'
TWELVE_GOLDEN_SUFFIX_SHA256 = '1773950ecb88dc8436566bb9e2e1bf7601deffb7527ffedfd051bdc9feb4b0c1'
ELEVEN_GOLDEN_SUFFIX_SHA256 = '69c4582879c7ffd6a65315a9882a9d42a96ff2b9ccb10a43819384f0356751f8'
TEN_GOLDEN_SUFFIX_SHA256 = '7df227f8aa712296687c703d6dcc0d9cfbca678a3c3ed9f033c8cddc7417c22e'


def restore_original_gtests(source, additions):
    """Reject changed, missing, repeated or mismatched registered additions."""
    digest = lambda data: hashlib.sha256(data).hexdigest()
    suffix_digest = digest(additions)
    if suffix_digest == FOUR_GOLDEN_SUFFIX_SHA256:
        registered_digest = FOUR_REGISTERED_SOURCE_SHA256
    elif suffix_digest == FIVE_GOLDEN_SUFFIX_SHA256:
        registered_digest = FIVE_REGISTERED_SOURCE_SHA256
    elif suffix_digest == SIX_GOLDEN_SUFFIX_SHA256:
        registered_digest = SIX_REGISTERED_SOURCE_SHA256
    elif suffix_digest == TEN_GOLDEN_SUFFIX_SHA256:
        registered_digest = TEN_REGISTERED_SOURCE_SHA256
    elif suffix_digest == ELEVEN_GOLDEN_SUFFIX_SHA256:
        registered_digest = ELEVEN_REGISTERED_SOURCE_SHA256
    else:
        assert suffix_digest == TWELVE_GOLDEN_SUFFIX_SHA256
        registered_digest = TWELVE_REGISTERED_SOURCE_SHA256
    if digest(source) == ORIGINAL_SOURCE_SHA256:
        return source
    assert digest(source) == registered_digest
    assert source.endswith(additions)
    original = source[:-len(additions)]
    assert digest(original) == ORIGINAL_SOURCE_SHA256
    return original
