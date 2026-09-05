#!/bin/sh
#
# Regenerates the two test certificates which expire:
#
#   test-root-ca.pem       - self-signed test root CA (v3, CA:TRUE, keyCertSign)
#   test-server-cert.pem   - server certificate for *.*.mycompany.com / localhost, signed by the root
#
# The following inputs are KEPT and only created if missing:
#
#   test-root-ca-key.pem   - root CA key (PKCS#1)
#   test-server-key.pem    - server key (PKCS#8); it is embedded verbatim in
#                            src/utests/include/utests/baselib/UtfCrypto.h (getDefaultServerKey)
#   test-server-req.pem    - server CSR; carries the exact DN and SANs the tests depend on.
#                            DNS:*.*.mycompany.com MUST stay the first SAN (TestTlsPeerVerification
#                            asserts that a multi-label wildcard matches nothing)
#
# Validity is 10000 days (same as test-server-ip-cert.pem).
#
# After running this script the two PEM files must be re-embedded as the string literals
# getDevRootCA() and getDefaultServerCertificate() in UtfCrypto.h (one "<line>\n" C string per
# PEM line, 16-space indent, ';' after the END line) and checked with:
#
#   diff <(sed -n '/getDevRootCA/,/^        }/p' src/utests/include/utests/baselib/UtfCrypto.h \
#       | grep '^ *"' | sed -e 's/^ *"//' -e 's/\\n";*$//') certs/test-root-ca.pem
#
# (and the same for getDefaultServerCertificate vs certs/test-server-cert.pem); both must be empty.
#
# Note: 'openssl verify -x509_strict' is deliberately NOT used - it would require an
# authorityKeyIdentifier on the server certificate, which [ v3_req ] does not emit. The library
# itself only enforces SSL_CTX_set_security_level(2), which -auth_level 2 mirrors.
#

set -eu
cd "$(dirname "$0")"

DAYS=10000
ROOT_SUBJ="/C=US/ST=New York/L=NYC/O=My Company Ltd/CN=My Company Ltd Test Root Certificate"
SERVER_SUBJ="/C=US/ST=New York/L=NYC/O=My Company Ltd/CN=*.*.mycompany.com"

[ -f test-root-ca-key.pem ] || openssl genrsa -out test-root-ca-key.pem 2048
[ -f test-server-key.pem ]  || openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out test-server-key.pem

# root CA: self-signed, v3 with explicit CA extensions (OpenSSL 3.0 would otherwise emit v1)
openssl req -config openssl.conf -new -x509 -nodes -days "$DAYS" \
    -key test-root-ca-key.pem -subj "$ROOT_SUBJ" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash" \
    -out test-root-ca.pem

# server CSR: only if missing (the existing one carries the exact DN and SANs the tests depend on)
[ -f test-server-req.pem ] || openssl req -config openssl.conf -new -key test-server-key.pem \
    -subj "$SERVER_SUBJ" -out test-server-req.pem

# server cert: signed by the root, extensions from [ v3_req ]
openssl x509 -req -in test-server-req.pem -days "$DAYS" \
    -CA test-root-ca.pem -CAkey test-root-ca-key.pem -set_serial 01 \
    -extfile openssl.conf -extensions v3_req -out test-server-cert.pem

openssl verify -auth_level 2 -CAfile test-root-ca.pem test-server-cert.pem
openssl x509 -in test-server-cert.pem -noout -subject -dates -ext subjectAltName,keyUsage,basicConstraints
