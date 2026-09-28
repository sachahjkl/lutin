{pkgs}: let
  curl = pkgs.fetchurl {
    url = "https://blocksds.skylyrac.net/packages/rolling/linux/x86_64/blocksds-libcurl-8.16.0-2-any.pkg.tar.xz";
    sha256 = "4c1362e532747f7a69167e7d14388f90d6019f9fbd1a5129d867943bebf16174";
  };
  tls = pkgs.fetchurl {
    url = "https://blocksds.skylyrac.net/packages/rolling/linux/x86_64/blocksds-mbedtls-3.6.4-2-any.pkg.tar.xz";
    sha256 = "a2fa12f1688abac050afb03e34664a0bf4788428058ee718b39325c69b19d752";
  };
  zlib = pkgs.fetchurl {
    url = "https://wonderful.asie.pl/packages/rolling/linux/x86_64/toolchain-gcc-arm-none-eabi-zlib-1.3.2-1-any.pkg.tar.xz";
    sha256 = "3c81569d924f2f455207320c0abec99261891d9bb5f105bce56c54afaa3f89f5";
  };
in
  pkgs.runCommand "blocksds-network-libraries" {} ''
    mkdir -p "$out"
    for archive in ${curl} ${tls} ${zlib}; do
      tar -xf "$archive" -C "$out"
    done
  ''
