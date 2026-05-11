/**
 * @file
 * @brief Matter abstraction layer for Direct Internet Connectivity.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc.
 *www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon
 *Laboratories Inc. Your use of this software is
 *governed by the terms of Silicon Labs Master
 *Software License Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 *This software is distributed to you in Source Code
 *format and is governed by the sections of the MSLA
 *applicable to Source Code.
 *
 ******************************************************************************/
#include <cstring>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/SilabsConfig.h>

#include "MatterAwsConfig.h"
#include "MatterAwsNvmCert.h"
#if 1
char ca_certificate[] = { "-----BEGIN CERTIFICATE-----\r\n"
                          "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\r\n"
                          "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\r\n"
                          "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\r\n"
                          "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\r\n"
                          "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\r\n"
                          "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\r\n"
                          "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\r\n"
                          "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\r\n"
                          "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\r\n"
                          "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\r\n"
                          "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\r\n"
                          "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\r\n"
                          "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\r\n"
                          "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\r\n"
                          "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\r\n"
                          "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\r\n"
                          "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\r\n"
                          "rqXRfboQnoZsG4q5WTP468SQvvG5\r\n"
                          "-----END CERTIFICATE-----\r\n" };
#endif
#if 0
char device_certificate[] = { "-----BEGIN CERTIFICATE-----\r\n"
                              "MIIDWTCCAkGgAwIBAgIUevvv4fhDbeTA7tGxEp6n9/JCqBkwDQYJKoZIhvcNAQEL\r\n"
                              "BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\r\n"
                              "SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDQyODA0Mjcx\r\n"
                              "NFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\r\n"
                              "ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALxJ/dbJ92r+9hecbJ0l\r\n"
                              "GpdKTdpU2eAUJvmaUHlQh1+fJL6R+62wtSkp/CSRxyijMJMD/hIrGba0WT0d3kGj\r\n"
                              "g7hcetFDh/jJJnPuUvgKU1OrVK5sd7pcKcfEuSYQgmI+Ga+1hDkJPTGao/oqEzKc\r\n"
                              "uZqNew8gVy6XReLIoXMbPYNtFszP1LXZ2WubaAYNfNB4ddTWHAwSNSxREqUsAFZW\r\n"
                              "0daUGCBBSmJkr8KWqNp1TKR1Qfe2A5qfbFhmzJAQ/xCQlaRLDAdtAvdPv7ejB/1P\r\n"
                              "356+tk3/yqY98rIz6UqSWVWJ6d6tRig1TbDFR+vE5ukdcF67y7kA7pxO0Vi7M+e0\r\n"
                              "89UCAwEAAaNgMF4wHwYDVR0jBBgwFoAUmnqjF7BXW1Xh2Ov06vakwS6SzPEwHQYD\r\n"
                              "VR0OBBYEFPE9BHw6BX+0LhrtIYAtPKsxqE+vMAwGA1UdEwEB/wQCMAAwDgYDVR0P\r\n"
                              "AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQAjxvsXVeqjSRedV5kYjb+9jjZc\r\n"
                              "rK28ec0vIHm/ZrRiKuBCATzPpUHh6Ru/uJ3oT5eNx4ScLdBUzwIEDXz+aWD1IaGW\r\n"
                              "8kP9LVElnGQ92LNddT34Nxyqk25HGA8lLASaAuZhOcTPBlxd9rSQMEE7Il/6HbdH\r\n"
                              "dmVrvN/3Dj0tfr5xrqJ4AfnsgQIhD19M2Fe5TlDuc+EdmTnljfEOCxgjqoP0u8A1\r\n"
                              "mAtEnD1ou4rmtUWFaXAwn9SaaDZHcagTWu3ba0xA4y2L1weqbhFb6K56HV9rWi/U\r\n"
                              "kkPxNzg46Ui5cpT1Q5cHNPkRyqIYCaPwzw9OTGXk2wZarliz9UuNtD+3p80S\r\n"
                              "-----END CERTIFICATE-----\r\n" };

char device_key[] = { "-----BEGIN RSA PRIVATE KEY-----\r\n"
                      "MIIEowIBAAKCAQEAvEn91sn3av72F5xsnSUal0pN2lTZ4BQm+ZpQeVCHX58kvpH7\r\n"
                      "rbC1KSn8JJHHKKMwkwP+EisZtrRZPR3eQaODuFx60UOH+Mkmc+5S+ApTU6tUrmx3\r\n"
                      "ulwpx8S5JhCCYj4Zr7WEOQk9MZqj+ioTMpy5mo17DyBXLpdF4sihcxs9g20WzM/U\r\n"
                      "tdnZa5toBg180Hh11NYcDBI1LFESpSwAVlbR1pQYIEFKYmSvwpao2nVMpHVB97YD\r\n"
                      "mp9sWGbMkBD/EJCVpEsMB20C90+/t6MH/U/fnr62Tf/Kpj3ysjPpSpJZVYnp3q1G\r\n"
                      "KDVNsMVH68Tm6R1wXrvLuQDunE7RWLsz57Tz1QIDAQABAoIBAQClnALrZ6r57hUw\r\n"
                      "AUK7GUaBKTa+wYD9CVyaj/MWMRFQtp5QDACs7c75zNbcp2ffw2FW/dz7x/MO8yPG\r\n"
                      "kL3LR/H0N0tDQj2XQf1TXJyXVCWkYv7Rh8/rF2McNViQNVco5+wZ0vLgY9LyDU0L\r\n"
                      "HPTTwGuzl5tUW6Ky9RTf+o2eu6foLonsy83Ivz0YTXlPzO6nuam4yyFBWJIa+uGM\r\n"
                      "MbpNpbNc1j+8v7PCzBuDEzDPhBHxkiTH+r3rPIswC7QS8qiEyeDeEul/KsC9F9bD\r\n"
                      "CK0Eytq/RZswqjdDd4Dnonk/+boH//lhspT590QbO58qGeGlmAHgeR2wjNFozV04\r\n"
                      "OVo+b4IhAoGBAN7Edc3GkQMqRuMCnDqkODUPppY8ozGbM2cLxIAtXiGUnjzcmBKG\r\n"
                      "Nxjmr/Y5Rsh9iWJizs6GAa+/XlOMB3+5jvtRVFSTFAMn46dVsdIYiqzUD49c4jtF\r\n"
                      "61vEz6thTkWd/DzZj3u0IDpvSRQcL20mDadFLrLs17hW1ddSERzYs5TJAoGBANhg\r\n"
                      "yyPbehuGiKNQZa/0Y5kUgBtVBOZojPnaAhgYBimwZk/tpvNyWv54P3w+rrPy9bRX\r\n"
                      "n0QVL9/m8uEhdfTj32c5gzoab5ZWq3mpYVptwMgSIs0veLgQFxg0Gf7c6SrFqbF4\r\n"
                      "sUX2l7ugK9ag2qf989uOFPxd68hQrbTCc1i9SiitAoGACgtbsHKSmUzvs06r0q0S\r\n"
                      "57mT7lQ/m33+/Fd4fbsN8n+r/tyHctZgZ5wHNRfyDBo09p2z63X5X35Gd4fp0qWA\r\n"
                      "P+6z2bhj/5xt1F61zN5UamSJaxJqFPgbG6EtJ/IUQGlWNTwuDwrZJSldZy26Kba8\r\n"
                      "iN1CtMifFW286J+HrABNYJkCgYBY1HvxD5R0ommL6mCkuMb+vFzPW7r0QG3H0QlV\r\n"
                      "DN/S05ldLOqCPStAFuuxPSaJt6/JRsOatrv9xplldVAwpjA79295NgsjhBdHjhLq\r\n"
                      "he5D9LYW2GtN3UUt6Y3WhKiwp48/zZWxbEfkfiAhL840c1xegVj6NjCq/vwhHkcu\r\n"
                      "Yh+EzQKBgFkWgIKJ7Np/oexI7Is1QYYiyr3PNoAIyVyy6ICKeMYvavL4cSicpYgd\r\n"
                      "6VHKU9zkVZTt6nLLOwgHeccMua7hXUgnvKdB3q6y90nLBLQD7sJvR3uaO4X5exxK\r\n"
                      "N7Q75gEcO/amO0bHdCIbBvYEJ7qjqbTaR+tnMkjK5r3xHfiQqdiT\r\n"
                      "-----END RSA PRIVATE KEY-----\r\n" };
#endif
#if 0
char ca_certificate[] = { "-----BEGIN CERTIFICATE-----\r\n"
                          "MIIBtjCCAVugAwIBAgITBmyf1XSXNmY/Owua2eiedgPySjAKBggqhkjOPQQDAjA5\r\n"
                          "MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6b24g\r\n"
                          "Um9vdCBDQSAzMB4XDTE1MDUyNjAwMDAwMFoXDTQwMDUyNjAwMDAwMFowOTELMAkG\r\n"
                          "A1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJvb3Qg\r\n"
                          "Q0EgMzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABCmXp8ZBf8ANm+gBG1bG8lKl\r\n"
                          "ui2yEujSLtf6ycXYqm0fc4E7O5hrOXwzpcVOho6AF2hiRVd9RFgdszflZwjrZt6j\r\n"
                          "QjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdDgQWBBSr\r\n"
                          "ttvXBp43rDCGB5Fwx5zEGbF4wDAKBggqhkjOPQQDAgNJADBGAiEA4IWSoxe3jfkr\r\n"
                          "BqWTrBqYaGFy+uGh0PsceGCmQ5nFuMQCIQCcAu/xlJyzlvnrxir4tiz+OpAUFteM\r\n"
                          "YyRIHN8wfdVoOw==\r\n"
                          "-----END CERTIFICATE-----\r\n" };
#endif
#if 1
char device_certificate[] = { "-----BEGIN CERTIFICATE-----\r\n"
                              "MIICyDCCAbCgAwIBAgIUcM+IxxZt+CRSCn2CP3ggfcX7nuswDQYJKoZIhvcNAQEL\r\n"
                              "BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\r\n"
                              "SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MDgxNTE1MTcz\r\n"
                              "MVoXDTQ5MTIzMTIzNTk1OVowWDELMAkGA1UEBhMCSU4xCzAJBgNVBAgMAlROMQww\r\n"
                              "CgYDVQQHDANIWUQxDzANBgNVBAoMBlNJTEFCUzEdMBsGA1UEAwwUU0xfTUFUVEVS\r\n"
                              "X0FXU19DTElFTlQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATT9MpH+BEc9eIZ\r\n"
                              "PRvtxyKHAlrBQxANlCned1JYuVIN68ksiWKZ57l54u2nb0LFGpDOzCfAwNXVy9SQ\r\n"
                              "XDplrc8Vo2AwXjAfBgNVHSMEGDAWgBRH/H+jNPEGRHFg0/FyXsTcNiyXeTAdBgNV\r\n"
                              "HQ4EFgQUORvyPJgb6v/naJsIQaqa7onsHmAwDAYDVR0TAQH/BAIwADAOBgNVHQ8B\r\n"
                              "Af8EBAMCB4AwDQYJKoZIhvcNAQELBQADggEBAHT1qetZtxgKkg2XklTWtx9tp8Po\r\n"
                              "dleMVP+j3utwDJYXeHOEQNPRqvVY3e/FPTA9pUb3ODVoCjwdLjlNq437GCosjO6m\r\n"
                              "G7clljhZhm5HmsjGGCCfKfLFJKP2KxrJhJTbIdLdAFFWqx+Fkpo/QJVBiAx5lJJ9\r\n"
                              "iA0etd1R4WBy8+23Wm9XrQv7vuVfswEhVx/eRSAQiAK0w56duNkK85lvw1+NXrTE\r\n"
                              "gZMKyWQavgaYJbnXydENubrXDaoOgtNZ5/6GH+uVSzWe0JTi6caQmiy47MUtQFBZ\r\n"
                              "CP1KcTn0Ycn9MvDC/XEPQ4tkPPV8kPIKsZlhL/o41KxMGi9VjBen3UHHM2o=\r\n"
                              "-----END CERTIFICATE-----\r\n" };

char device_key[] = { "-----BEGIN EC PRIVATE KEY-----\r\n"
                      "MHcCAQEEILhJmsve8o9IIUV589o28Gp92a7id1+s+C5ce49DsJt5oAoGCCqGSM49\r\n"
                      "AwEHoUQDQgAE0/TKR/gRHPXiGT0b7ccihwJawUMQDZQp3ndSWLlSDevJLIlimee5\r\n"
                      "eeLtp29CxRqQzswnwMDV1cvUkFw6Za3PFQ==\r\n"
                      "-----END EC PRIVATE KEY-----\r\n" };
#endif
using namespace chip::DeviceLayer::Internal;

CHIP_ERROR MatterAwsGetCACertificate(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_MATTER_AWS_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_CACerts, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = sizeof(ca_certificate);
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, ca_certificate, *bufSize);
#endif
    return status;
}

CHIP_ERROR MatterAwsGetDeviceCertificate(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_MATTER_AWS_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_DeviceCerts, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = sizeof(device_certificate);
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, device_certificate, *bufSize);
#endif
    return status;
}

CHIP_ERROR MatterAwsGetDevicePrivKey(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_MATTER_AWS_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_DeviceKey, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = sizeof(device_key);
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, device_key, *bufSize);
#endif
    return status;
}

CHIP_ERROR MatterAwsGetHostname(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_MATTER_AWS_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_hostname, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = sizeof(MATTER_AWS_SERVER_HOST);
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, MATTER_AWS_SERVER_HOST, *bufSize);
#endif
    return status;
}

CHIP_ERROR MatterAwsGetClientId(char * buf, size_t buf_len, size_t * bufSize)
{
    CHIP_ERROR status = CHIP_NO_ERROR;
#if SL_MATTER_AWS_NVM_EMBED_CERT
    status =
        SilabsConfig::ReadConfigValueBin(SilabsConfig::kConfigKey_clientid, reinterpret_cast<uint8_t *>(buf), buf_len, *bufSize);
#else
    *bufSize = sizeof(MATTER_AWS_CLIENT_ID);
    if (buf == NULL || buf_len < *bufSize)
    {
        return CHIP_ERROR_INTERNAL;
    }
    strncpy(buf, MATTER_AWS_CLIENT_ID, *bufSize);
#endif
    return status;
}
