/*
 * This file is part of the swblocks-baselib library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __TEST_UTFCRYPTO_H_
#define __TEST_UTFCRYPTO_H_

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/OS.h>
#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

namespace test
{
    template
    <
        typename E = void
    >
    class UtfCryptoT
    {
        BL_DECLARE_STATIC( UtfCryptoT )

    public:

        static auto getDevRootCA() -> const char*
        {
            /*
             * This is from following file: "certs/test-root-ca.pem"
             */

            return
                "-----BEGIN CERTIFICATE-----\n"
                "MIIDvjCCAqagAwIBAgIUWXcRaor9MG9nfIMDD1gPLzZZ/XcwDQYJKoZIhvcNAQEL\n"
                "BQAwdjELMAkGA1UEBhMCVVMxETAPBgNVBAgMCE5ldyBZb3JrMQwwCgYDVQQHDANO\n"
                "WUMxFzAVBgNVBAoMDk15IENvbXBhbnkgTHRkMS0wKwYDVQQDDCRNeSBDb21wYW55\n"
                "IEx0ZCBUZXN0IFJvb3QgQ2VydGlmaWNhdGUwIBcNMjYwOTA0MTg0MDU4WhgPMjA1\n"
                "NDAxMjAxODQwNThaMHYxCzAJBgNVBAYTAlVTMREwDwYDVQQIDAhOZXcgWW9yazEM\n"
                "MAoGA1UEBwwDTllDMRcwFQYDVQQKDA5NeSBDb21wYW55IEx0ZDEtMCsGA1UEAwwk\n"
                "TXkgQ29tcGFueSBMdGQgVGVzdCBSb290IENlcnRpZmljYXRlMIIBIjANBgkqhkiG\n"
                "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAslipXTEu/Tf2YsVx50D1bZRbksk+6jl0LVyJ\n"
                "tjuhlcJS+SwWXEH03prTXoYu+ocEzVvhkA+gmwpt0tqwewdbip42JCT7mrALn82V\n"
                "ngTLuwX99jKsBvkzM0NlwK6F++d6yb561Aq84jTumL33tCh83TdOPI5x3giAx8fV\n"
                "Nh2Mt8dhj9DgLXnwyMQTeluGABruWna9gUPlYOeZzBrqaio+nSFFTw7shE6lPltT\n"
                "Cb/0LRNPhGWhBUqy0JioRsePVAvbOKffW8veV5WnUTFdP8yBh0dWt4rA3bQIjJK4\n"
                "M6SEx2FJ2xc7D2yVED3uzV5GBF3Fm/8syl6lctpGOmSZtAUxbQIDAQABo0IwQDAP\n"
                "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUW9GmcDax\n"
                "aG4VJTmrnJ96nEyNzbQwDQYJKoZIhvcNAQELBQADggEBAFnuJ1aDCopdkckZ0RDJ\n"
                "+uL7Pee8o+u9ayrLVV1aWkwgNeLbNeGMbcII5OxO3RNpgJzQxxF/UjJCr1p+zCPm\n"
                "x7QGvCuglQFUh7k4NOQ7HuU6kjB9aXKR64OpPLoYmdrQ5xExD9pnmLomBAOGaKTv\n"
                "YblZR5Ii/3o/J/fYRjCFCRT6MaMQAaFViJt9w5qQi/tqJ4hpvJPt5JlXeu0dCvMz\n"
                "stYkxhG7Q2SlVpR2ssoVMI7iBjPFJTxUIiB0on59AvbGbe/DLHkHQkqwHFoeSubl\n"
                "U4ihavTKD9xNfSbuJ0SHN8Ek0NlJppA44lZot84mXJwUYdW1B5koiXFpYF9XH4Oa\n"
                "k+E=\n"
                "-----END CERTIFICATE-----\n";
        }

        static auto getDefaultServerKey() -> const char*
        {
            /*
             * This is from following file: "certs/test-server-key.pem"
             */

            return
                "-----BEGIN PRIVATE KEY-----\n"
                "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDF04YyIhEtTecw\n"
                "51Xy9Z9kyVsdS7Tr19fTsUVCaFJiDL7KfR9loVSQzlvT/M1xZhePkZISZzQ0Ixi+\n"
                "CTLwF+SzSSCD81KjDHqeRcuRetJgz4afnAq9QsgtET7cIeVyBk6sz+CheBJmOyii\n"
                "TDfgZBaPjN1tVqTN4Hz+whavXeXw0kTqrEPuR1ba0kbCjyU3jjFg75iu1nfwEo+K\n"
                "kizgBSbBdWreiZJQf25q5c9yrPqBe/WSx2/QZfYs24Nehm2jCAUZyk0bh4cr4aRJ\n"
                "QoFUzpAo15Vl8kD2msUJnS5Y5a9wc/8u1Q3OJotvJ2AN45g0xGyqSwCjIiXnBFeT\n"
                "/estF/VPAgMBAAECggEAdoXp0+WHRw5yomEnpJ42tmrRVTcDmX3DSIjgBw57tVUP\n"
                "hj/67KgBA5UvfU3sRLG3EgRUcQQ2SbpxW4Ila6XVFvmMKqJA84FJgcQtV+cvXmNX\n"
                "tA8IfCYjyqSXdco1LuDKiE0vt246D9gH210w6RbuUWlDTPvpV5PVL8lXUBBA8Mvr\n"
                "CJus47wq58U0WK2AF6CABcGFnSY9JiJavdZpAlgaO54sIq5YdZORJ8pbk5IjXybR\n"
                "TGTL3IBpuB8RIo4zO9dDvuE1zxBj2FgSTNoo3IsGoIrpInaT2SWGtv04sBrMIbFl\n"
                "B/mkkEzONf2OOmZ0NLPfnZdj4tejyxGq4woYgbLUkQKBgQD45bEHBIHBtKHuIv0d\n"
                "1Y20OVfjQJOTc2bQUNj7f8WeHrINYBYes3jRuf9LDAFDUYZyesOMGjEUcAnpWuha\n"
                "4mHJJqj5r1R9dIpaW8L1ABcV931t1j7aEnq1E/VdRTM+iUR+rUHRzYid+p3CgqLx\n"
                "SMEnDomeDjRGCw8LKcAuVSwaJwKBgQDLeLxglju0Rm21tRE0oSEYwStkkVhmP4gL\n"
                "nDeRMvephgPDTTXWrP4eV2mvynH0/+t+pwbDWHM1q4XrCBnVX/bGpqqOgoPuAj2x\n"
                "wY1BMTDoUlG9VO7koJWM0SN3gyZeOicykpMKszYfWkOoETQ1U49LrOIFJRx192xR\n"
                "WIvpvXmMmQKBgQC6ZYnF76IdJuF+LcXRafTNW4RuNBZQ/sOojmNxNacRW3uMeMEY\n"
                "DOAWcGy4Dy2C9LLzWOzJJ3RKEf3aPLJ2HcONmN5C3wMvUO+r67x9LqwbT1UnxKMd\n"
                "PWmX4nKGfyR5WONq2uXH8Vy2stEisiLE/+9nCIQXUhvjuLRzb7j0+eQlUQKBgDCF\n"
                "6X6rNS/Hv/Aebyz65BawMnX4R3mS2xHRvlqtKezOneUca6N3e96mf/jBMa34viNl\n"
                "F7LMTCVXc0daljaRfRtgsbnsnCPNewMCInqSjZRJ1V5ue84gEaoUUf31U9gSzDg+\n"
                "Rjy+AkE12H6jI6038Std3kTV1dS4HafEkxE581u5AoGAIgoxxG5L1tOq7FOycPgJ\n"
                "EeN2e81PgAVesy0fsemlXxoobXpnftT3uPn5Zpo/yVwUXbmCzRahblgljAMK19TY\n"
                "HNieCuaJCnml/NQz4Atzq+AhOczo3eK1v6wgkWBDDSqu7CahEu0ndarDI6Xfa3Pc\n"
                "RIGnam24+CSu1F4r2xiy2XA=\n"
                "-----END PRIVATE KEY-----\n";
        }

        static auto getDefaultServerCertificate() -> const char*
        {
            /*
             * This is from following file: "certs/test-server-cert.pem"
             */

            return
                "-----BEGIN CERTIFICATE-----\n"
                "MIID2zCCAsOgAwIBAgIBATANBgkqhkiG9w0BAQsFADB2MQswCQYDVQQGEwJVUzER\n"
                "MA8GA1UECAwITmV3IFlvcmsxDDAKBgNVBAcMA05ZQzEXMBUGA1UECgwOTXkgQ29t\n"
                "cGFueSBMdGQxLTArBgNVBAMMJE15IENvbXBhbnkgTHRkIFRlc3QgUm9vdCBDZXJ0\n"
                "aWZpY2F0ZTAgFw0yNjA5MDQxODQwNThaGA8yMDU0MDEyMDE4NDA1OFowYzELMAkG\n"
                "A1UEBhMCVVMxETAPBgNVBAgMCE5ldyBZb3JrMQwwCgYDVQQHDANOWUMxFzAVBgNV\n"
                "BAoMDk15IENvbXBhbnkgTHRkMRowGAYDVQQDDBEqLioubXljb21wYW55LmNvbTCC\n"
                "ASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMXThjIiES1N5zDnVfL1n2TJ\n"
                "Wx1LtOvX19OxRUJoUmIMvsp9H2WhVJDOW9P8zXFmF4+RkhJnNDQjGL4JMvAX5LNJ\n"
                "IIPzUqMMep5Fy5F60mDPhp+cCr1CyC0RPtwh5XIGTqzP4KF4EmY7KKJMN+BkFo+M\n"
                "3W1WpM3gfP7CFq9d5fDSROqsQ+5HVtrSRsKPJTeOMWDvmK7Wd/ASj4qSLOAFJsF1\n"
                "at6JklB/bmrlz3Ks+oF79ZLHb9Bl9izbg16GbaMIBRnKTRuHhyvhpElCgVTOkCjX\n"
                "lWXyQPaaxQmdLljlr3Bz/y7VDc4mi28nYA3jmDTEbKpLAKMiJecEV5P96y0X9U8C\n"
                "AwEAAaOBhDCBgTAJBgNVHRMEAjAAMAsGA1UdDwQEAwIF4DAnBgNVHREEIDAeghEq\n"
                "LioubXljb21wYW55LmNvbYIJbG9jYWxob3N0MB0GA1UdDgQWBBSDhf4X1OVVG2+6\n"
                "VNF93hZ1hzg/lTAfBgNVHSMEGDAWgBRb0aZwNrFobhUlOaucn3qcTI3NtDANBgkq\n"
                "hkiG9w0BAQsFAAOCAQEAE4P/+5q3i+b3mD3kbXO5mc2ftyl1tiJcNoia3+KvITFD\n"
                "qPQeoPh1kWn3lvthBgUbJmLvWQunfih5e3cQbCRUy7Gy0k0UWo2J/B2FKk1gwR5X\n"
                "GNRf/QCnpt+WAN1P0J9OUzijK8iLIp4tiymok8a2EEiNEWMhWRZVqjS5S4w0A5HS\n"
                "Yyp0594JlLnhDHrbqevbG2LZ8kadPlKtLfWwgiFX9KgAGqL/hKFoj2G9r72jV/rt\n"
                "02JQYU/bPV1EBFzdf8cljxU8NLMr4CIaBcr9cN3ohyZTWd7+F/wnjKI5ciN2TA+Y\n"
                "dUnBKzg4ai3JlLIBxUK8CF1jXh3yidz3WWQ4uVsQNQ==\n"
                "-----END CERTIFICATE-----\n";
        }

        static auto getIpAddressServerCertificate() -> const char*
        {
            /*
             * This is from following file: "certs/test-server-ip-cert.pem"
             *
             * A self-signed certificate used only by the TlsPeerVerification tests. Its SAN set
             * is chosen so one certificate covers every branch of the peer-name match:
             *
             *   IP:127.0.0.1     - an address literal which matches
             *   IP:::1           - the same for IPv6
             *   DNS:10.11.12.13  - a DNS entry which LOOKS like an address, so that asking for
             *                      10.11.12.13 exercises the RFC 6125 rule that a literal must
             *                      not be matched against DNS names; ::X509_check_host() returns
             *                      1 for it and the match must still be refused
             *   DNS:example.test - an ordinary DNS name which matches
             *
             * See certs/ip-openssl.conf for how it was generated.
             */

            return
                "-----BEGIN CERTIFICATE-----\n"
                "MIIDwzCCAqugAwIBAgIUUGav9VKl88PqHSRyWLErX2NYdPwwDQYJKoZIhvcNAQEL\n"
                "BQAwXjELMAkGA1UEBhMCVVMxETAPBgNVBAgMCE5ldyBZb3JrMQwwCgYDVQQHDANO\n"
                "WUMxFzAVBgNVBAoMDk15IENvbXBhbnkgTHRkMRUwEwYDVQQDDAxleGFtcGxlLnRl\n"
                "c3QwIBcNMjYwOTAzMjEzNzQ1WhgPMjA1NDAxMTkyMTM3NDVaMF4xCzAJBgNVBAYT\n"
                "AlVTMREwDwYDVQQIDAhOZXcgWW9yazEMMAoGA1UEBwwDTllDMRcwFQYDVQQKDA5N\n"
                "eSBDb21wYW55IEx0ZDEVMBMGA1UEAwwMZXhhbXBsZS50ZXN0MIIBIjANBgkqhkiG\n"
                "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAss/hjDYnHd4M4eUUvPKMcru/k/JoPvLA6EHX\n"
                "pYMIURdhbIsKGSjI0yLx6/abYRJWrrPiC1WcPps41RZ5wyHhOTNLU+nMmjuKQUuY\n"
                "cCoVNPk8ltdYQuvR3SqtYfLyBZ9/lCZTmFTBsX4L5dbAKA4BKpwOZH6zEF1EqoIW\n"
                "QIaO1nLgYGYzUpvHcs6n0HFzfcrpyPQmYrzCXyExr6Ur7aQS9BM9t8rYk0FJSdVz\n"
                "zTWLPHT9he1AZpSr1l5oDJtS6x+0j90oPJmPdJiwR3y5FnnsQTdg3rqdfeKps2+7\n"
                "WI3Mfvy3O7ot4y68nhBt3AFkUCiNhbewAwmSyTG8iSGLWa7jAQIDAQABo3cwdTAJ\n"
                "BgNVHRMEAjAAMAsGA1UdDwQEAwIF4DA8BgNVHREENTAzggxleGFtcGxlLnRlc3SC\n"
                "CzEwLjExLjEyLjEzhwR/AAABhxAAAAAAAAAAAAAAAAAAAAABMB0GA1UdDgQWBBQE\n"
                "cDdvpxq64qvuCtoOR7p5xYA5TDANBgkqhkiG9w0BAQsFAAOCAQEANC2kH5xA00MB\n"
                "aKo2kQRrIP38/xoYg12HlmLFk17bfnakFGJwTliM6sRmimcFQ+6d0XEkHks6GIgy\n"
                "YTNqXMQTxAd92hzf914VablTNp+R+m/o5JCfzZro6PTj4mGuohCPQA+4YrGZxUvX\n"
                "aO094WVlUr/pZb3Jwh2HMzBAZGmCP18TEG19ovvG1pLcl3B/+oHAoeiyCbhusvZ4\n"
                "LyjUAa+tzoheBB2cR5cV6exIdIEAfLyxNPpkqfVMieq1hqwXX/VlEr0Te9efYr2z\n"
                "dRSJbSYWHUkyJDXZi0Vc/iDHncVkero+u32OgBWHoHN+gJVWQ+eGbBy5Ze7rwXsj\n"
                "CrymOjILEw==\n"
                "-----END CERTIFICATE-----\n";
        }
    };

    typedef UtfCryptoT<> UtfCrypto;

} // test

#endif /* __TEST_UTFCRYPTO_H_ */
