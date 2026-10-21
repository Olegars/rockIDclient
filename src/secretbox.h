#pragma once

#include <QByteArray>
#include <QString>

// Общий с Android секрет: ECDH P-256 и AES-256-GCM.
// Публичный ключ — 64 байта X||Y. На проводе текст начинается с "e2e:".
namespace secretbox {

struct Identity {
    QByteArray privBlob;
    QByteArray publicRaw;
};

bool generate(Identity *out);
QByteArray agree(const QByteArray &privBlob, const QByteArray &peerRaw64);
QString seal(const QByteArray &key, const QString &plain);
QString open(const QByteArray &key, const QString &wire);
QString openParts(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipherAndTag);
QString openRk1(const QByteArray &priv32, qint64 userId, const QString &sealedJson);

}
