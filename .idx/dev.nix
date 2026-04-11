{ pkgs, ... }: {
  channel = "stable-24.05";

  packages = [
    pkgs.cmake
    pkgs.gnumake
    pkgs.gcc
    pkgs.python311
    pkgs.python311Packages.pyyaml
    pkgs.git
    pkgs.curl
  ];

  idx = {
    extensions = [
      "ms-vscode.cmake-tools"
      "ms-vscode.cpptools"
    ];

    workspace = {
      onStart = {
        setup = "source scripts/setup-workspace.sh";
      };
    };
  };
}
