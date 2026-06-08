# Known issues
host ubuntu-24.04 / clang++ 错误日志如下：
0s
Run ctest --test-dir build --output-on-failure -j
Internal ctest changing into directory: /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build
Test project /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build
    Start 1: cidr
    Start 2: lpm
    Start 3: frame
    Start 4: registration
1/5 Test #1: cidr .............................***Failed    0.06 sec
==7953==Your application is linked against incompatible ASan runtimes.

2/5 Test #2: lpm ..............................***Failed    0.07 sec
==7954==Your application is linked against incompatible ASan runtimes.

3/5 Test #3: frame ............................***Failed    0.06 sec
==7955==Your application is linked against incompatible ASan runtimes.

    Start 5: netlink
4/5 Test #4: registration .....................***Failed    0.06 sec
==7956==Your application is linked against incompatible ASan runtimes.

5/5 Test #5: netlink ..........................***Failed    0.00 sec
==7957==Your application is linked against incompatible ASan runtimes.


0% tests passed, 5 tests failed out of 5

Total Test time (real) =   0.12 sec

The following tests FAILED:
	  1 - cidr (Failed)
Errors while running CTest
	  2 - lpm (Failed)
	  3 - frame (Failed)
	  4 - registration (Failed)
	  5 - netlink (Failed)
Error: Process completed with exit code 8.

cross mipsel 错误日志如下：
-- QUIC_ENABLE_LOGGING is false. Disabling logging
-- libatomic not found. If build fails, install libatomic
-- libnuma not found. If build fails, install libnuma
-- Enabling shared ephemeral port work around
-- Configuring for OpenSSL 1.1
-- Setting openssldir to /usr/lib/ssl
-- Configuring OpenSSL: /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-mipsel/_deps/msquic-src/submodules/openssl/config;CC=/usr/bin/mipsel-linux-gnu-gcc;CXX=/usr/bin/mipsel-linux-gnu-g++ enable-tls1_3;no-makedepend;no-dgram;no-ssl3;no-psk;no-srp;no-zlib;no-egd;no-idea;no-rc5;no-rc4;no-afalgeng;no-comp;no-cms;no-ct;no-srp;no-srtp;no-ts;no-gost;no-dso;no-ec2m;no-tls1;no-tls1_1;no-tls1_2;no-dtls;no-dtls1;no-dtls1_2;no-ssl;no-ssl3-method;no-tls1-method;no-tls1_1-method;no-tls1_2-method;no-dtls1-method;no-dtls1_2-method;no-siphash;no-whirlpool;no-aria;no-bf;no-blake2;no-sm2;no-sm3;no-sm4;no-camellia;no-cast;no-md4;no-mdc2;no-ocb;no-rc2;no-rmd160;no-scrypt;no-seed;no-weak-ssl-ciphers;no-shared;no-tests;--openssldir="/usr/lib/ssl";--prefix=/home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-mipsel/_deps/opensslquic-build/openssl
-- Configuring for OpenSSL
-- msquic (FetchContent): v2.4.5
-- Configuring incomplete, errors occurred!
Error: Process completed with exit code 1.

cross arm 错误日志如下：
-- libatomic not found. If build fails, install libatomic
-- libnuma not found. If build fails, install libnuma
-- Enabling shared ephemeral port work around
-- Configuring for OpenSSL 1.1
-- Setting openssldir to /usr/lib/ssl
-- Configuring OpenSSL: /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-arm/_deps/msquic-src/submodules/openssl/Configure;linux-armv4;-DL_ENDIAN;--cross-compile-prefix=- enable-tls1_3;no-makedepend;no-dgram;no-ssl3;no-psk;no-srp;no-zlib;no-egd;no-idea;no-rc5;no-rc4;no-afalgeng;no-comp;no-cms;no-ct;no-srp;no-srtp;no-ts;no-gost;no-dso;no-ec2m;no-tls1;no-tls1_1;no-tls1_2;no-dtls;no-dtls1;no-dtls1_2;no-ssl;no-ssl3-method;no-tls1-method;no-tls1_1-method;no-tls1_2-method;no-dtls1-method;no-dtls1_2-method;no-siphash;no-whirlpool;no-aria;no-bf;no-blake2;no-sm2;no-sm3;no-sm4;no-camellia;no-cast;no-md4;no-mdc2;no-ocb;no-rc2;no-rmd160;no-scrypt;no-seed;no-weak-ssl-ciphers;no-shared;no-tests;--openssldir="/usr/lib/ssl";--prefix=/home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-arm/_deps/opensslquic-build/openssl;-latomic
-- Configuring for OpenSSL
-- msquic (FetchContent): v2.4.5
-- Configuring incomplete, errors occurred!
Error: Process completed with exit code 1.

cross arm-eabi5 错误日志如下：
-- libatomic not found. If build fails, install libatomic
-- libnuma not found. If build fails, install libnuma
-- Enabling shared ephemeral port work around
-- Configuring for OpenSSL 1.1
-- Setting openssldir to /usr/lib/ssl
-- Configuring OpenSSL: /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-arm-eabi5/_deps/msquic-src/submodules/openssl/Configure;linux-armv4;-DL_ENDIAN;--cross-compile-prefix=- enable-tls1_3;no-makedepend;no-dgram;no-ssl3;no-psk;no-srp;no-zlib;no-egd;no-idea;no-rc5;no-rc4;no-afalgeng;no-comp;no-cms;no-ct;no-srp;no-srtp;no-ts;no-gost;no-dso;no-ec2m;no-tls1;no-tls1_1;no-tls1_2;no-dtls;no-dtls1;no-dtls1_2;no-ssl;no-ssl3-method;no-tls1-method;no-tls1_1-method;no-tls1_2-method;no-dtls1-method;no-dtls1_2-method;no-siphash;no-whirlpool;no-aria;no-bf;no-blake2;no-sm2;no-sm3;no-sm4;no-camellia;no-cast;no-md4;no-mdc2;no-ocb;no-rc2;no-rmd160;no-scrypt;no-seed;no-weak-ssl-ciphers;no-shared;no-tests;--openssldir="/usr/lib/ssl";--prefix=/home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-arm-eabi5/_deps/opensslquic-build/openssl;-latomic
-- Configuring for OpenSSL
-- msquic (FetchContent): v2.4.5
-- Configuring incomplete, errors occurred!
Error: Process completed with exit code 1.

cross linux-amd64 错误日志如下：
-- Found libatomic: /usr/lib/x86_64-linux-gnu/libatomic.so.1
-- Found libnuma: /usr/lib/x86_64-linux-gnu/libnuma.so.1
-- numa.h not found. If build fails, install libnuma-dev
-- Enabling shared ephemeral port work around
-- Configuring for OpenSSL 1.1
-- Setting openssldir to /usr/lib/ssl
-- Configuring OpenSSL: /home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-linux-amd64/_deps/msquic-src/submodules/openssl/config;CC=/usr/bin/cc;CXX=/usr/bin/c++ enable-tls1_3;no-makedepend;no-dgram;no-ssl3;no-psk;no-srp;no-zlib;no-egd;no-idea;no-rc5;no-rc4;no-afalgeng;no-comp;no-cms;no-ct;no-srp;no-srtp;no-ts;no-gost;no-dso;no-ec2m;no-tls1;no-tls1_1;no-tls1_2;no-dtls;no-dtls1;no-dtls1_2;no-ssl;no-ssl3-method;no-tls1-method;no-tls1_1-method;no-tls1_2-method;no-dtls1-method;no-dtls1_2-method;no-siphash;no-whirlpool;no-aria;no-bf;no-blake2;no-sm2;no-sm3;no-sm4;no-camellia;no-cast;no-md4;no-mdc2;no-ocb;no-rc2;no-rmd160;no-scrypt;no-seed;no-weak-ssl-ciphers;no-shared;no-tests;--openssldir="/usr/lib/ssl";--prefix=/home/runner/work/Cloudflared-Zero-Trust/Cloudflared-Zero-Trust/build-linux-amd64/_deps/opensslquic-build/openssl
-- Configuring for OpenSSL
-- msquic (FetchContent): v2.4.5
-- Configuring incomplete, errors occurred!

