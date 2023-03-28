# HTTP

In order to work with https, we would need a lib to provide ssl/tls/crypto implementations. Given that, the first lib that come into my mind is OpenSSL. 

OpenSSL have many issues. Ex.: in order to build it, we need to have Perl scripting...

So we need to look for options.

- LibreSSL provides same bindings and it is compatible with OpenSSL interfaces. So it could be a good option.
- BoringSSL
- mbedtls

https://en.wikipedia.org/wiki/Comparison_of_TLS_implementations

https://www.amongbytes.com/post/201804-comparing-mbedtls-to-boringssl/
