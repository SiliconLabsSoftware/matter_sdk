/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 * @file  jwtcert.h
 * @brief PKCS#8 RSA private key (PEM) used to sign the MQTT RS256 JWT
 */

 #pragma once

 const unsigned char kJwtCertExample[] = 
 "-----BEGIN PRIVATE KEY-----\n"
"MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCntq6THjo105Bx\n"
"PRSTlyEtq6hvFLEZU3LVJofxAnHCPVhz37+zUquT+Iz0cNTgQSrlwBIGI3w0/giS\n"
"evataRSeHJ6WONp6Cb2TQyrSE8PaVH14BgByzKxS9NGujZPzTVSsf7uwKX9sMf/7\n"
"QN3hbrtEnnB2RMCJZqa17coa/5RT62l6qNm+jqsPSV7/sGZju36eeRTIZcVQp7r0\n"
"0wB0+R8oQtK2H1pmX1qVLWMbqKOdWfzCSFD52J35JkiPhWttMat+4VWRjWmeVinb\n"
"valsCESzHusjXFULvwJ5ezOwwI1m8N/qIdUH7zYFMDkmqqvFXrSusTUsDGrX567v\n"
"nDDS8w1FAgMBAAECggEAA6SsntYy7i++B90Le7f2aOHoEx8ASIw53Awt5dFncTsY\n"
"lyPO1pwUIsRtI3OUBu8YzyGLWAYgpkdic57SNFV/tNOVqlkVKJOE7RRcyMV/JViy\n"
"hQ9JpoobBY5QmRnK6KfPEBQ87928Po3I1sduSn23pGJLZSD8jGo7cuD12VJLVutR\n"
"g94WABg5VeIGVQYv+bt33TnpidIIvix11D8hh0P4AwW8sE3QEsLusJm2Y1P/W//Y\n"
"7SEWBc0atQnw0BHPaszwO3ST4E4c6il37oup6xuEHgAtyTSPILAEDQtdOsb32LJX\n"
"AV0UQc8l89dR6SwjZYwsjJVuhcmSXAPQrMWxLFFHXQKBgQDqlWIAmJoNWjB71MZA\n"
"94GRt+kIjM1FHaLrGVQCdm6TzJz2bDrq5QSSEERQGQ9mYwLGGGrFqOZua4c6mudH\n"
"kh9Yx2xnAGeIxFLM+4KZ9zVMmaHbqmvkXoKyVYtlNwFv+WanVPxmsN7K7a6EPOLL\n"
"ID9/P3dW7JPTt1e10+2bq7qpgwKBgQC3Bm9UtdQ7RKWGnOKnYp28la9g2t7ypUlD\n"
"DZzDLsNBwRwsHsQPf9rpcyfz1aKGp2SIi2oDomFmuz0FUozZmenrEi3I04/9sV2N\n"
"51D5f/Rf8Oqbin/QiPJV0uJkDuZDT0wOzGUYOkfI9VoxBL6awKa2tPBNwVeY8JRn\n"
"h78ECfLblwKBgQCwzRnp/RnOinUUP4+Uk3aSTxxRl112hBmwO4y3tm8s8gAzMetN\n"
"8oH5XE1AWULkFieXCfwfMWdLPbvUDb/Wj2kUzmkDUKi3yc/mMoGCbXE8ZGY7Wzyq\n"
"CBlVM2g2RrjMnhoib8kz1IZ2R6FKhWEhWxLAYyMc3n2kCgEPR8VDmLC85QKBgFer\n"
"1G6OfvA5DNUzl3q4yXhJd238yekPdc2R2rLAVrXLrBQSVLZb91/2UoABqM25p3Wk\n"
"2o6NHP0Z2bwP8/pUOPHqjlXxybqrWHYeBPLO8R94Btmk+V7KCtNDpkBpxrEdbgB0\n"
"tkRpvnMr3B04+ZilifZhK+8DG1PegigppT1pCpVXAoGAES2nNlEBkKaeDRiSoaLi\n"
"tJeSO6QpUjuYhDLYw7bhkmRPX79jcqKIAIJKgoN7RuBkxKpiR8Iken1a4+ZDM9OT\n"
"uA6CyLgviHLmWe88ghs9c1NC57V2fJnDofviZjurZMxOF2jrfSPLYDRnKLFsKcYg\n"
"uUlTUdkL3I3lH3+chjBY/2g=\n"
"-----END PRIVATE KEY-----\n";     