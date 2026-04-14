{ pkgs, ... }: {
  channel = "stable-24.05";
  packages = [
    # Host build tools
    pkgs.file
    pkgs.sudo
    pkgs.gh
    pkgs.cmake
    pkgs.gnumake
    pkgs.gcc
    pkgs.python311
    pkgs.python311Packages.pyyaml
    pkgs.python311Packages.pytest
    pkgs.python311Packages.pydantic
    pkgs.git
    pkgs.curl

    # aarch64 cross-compiler (lighter — just the compiler, no full sysroot)
    pkgs.gcc-arm-embedded

    # QEMU user-mode only (much lighter than full system QEMU)
    pkgs.qemu
  ];

  idx = {
    extensions = [
      "ms-vscode.cmake-tools"
      "ms-vscode.cpptools"
    ];
    workspace = {
      onStart = {
        setup = "source ~/.bashrc";
      };
    };
  };
}
