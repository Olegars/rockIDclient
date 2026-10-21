#include <windows.h>
#include <bcrypt.h>

extern "C" void randombytes(unsigned char *out, unsigned long long length)
{
    unsigned long long done = 0;
    while (done < length) {
        const unsigned long long left = length - done;
        const ULONG chunk = left > 1048576ull ? 1048576u : static_cast<ULONG>(left);
        if (BCryptGenRandom(nullptr, out + done, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return;
        done += chunk;
    }
}
