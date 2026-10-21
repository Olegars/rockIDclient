extern "C" int crypto_scalarmult(unsigned char *q, const unsigned char *n, const unsigned char *p);
extern "C" int crypto_scalarmult_base(unsigned char *q, const unsigned char *n);

namespace {

int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool parse(const char *text, unsigned char *out)
{
    for (int i = 0; i < 32; ++i) {
        const int hi = hex(text[i * 2]);
        const int lo = hex(text[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

}  // namespace

bool rockX25519SelfTest()
{
    const char *alice = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
    const char *bob = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
    const char *alicePub = "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
    const char *bobPub = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
    const char *shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
    unsigned char a[32], b[32], ap[32], bp[32], expectA[32], expectB[32], expectS[32], s1[32], s2[32];
    if (!parse(alice, a) || !parse(bob, b) || !parse(alicePub, expectA) || !parse(bobPub, expectB) || !parse(shared, expectS))
        return false;
    crypto_scalarmult_base(ap, a);
    crypto_scalarmult_base(bp, b);
    crypto_scalarmult(s1, a, bp);
    crypto_scalarmult(s2, b, ap);
    for (int i = 0; i < 32; ++i) {
        if (ap[i] != expectA[i] || bp[i] != expectB[i] || s1[i] != expectS[i] || s1[i] != s2[i])
            return false;
    }
    return true;
}
