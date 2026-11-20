# BearSSL scalar ChaCha20/Poly1305 policy for the bare-metal MayKernel

During the isolated TLS 1.2 memory handshake CI a real x86_64 QEMU guest trapped at br_chacha20_sse2_run (movdqu xmm3...), raising #UD. The CPU reported SSE2 support, but MayKernel has not configured the kernel FPU/XMM control and context save/restore. CPU feature availability is not sufficient for legal kernel SIMD use.

To avoid the exact same bug in any future BearSSL client, the native provider initialization now selects a single TLS1.2 ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 suite and the portable constant-time br_chacha20_ct_run and br_poly1305_ctmul_run functions, ignoring BearSSL's CPU-based SIMD selection. This is a conservative performance tradeoff until a complete FPU/XMM lifecycle has been implemented and reviewed.

MayShell TLS status explicitly reports whether the scalar policy is selected. The existing BearSSL-native QEMU smoke test also verifies that the kernel still blocks HTTPS: no trusted certificate time, production CSPRNG, certificate roots or authenticated handshake exists in the default provider. There is no network request or system install.

This hardening does not enable a TLS connection. Separate safe initialization of each future session, trusted seed and time, full X509 trust, socket transport and signed update verification are required. Performance optimizations may only be re-enabled after proper CPU FPU/XMM configuration and task context management.