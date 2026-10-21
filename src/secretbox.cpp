#include "secretbox.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <windows.h>
#include <bcrypt.h>

extern "C" int crypto_scalarmult(unsigned char *q, const unsigned char *n, const unsigned char *p);

#include <algorithm>
#include <cstring>

namespace secretbox {
namespace {

bool ok(NTSTATUS status)
{
    return status >= 0;
}

QByteArray sha256(const QByteArray &data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return {};
    if (!ok(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())),
                   static_cast<ULONG>(data.size()), 0);
    QByteArray out(32, 0);
    BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()), 32, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

QByteArray pad32(QByteArray secret)
{
    if (secret.size() > 32)
        secret = secret.right(32);
    if (secret.size() < 32)
        secret.prepend(QByteArray(32 - secret.size(), '\0'));
    return secret;
}

}  // namespace

bool generate(Identity *out)
{
    if (!out)
        return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0)))
        return false;
    if (!ok(BCryptGenerateKeyPair(alg, &key, 256, 0)) || !ok(BCryptFinalizeKeyPair(key, 0))) {
        if (key)
            BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    ULONG privLen = 0;
    ULONG pubLen = 0;
    BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &privLen, 0);
    BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &pubLen, 0);
    QByteArray priv(static_cast<int>(privLen), 0);
    QByteArray pub(static_cast<int>(pubLen), 0);
    const bool exported =
        ok(BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, reinterpret_cast<PUCHAR>(priv.data()), privLen,
                           &privLen, 0))
        && ok(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(pub.data()), pubLen,
                              &pubLen, 0));
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!exported || pub.size() < 72)
        return false;
    out->privBlob = priv;
    out->publicRaw = pub.mid(8, 64);
    return out->publicRaw.size() == 64;
}

QByteArray agree(const QByteArray &privBlob, const QByteArray &peerRaw64)
{
    if (privBlob.isEmpty() || peerRaw64.size() != 64)
        return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0)))
        return {};
    BCRYPT_KEY_HANDLE mine = nullptr;
    BCRYPT_KEY_HANDLE peer = nullptr;
    if (!ok(BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &mine,
                                reinterpret_cast<PUCHAR>(const_cast<char *>(privBlob.constData())),
                                static_cast<ULONG>(privBlob.size()), 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    QByteArray pub(8 + 64, 0);
    const ULONG magic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
    const ULONG coord = 32;
    std::memcpy(pub.data(), &magic, 4);
    std::memcpy(pub.data() + 4, &coord, 4);
    std::memcpy(pub.data() + 8, peerRaw64.constData(), 64);
    if (!ok(BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &peer,
                                reinterpret_cast<PUCHAR>(pub.data()), static_cast<ULONG>(pub.size()), 0))) {
        BCryptDestroyKey(mine);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCRYPT_SECRET_HANDLE shared = nullptr;
    if (!ok(BCryptSecretAgreement(mine, peer, &shared, 0))) {
        BCryptDestroyKey(mine);
        BCryptDestroyKey(peer);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    ULONG rawLen = 0;
    BCryptDeriveKey(shared, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &rawLen, 0);
    QByteArray raw(static_cast<int>(rawLen), 0);
    if (!ok(BCryptDeriveKey(shared, BCRYPT_KDF_RAW_SECRET, nullptr, reinterpret_cast<PUCHAR>(raw.data()), rawLen,
                            &rawLen, 0))
        || raw.size() < 4) {
        BCryptDestroySecret(shared);
        BCryptDestroyKey(mine);
        BCryptDestroyKey(peer);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    ULONG secretLen = 0;
    std::memcpy(&secretLen, raw.constData(), 4);
    QByteArray secret = raw.mid(4, static_cast<int>(secretLen));
    std::reverse(secret.begin(), secret.end());
    BCryptDestroySecret(shared);
    BCryptDestroyKey(mine);
    BCryptDestroyKey(peer);
    BCryptCloseAlgorithmProvider(alg, 0);
    return sha256(pad32(secret));
}

QString seal(const QByteArray &key, const QString &plain)
{
    if (key.size() != 32)
        return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return {};
    if (!ok(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    if (!ok(BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())), 32, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    UCHAR nonce[12];
    UCHAR tag[16];
    BCryptGenRandom(nullptr, nonce, sizeof nonce, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce;
    info.cbNonce = sizeof nonce;
    info.pbTag = tag;
    info.cbTag = sizeof tag;
    const QByteArray utf8 = plain.toUtf8();
    ULONG outLen = 0;
    BCryptEncrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(utf8.constData())),
                  static_cast<ULONG>(utf8.size()), &info, nullptr, 0, nullptr, 0, &outLen, 0);
    QByteArray cipher(static_cast<int>(outLen), 0);
    if (!ok(BCryptEncrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(utf8.constData())),
                          static_cast<ULONG>(utf8.size()), &info, nullptr, 0,
                          reinterpret_cast<PUCHAR>(cipher.data()), outLen, &outLen, 0))) {
        BCryptDestroyKey(hkey);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    cipher.resize(static_cast<int>(outLen));
    QByteArray packed;
    packed.append(reinterpret_cast<const char *>(nonce), 12);
    packed.append(cipher);
    packed.append(reinterpret_cast<const char *>(tag), 16);
    BCryptDestroyKey(hkey);
    BCryptCloseAlgorithmProvider(alg, 0);
    return QStringLiteral("e2e:") + QString::fromLatin1(packed.toBase64());
}

QString open(const QByteArray &key, const QString &wire)
{
    if (key.size() != 32 || !wire.startsWith(QStringLiteral("e2e:")))
        return {};
    const QByteArray packed = QByteArray::fromBase64(wire.mid(4).toLatin1());
    if (packed.size() < 12 + 16)
        return {};
    const QByteArray nonce = packed.left(12);
    const QByteArray tag = packed.right(16);
    const QByteArray cipher = packed.mid(12, packed.size() - 28);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return {};
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!ok(BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())), 32, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
    info.cbNonce = 12;
    info.pbTag = reinterpret_cast<PUCHAR>(const_cast<char *>(tag.constData()));
    info.cbTag = 16;
    ULONG outLen = 0;
    BCryptDecrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(cipher.constData())),
                  static_cast<ULONG>(cipher.size()), &info, nullptr, 0, nullptr, 0, &outLen, 0);
    QByteArray plain(static_cast<int>(outLen), 0);
    const NTSTATUS status = BCryptDecrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(cipher.constData())),
                                          static_cast<ULONG>(cipher.size()), &info, nullptr, 0,
                                          reinterpret_cast<PUCHAR>(plain.data()), outLen, &outLen, 0);
    BCryptDestroyKey(hkey);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok(status))
        return {};
    plain.resize(static_cast<int>(outLen));
    return QString::fromUtf8(plain);
}

QString openParts(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipherAndTag)
{
    if (key.size() != 32 || nonce.size() != 12 || cipherAndTag.size() < 16)
        return {};
    const QByteArray tag = cipherAndTag.right(16);
    const QByteArray cipher = cipherAndTag.left(cipherAndTag.size() - 16);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    if (!ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return {};
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!ok(BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())), 32, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
    info.cbNonce = 12;
    info.pbTag = reinterpret_cast<PUCHAR>(const_cast<char *>(tag.constData()));
    info.cbTag = 16;
    ULONG outLen = 0;
    BCryptDecrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(cipher.constData())),
                  static_cast<ULONG>(cipher.size()), &info, nullptr, 0, nullptr, 0, &outLen, 0);
    QByteArray plain(static_cast<int>(outLen > 0 ? outLen : 1), 0);
    const NTSTATUS status = BCryptDecrypt(hkey, reinterpret_cast<PUCHAR>(const_cast<char *>(cipher.constData())),
                                          static_cast<ULONG>(cipher.size()), &info, nullptr, 0,
                                          reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()),
                                          &outLen, 0);
    BCryptDestroyKey(hkey);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok(status))
        return {};
    plain.resize(static_cast<int>(outLen));
    return QString::fromUtf8(plain);
}

QString openRk1(const QByteArray &priv32, qint64 userId, const QString &sealedJson)
{
    if (priv32.size() != 32 || userId <= 0 || !sealedJson.startsWith(QStringLiteral("{\"e\":1")))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(sealedJson.toUtf8()).object();
    if (root.value(QStringLiteral("e")).toInt() != 1)
        return {};
    const QJsonArray seals = root.value(QStringLiteral("seals")).toArray();
    for (const QJsonValue &value : seals) {
        const QJsonObject row = value.toObject();
        if (static_cast<qint64>(row.value(QStringLiteral("user_id")).toDouble()) != userId)
            continue;
        const QStringList parts = row.value(QStringLiteral("box")).toString().split(QLatin1Char('.'));
        if (parts.size() != 4 || parts.at(0) != QStringLiteral("rk1"))
            return {};
        const QByteArray epub = QByteArray::fromBase64(parts.at(1).toLatin1());
        const QByteArray nonce = QByteArray::fromBase64(parts.at(2).toLatin1());
        const QByteArray boxed = QByteArray::fromBase64(parts.at(3).toLatin1());
        if (epub.size() != 32 || nonce.size() != 12 || boxed.size() < 16)
            return {};
        QByteArray shared(32, 0);
        crypto_scalarmult(reinterpret_cast<unsigned char *>(shared.data()),
                          reinterpret_cast<const unsigned char *>(priv32.constData()),
                          reinterpret_cast<const unsigned char *>(epub.constData()));
        return openParts(sha256(shared), nonce, boxed);
    }
    return {};
}

}  // namespace secretbox
