class MulleClangProject < Formula
  desc "Objective-C compiler for the mulle-kybernetik runtime"
  homepage "https://github.com/mulle-cc/mulle-clang-project"
  license "BSD-3-Clause"
  version "22.1.8.7"
#  revision 1
  head "https://github.com/mulle-cc/mulle-clang-project.git", branch: "mulle/17.0.6"

#
# PREFERRED WORKFLOW:
#    The whole version-bump + bottle build + publish process is now scripted as a
#    replayable mulle-release-commander (MRC) bundle:
#
#        mulle-clang-project-macos.mrc   (sibling of the homebrew-software repo)
#
#    Point an AI agent at it (see its AGENTS.md / AI-EXECUTION.md) and it runs the
#    ordered tasks: verify tap symlink, set version+source sha256, --build-bottle,
#    brew bottle + rename, insert the bottle sha256 block, upload to the github
#    release, and verify install from the bottle. Bottles are hosted on github
#    releases (matching the `bottle do root_url` below).
#
#    The MEMO steps below are kept as the MANUAL FALLBACK / reference for what the
#    bundle automates. Prefer the MRC bundle.
#
# MEMO (manual fallback):
#    0. Replace 22.1.8.7 with x.0.0.0 your version number (and check vendor)
#    1. Create a release on github
#    2. Download the tar.gz file from github like so
#       `curl -O -L "https://github.com/mulle-cc/mulle-clang-project/archive/22.1.8.7.tar.gz"`
#    3. Run shasum over it `shasum -a 256 -b 22.1.8.7.tar.gz`
#    4. Remove bottle urls
#
  url "https://github.com/mulle-cc/mulle-clang-project/archive/refs/tags/22.1.8.7.tar.gz"
  sha256 "a6c3ea6ee6ab44f4b5ff0e0cd7d6f3ac3245f2da1b56cc72660a663edad6b05f"

  def vendor
    "mulle-clang 22.1.8.7 (runtime-load-version: 19)"
  end

#
# MEMO (manual fallback):
#    For each OS X version, create bottles with:
#    (This is the manual equivalent of what mulle-clang-project-macos.mrc automates.)
#
#    `brew uninstall mulle-kybernetik/software/mulle-clang-project`
#    `brew install --formula --build-bottle mulle-clang-project.rb`
# Now it gets retarded:
#    `brew tap-new mulle-kybernetik/software`
#    `cp mulle-clang-project.rb /usr/local/Homebrew/Library/Taps/mulle-kybernetik/homebrew-software/Formula/`
#    `brew bottle mulle-kybernetik/software/mulle-clang-project`
#    `mv ./mulle-clang--22.1.8.7.sequoia.bottle.tar.gz  ./mulle-clang-project-22.1.8.7.sequoia.bottle.tar.gz`
#
#     scp -i ~/.ssh/id_rsa_hetzner_pw \
#            ./mulle-clang-22.1.8.7.sequoia.bottle.tar.gz \
#            codeon@www262.your-server.de:public_html/_site/bottles/
#
  bottle do
    root_url "https://github.com/mulle-cc/mulle-clang-project/releases/download/22.1.8.7/"
    sha256 cellar: :any, sequoia: "c421aee60a17ce1a83f109161c265d1ad99d6c04185a132b80ceb5be26b425ed"
  end

#
# MEMO:
#    Change llvm to proper version
#
  # depends_on 'llvm@9'  => :build
  depends_on 'cmake'   => :build
  depends_on 'ninja'   => :build 
  #
  # homebrew llvm is built with polly, but cmake doesn't pick it up
  # for some reason
  # DOESN'T WORK ANYMORE, presumably because LLVM builds cmake itself
  #
  # def install
  #   if "#{vendor}".empty?
  #     raise "vendor is empty"
  #   end

  #  compiler_rt doesn't build on macos
  def install
    mkdir "build" do
      args = std_cmake_args
      args << '-DLLVM_BUILD_LLVM_DYLIB=ON'
      args << "-DLLVM_ENABLE_PROJECTS='clang'" # ";compiler-rt'" don't build for now on sequoia
      args << "-DLLVM_ENABLE_RUNTIMES='libcxxabi;libcxx;libunwind'"
      args << '-DLLVM_LINK_LLVM_DYLIB=ON'
      args << '-DLLVM_PARALLEL_LINK_JOBS=4'
      args << '-DCMAKE_BUILD_TYPE=Release'
      args << '-DCLANG_VENDOR=mulle' 
      #
      # `--reduce-memory-overheads` is a GNU ld (binutils) option with no ld64
      # equivalent; macOS ld rejects it ("ld: unknown options"), which breaks the
      # CMake compiler check. Only pass it on Linux where GNU ld is used.
      #
      unless OS.mac?
        args << "-DCMAKE_SHARED_LINKER_FLAGS='-Wl,--reduce-memory-overheads'"
        args << "-DCMAKE_EXE_LINKER_FLAGS='-Wl,--reduce-memory-overheads'"
      end
      args << '-DCMAKE_INSTALL_MESSAGE=LAZY'
      args << "-DCMAKE_INSTALL_PREFIX='#{prefix}/root'"
      args << '../llvm'
  
      system "cmake", "-G", "Ninja", *args
      system "ninja", "install"
    end
    bin.install_symlink "#{prefix}/root/bin/clang" => "mulle-clang"
    bin.install_symlink "#{prefix}/root/bin/nm" => "mulle-nm"
    bin.install_symlink "#{prefix}/root/bin/scan-build" => "mulle-scan-build"
  end

  def caveats
    str = <<~EOS
    To use mulle-clang inside homebrew formulae, you need a shim.
    See:
       https://github.com/mulle-kybernetik/mulle-clang-homebrew
    EOS
    str
  end

  test do
    system "#{bin}/mulle-clang", "--help"
  end
end
