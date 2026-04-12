{ pkgs, ... }: {
  channel = "stable-24.05";

  packages = [
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
