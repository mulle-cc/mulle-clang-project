Create mulle-objc x86_64 compiler for **trixie and bookworm**. Just run the script.

> #### Harmless warning
> /home/nat/mono/mulle-clang-project/clang/lib/Sema/SemaDeclObjC.cpp:2916:10: warning: add explicit braces to avoid dangling else [-Wdangling-else]
> 2916 |          else
>

## Manual Usage

### Unix

#### Get git happening and clone cpack-mulle-clang:

``` bash
sudo apt-get install git sudo
git clone https://github.com/mulle-cc/mulle-clang-cpack.git
```

#### Build mulle-clang into a local opt folder:

Set `VERSION` appropriately:

``` bash
VERSION="17.0.6.0"
RC="" # e.g. -RC1
mkdir mono
cd mono
wget -O - "https://github.com/mulle-cc/mulle-clang-project/archive/${VERSION}${RC}.tar.gz" | tar xfz -
mv "mulle-clang-project-${VERSION}${RC}" mulle-clang-project
mkdir opt/mulle-clang-project
sudo ln -s "$PWD/opt/mulle-clang-project" "/opt/mulle-clang-project"
```

####  Build normally

``` bash
PREFIX="/opt" NAME="${VERSION}" ./mulle-clang-project/clang/bin/cmake-ninja.linux
```


#### Create .deb package and upload:

``` bash
cp ../cpack-mulle-clang/* .
chmod 755 generate-package
./generate-package
```
